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
int collectCandidates(Player& player, Card* out, int maxCards) {
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
            add(Card{CardKind::Passive, WeaponId::Rifle, id, static_cast<uint8_t>(held->level + 1)});
        } else if (!held && player.passiveCount < kMaxHeldPassives) {
            add(Card{CardKind::Passive, WeaponId::Rifle, id, 1});
        }
    }
    return count;
}

// Draws from the run's one random stream, so in co-op the order players level up in is
// part of the run, like everything else.
void buildOffers(GameState& state, int slot) {
    Player& player = state.players[slot];
    Card candidates[kWeaponTypes + kPassiveTypes + 8];
    const int candidateCount = collectCandidates(player, candidates, static_cast<int>(std::size(candidates)));
    player.offerCount = 0;

    // Evolutions are always shown first, because they are the build's payoff.
    int remaining = candidateCount;
    for (int i = 0; i < remaining && player.offerCount < kOffersPerLevel;) {
        if (candidates[i].kind == CardKind::Evolution) {
            player.offers[player.offerCount++] = candidates[i];
            candidates[i] = candidates[--remaining];
        } else {
            ++i;
        }
    }
    while (player.offerCount < kOffersPerLevel && remaining > 0) {
        const int pick = state.rng.below(remaining);
        player.offers[player.offerCount++] = candidates[pick];
        candidates[pick] = candidates[--remaining];
    }
    // A finished build still gets a useful choice.
    if (player.offerCount == 0) {
        player.offers[player.offerCount++] = Card{CardKind::Heal, WeaponId::Rifle, PassiveId::Damage, 1};
    }
}

}  // namespace

void addXp(GameState& state, int slot, int amount) {
    Player& player = state.players[slot];
    player.xp += amount;
    while (player.xp >= player.xpToNext) {
        player.xp -= player.xpToNext;
        player.level++;
        player.xpToNext = balance::xpForLevel(player.level);
        player.pendingLevels++;
    }
}

void update(GameState& state) {
    RunState& run = state.run;
    if (!state.isCoop()) {
        Player& player = state.firstPlayer();
        if (run.phase == RunPhase::Playing && player.pendingLevels > 0 && player.hp > 0.0f) {
            buildOffers(state, 0);
            run.phase = RunPhase::LevelUp;
            pushPlayerEvent(state, EventType::LevelUp, 0, player.pos, static_cast<float>(player.level));
        }
        return;
    }
    // Co-op, in slot order. A player who is down or at 0 HP gets their cards once back up.
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        Player& player = state.players[slot];
        if (player.cardsOpen || player.pendingLevels <= 0 || player.downed || player.hp <= 0.0f) continue;
        buildOffers(state, slot);
        player.cardsOpen = true;
        player.cardShield = balance::kCoopCardShieldSeconds;
        pushPlayerEvent(state, EventType::LevelUp, slot, player.pos, static_cast<float>(player.level));
    }
}

bool choosing(const GameState& state, int slot) {
    if (state.isCoop()) return state.players[slot].cardsOpen;
    return slot == 0 && state.run.phase == RunPhase::LevelUp;
}

void choose(GameState& state, int slot, int index) {
    Player& player = state.players[slot];
    if (!choosing(state, slot) || index < 0 || index >= player.offerCount) return;
    const Card card = player.offers[index];

    switch (card.kind) {
        case CardKind::Weapon:
            if (WeaponSlot* held = findWeapon(player, card.weapon)) {
                held->level = card.newLevel;
                weapons::refill(player, *held);
            } else {
                weapons::grant(state, slot, card.weapon);
            }
            break;
        case CardKind::Passive:
            if (PassiveSlot* held = findPassive(player, card.passive)) {
                held->level = card.newLevel;
            } else if (player.passiveCount < kMaxHeldPassives) {
                PassiveSlot& passive = player.passives[player.passiveCount++];
                passive.id = card.passive;
                passive.level = 1;
            }
            recomputeStats(state, slot);
            break;
        case CardKind::Evolution:
            if (WeaponSlot* held = findWeapon(player, card.weapon)) {
                held->evolved = true;
                pushPlayerEvent(state, EventType::EvolutionGained, slot, player.pos, 0.0f, static_cast<uint8_t>(card.weapon));
            }
            break;
        case CardKind::Heal:
            player::heal(state, slot, player.maxHp * balance::kHealCardFraction);
            break;
    }

    player.pendingLevels--;
    if (player.pendingLevels > 0) {
        // The next level's cards follow at once. In co-op the card shield keeps running, so a
        // pile of levels can't make it last longer than one shield.
        buildOffers(state, slot);
        pushPlayerEvent(state, EventType::LevelUp, slot, player.pos, static_cast<float>(player.level));
    } else if (state.isCoop()) {
        player.cardsOpen = false;
        player.cardShield = 0.0f;
    } else {
        state.run.phase = RunPhase::Playing;
    }
}

void reroll(GameState& state, int slot) {
    Player& player = state.players[slot];
    if (!choosing(state, slot) || player.rerollsLeft <= 0) return;
    player.rerollsLeft--;
    buildOffers(state, slot);
}

void recomputeStats(GameState& state, int slot) {
    Player& player = state.players[slot];
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
