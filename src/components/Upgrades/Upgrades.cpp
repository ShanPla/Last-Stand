#include "components/Upgrades/Upgrades.h"

#include "components/Player/Player.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"

#include <iterator>

namespace ls::upgrades {

namespace {

constexpr int kWeaponTypes = static_cast<int>(WeaponId::Count);
constexpr int kPassiveTypes = static_cast<int>(PassiveId::Count);

WeaponSlot* findWeapon(Player& player, WeaponId id) {
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == id) return &player.weapons[i];
    }
    return nullptr;
}

PassiveSlot* findPassive(Player& player, PassiveId id) {
    for (int i = 0; i < player.passiveCount; ++i) {
        if (player.passives[i].id == id) return &player.passives[i];
    }
    return nullptr;
}

// Lists every card the player could take right now.
int collectCandidates(GameState& state, Card* out, int maxCards) {
    Player& player = state.player;
    int count = 0;
    const auto add = [&](const Card& card) {
        if (count < maxCards) out[count++] = card;
    };

    for (const balance::Evolution& evolution : balance::kEvolutions) {
        const WeaponSlot* weapon = findWeapon(player, evolution.weapon);
        if (weapon && weapon->level >= kMaxLevelPerItem && !weapon->evolved && findPassive(player, evolution.passive)) {
            add(Card{CardKind::Evolution, evolution.weapon, PassiveId::Damage, static_cast<uint8_t>(kMaxLevelPerItem)});
        }
    }
    for (int i = 0; i < kWeaponTypes; ++i) {
        const WeaponId id = static_cast<WeaponId>(i);
        const WeaponSlot* held = findWeapon(player, id);
        if (held && held->level < kMaxLevelPerItem) {
            add(Card{CardKind::Weapon, id, PassiveId::Damage, static_cast<uint8_t>(held->level + 1)});
        } else if (!held && player.weaponCount < kMaxHeldWeapons) {
            add(Card{CardKind::Weapon, id, PassiveId::Damage, 1});
        }
    }
    for (int i = 0; i < kPassiveTypes; ++i) {
        const PassiveId id = static_cast<PassiveId>(i);
        const PassiveSlot* held = findPassive(player, id);
        if (held && held->level < kMaxLevelPerItem) {
            add(Card{CardKind::Passive, WeaponId::Pulse, id, static_cast<uint8_t>(held->level + 1)});
        } else if (!held && player.passiveCount < kMaxHeldPassives) {
            add(Card{CardKind::Passive, WeaponId::Pulse, id, 1});
        }
    }
    return count;
}

void buildOffers(GameState& state) {
    Card candidates[kWeaponTypes + kPassiveTypes + 8];
    const int candidateCount = collectCandidates(state, candidates, static_cast<int>(std::size(candidates)));
    RunState& run = state.run;
    run.offerCount = 0;

    // Evolutions are always shown first, because they are the build's payoff.
    int remaining = candidateCount;
    for (int i = 0; i < remaining && run.offerCount < kOffersPerLevel;) {
        if (candidates[i].kind == CardKind::Evolution) {
            run.offers[run.offerCount++] = candidates[i];
            candidates[i] = candidates[--remaining];
        } else {
            ++i;
        }
    }
    while (run.offerCount < kOffersPerLevel && remaining > 0) {
        const int pick = state.rng.below(remaining);
        run.offers[run.offerCount++] = candidates[pick];
        candidates[pick] = candidates[--remaining];
    }
    // A finished build still gets a useful choice.
    if (run.offerCount == 0) {
        run.offers[run.offerCount++] = Card{CardKind::Heal, WeaponId::Pulse, PassiveId::Damage, 1};
    }
}

}  // namespace

void addXp(GameState& state, int amount) {
    Player& player = state.player;
    player.xp += amount;
    while (player.xp >= player.xpToNext) {
        player.xp -= player.xpToNext;
        player.level++;
        player.xpToNext = balance::xpForLevel(player.level);
        state.run.pendingLevels++;
    }
}

void update(GameState& state) {
    RunState& run = state.run;
    if (run.phase == RunPhase::Playing && run.pendingLevels > 0 && state.player.hp > 0.0f) {
        buildOffers(state);
        run.phase = RunPhase::LevelUp;
        pushEvent(state, EventType::LevelUp, state.player.pos, static_cast<float>(state.player.level));
    }
}

void choose(GameState& state, int index) {
    RunState& run = state.run;
    if (run.phase != RunPhase::LevelUp || index < 0 || index >= run.offerCount) return;
    const Card card = run.offers[index];
    Player& player = state.player;

    switch (card.kind) {
        case CardKind::Weapon:
            if (WeaponSlot* held = findWeapon(player, card.weapon)) {
                held->level = card.newLevel;
            } else {
                weapons::grant(state, card.weapon);
            }
            break;
        case CardKind::Passive:
            if (PassiveSlot* held = findPassive(player, card.passive)) {
                held->level = card.newLevel;
            } else if (player.passiveCount < kMaxHeldPassives) {
                PassiveSlot& slot = player.passives[player.passiveCount++];
                slot.id = card.passive;
                slot.level = 1;
            }
            recomputeStats(state);
            break;
        case CardKind::Evolution:
            if (WeaponSlot* held = findWeapon(player, card.weapon)) {
                held->evolved = true;
                pushEvent(state, EventType::EvolutionGained, player.pos, 0.0f, static_cast<uint8_t>(card.weapon));
            }
            break;
        case CardKind::Heal:
            player::heal(state, player.maxHp * balance::kHealCardFraction);
            break;
    }

    run.pendingLevels--;
    if (run.pendingLevels > 0) {
        buildOffers(state);
        pushEvent(state, EventType::LevelUp, player.pos, static_cast<float>(player.level));
    } else {
        run.phase = RunPhase::Playing;
    }
}

void reroll(GameState& state) {
    RunState& run = state.run;
    if (run.phase != RunPhase::LevelUp || run.rerollsLeft <= 0) return;
    run.rerollsLeft--;
    buildOffers(state);
}

void recomputeStats(GameState& state) {
    Player& player = state.player;
    PlayerStats stats;
    stats.moveSpeed = balance::kPlayerSpeed;
    stats.pickupRange = balance::kPlayerPickupRange;
    for (int i = 0; i < player.passiveCount; ++i) {
        const int level = player.passives[i].level - 1;
        switch (player.passives[i].id) {
            case PassiveId::Damage: stats.damageMult = balance::kPassiveDamage[level]; break;
            case PassiveId::FireRate: stats.cooldownMult = balance::kPassiveCooldown[level]; break;
            case PassiveId::Area: stats.areaMult = balance::kPassiveArea[level]; break;
            case PassiveId::MoveSpeed: stats.moveSpeed = balance::kPlayerSpeed * balance::kPassiveMoveSpeed[level]; break;
            case PassiveId::PickupRange: stats.pickupRange = balance::kPlayerPickupRange * balance::kPassivePickupRange[level]; break;
            case PassiveId::Armor: stats.armor = balance::kPassiveArmor[level]; break;
            case PassiveId::Count: break;
        }
    }
    player.stats = stats;
}

}  // namespace ls::upgrades
