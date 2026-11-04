#include "components/Pickups/Pickups.h"

#include "components/Enemies/Enemies.h"
#include "components/Player/Player.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ls::pickups {

namespace {

// Gem merging uses an open-addressing table keyed by the exact map cell. It is
// at least twice the gem pool, so it is never more than half full.
constexpr int kMergeSlots = 2048;
static_assert((kMergeSlots & (kMergeSlots - 1)) == 0, "slot count must be a power of two");
static_assert(kMergeSlots >= 2 * kMaxGems, "table must stay under half full");

struct MergeSlot {
    int cellX = 0;
    int cellY = 0;
    int gemIndex = -1;  // -1 means the slot is empty.
};

// The three random drops. Ammo is not among them: crates drop on their own counter.
constexpr PickupType kRandomDrops[] = {PickupType::Heal, PickupType::Magnet, PickupType::Bomb};

void applyPickup(GameState& state, PickupType type, Vec2 pos) {
    switch (type) {
        case PickupType::Heal:
            player::heal(state, state.player.maxHp * balance::kHealPickupFraction);
            break;
        case PickupType::Magnet:
            for (Gem& gem : state.gems) gem.attracted = true;
            break;
        case PickupType::Bomb: {
            const float radiusSq = balance::kBombRadius * balance::kBombRadius;
            for (int i = 0; i < state.enemies.count; ++i) {
                const Enemy& enemy = state.enemies[i];
                if (distanceSq(enemy.pos, pos) > radiusSq) continue;
                // The boss would melt in one bomb, so it only loses a small share.
                float damage = balance::kBombDamage;
                if (enemy.type == EnemyType::Boss) damage = std::min(damage, enemy.maxHp * balance::kBombBossMaxHpFraction);
                enemies::damageNoCredit(state, i, damage);
            }
            pushEvent(state, EventType::BombBlast, pos, balance::kBombRadius);
            break;
        }
        case PickupType::Ammo:
            weapons::addAmmoFraction(state, balance::kAmmoCrateFraction);
            break;
        case PickupType::Count:
            break;
    }
}

void collectPickups(GameState& state) {
    const float reachSq = balance::kPickupCollectRadius * balance::kPickupCollectRadius;
    for (int i = state.pickups.count - 1; i >= 0; --i) {
        const Pickup pickup = state.pickups[i];
        if (distanceSq(pickup.pos, state.player.pos) > reachSq) continue;
        state.pickups.removeAt(i);
        applyPickup(state, pickup.type, pickup.pos);
        pushEvent(state, EventType::PickupCollected, pickup.pos, 0.0f, static_cast<uint8_t>(pickup.type));
    }
}

}  // namespace

void spawnPickup(GameState& state, Vec2 pos, PickupType type) {
    if (state.pickups.count >= state.pickups.capacity()) state.pickups.removeAt(0);
    Pickup* pickup = state.pickups.add();
    pickup->pos = pos;
    pickup->type = type;
}

void dropGem(GameState& state, Vec2 pos, int xp) {
    if (xp <= 0) return;
    Gem* gem = state.gems.add();
    if (!gem) {
        upgrades::addXp(state, xp);
        return;
    }
    gem->pos = pos;
    gem->prevPos = pos;
    gem->xp = xp;
}

void onEnemyKilled(GameState& state, Vec2 pos, bool elite) {
    RunState& run = state.run;
    int dropped = 0;
    const auto drop = [&](PickupType type) {
        // Later drops from the same kill sit a little to the side so they do not overlap.
        const Vec2 offset = fromAngle(static_cast<float>(dropped) * 2.1f) * (balance::kPickupDropOffset * static_cast<float>(dropped));
        spawnPickup(state, pos + offset, type);
        dropped++;
    };

    // Called after the kill is counted, so kills is this kill's number.
    if (run.kills > 0 && run.kills % balance::kKillsPerAmmoCrate == 0) drop(PickupType::Ammo);
    if (run.kills >= run.nextPickupDropKills) {
        run.nextPickupDropKills += balance::kKillsPerPickupDrop;
        drop(kRandomDrops[state.rng.below(static_cast<int>(std::size(kRandomDrops)))]);
    }
    // Elites are rare, so each one always pays out, and may give any type.
    if (elite) drop(static_cast<PickupType>(state.rng.below(static_cast<int>(PickupType::Count))));
}

void mergeGems(GameState& state) {
    // One pass, O(n). Each map cell keeps the first gem seen in it, and later gems
    // in the same cell add their XP to it and are dropped. Gems that stay are packed
    // to the front as we go, so no removal ever has to fix up an index.
    // Cells are compared exactly, so gems from far-apart cells never merge.
    MergeSlot table[kMergeSlots];
    int kept = 0;
    for (int i = 0; i < state.gems.count; ++i) {
        const Gem gem = state.gems[i];
        if (gem.attracted) {  // Already flying to the player. Leave it alone.
            state.gems[kept++] = gem;
            continue;
        }
        const int cellX = static_cast<int>(std::floor(gem.pos.x / balance::kGemMergeCellSize));
        const int cellY = static_cast<int>(std::floor(gem.pos.y / balance::kGemMergeCellSize));
        uint32_t slot = (static_cast<uint32_t>(cellX) * 73856093u ^ static_cast<uint32_t>(cellY) * 19349663u) & (kMergeSlots - 1);
        while (table[slot].gemIndex >= 0 && (table[slot].cellX != cellX || table[slot].cellY != cellY)) {
            slot = (slot + 1) & (kMergeSlots - 1);
        }
        if (table[slot].gemIndex >= 0) {
            state.gems[table[slot].gemIndex].xp += gem.xp;
            continue;
        }
        table[slot] = MergeSlot{cellX, cellY, kept};
        state.gems[kept++] = gem;
    }
    state.gems.count = kept;
}

void update(GameState& state) {
    Player& player = state.player;
    const float range = player.stats.pickupRange;
    const float collectDistance = balance::kPlayerRadius + 6.0f;

    for (int i = state.gems.count - 1; i >= 0; --i) {
        Gem& gem = state.gems[i];
        gem.prevPos = gem.pos;
        const Vec2 toPlayer = player.pos - gem.pos;
        const float distSq = lengthSq(toPlayer);
        if (!gem.attracted && distSq < range * range) gem.attracted = true;
        if (gem.attracted) {
            gem.pos += normalizedOr(toPlayer, Vec2{}) * (balance::kGemAttractSpeed * kDt);
        }
        if (distSq < collectDistance * collectDistance) {
            const int xp = gem.xp;
            pushEvent(state, EventType::GemCollected, gem.pos, static_cast<float>(xp));
            state.gems.removeAt(i);
            upgrades::addXp(state, xp);
        }
    }

    collectPickups(state);

    if (state.gems.count > balance::kGemMergeStartCount && state.run.tick % 30 == 0) {
        mergeGems(state);
    }
}

}  // namespace ls::pickups
