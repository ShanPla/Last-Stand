#include "components/Pickups/Pickups.h"

#include "components/Enemies/Enemies.h"
#include "components/Obstacles/Obstacles.h"
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

// Removes a pickup and closes the gap, so the pool stays in drop order and slot 0 is
// always the oldest. The pool's own removeAt swaps the last item in, which would break
// that. Shifting is fine here, because the pool is tiny and this happens rarely.
void removeKeepingOrder(GameState& state, int index) {
    for (int i = index + 1; i < state.pickups.count; ++i) state.pickups[i - 1] = state.pickups[i];
    state.pickups.count--;
}

// The bomb clears the screen. In co-op it clears every standing player's screen, because
// a bomb helps the whole team: each zombie near the bomb or near any standing player is hit
// once. With one player only the bomb's own spot counts, as it always has.
void detonateBomb(GameState& state, Vec2 pos) {
    Vec2 centers[kMaxPlayers + 1];
    int centerCount = 0;
    centers[centerCount++] = pos;
    if (state.isCoop()) {
        for (int slot = 0; slot < state.playerCount(); ++slot) {
            if (standing(state, slot)) centers[centerCount++] = state.players[slot].pos;
        }
    }
    const float radiusSq = balance::kBombRadius * balance::kBombRadius;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        bool inside = false;
        for (int c = 0; c < centerCount && !inside; ++c) inside = distanceSq(enemy.pos, centers[c]) <= radiusSq;
        if (!inside) continue;
        // The boss would melt in one bomb, so it only loses a small share.
        float damage = balance::kBombDamage;
        if (enemy.type == EnemyType::Boss) damage = std::min(damage, enemy.maxHp * balance::kBombBossMaxHpFraction);
        enemies::damageNoCredit(state, i, damage);
    }
    for (int c = 0; c < centerCount; ++c) pushEvent(state, EventType::BombBlast, centers[c], balance::kBombRadius);
}

// Heal and ammo go to the player who picked them up. Magnet and bomb help the whole team.
void applyPickup(GameState& state, int slot, PickupType type, Vec2 pos) {
    switch (type) {
        case PickupType::Heal:
            player::heal(state, slot, state.players[slot].maxHp * balance::kHealPickupFraction);
            break;
        case PickupType::Magnet:
            // Every gem flies to whoever is nearest, so the whole team shares the pull.
            for (Gem& gem : state.gems) gem.attracted = true;
            break;
        case PickupType::Bomb:
            detonateBomb(state, pos);
            break;
        case PickupType::Ammo:
            weapons::addAmmoFraction(state, slot, balance::kAmmoCrateFraction);
            break;
        case PickupType::Count:
            break;
    }
}

// A pickup goes to the first standing player in reach, in slot order.
void collectPickups(GameState& state) {
    const float reachSq = balance::kPickupCollectRadius * balance::kPickupCollectRadius;
    for (int i = state.pickups.count - 1; i >= 0; --i) {
        const Pickup pickup = state.pickups[i];
        int taker = -1;
        for (int slot = 0; slot < state.playerCount() && taker < 0; ++slot) {
            if (standing(state, slot) && distanceSq(pickup.pos, state.players[slot].pos) <= reachSq) taker = slot;
        }
        if (taker < 0) continue;
        removeKeepingOrder(state, i);
        applyPickup(state, taker, pickup.type, pickup.pos);
        pushPlayerEvent(state, EventType::PickupCollected, taker, pickup.pos, 0.0f, static_cast<uint8_t>(pickup.type));
    }
}

}  // namespace

void spawnPickup(GameState& state, Vec2 pos, PickupType type) {
    if (state.pickups.count >= state.pickups.capacity()) removeKeepingOrder(state, 0);
    Pickup* pickup = state.pickups.add();
    // Nothing lands inside a car, where it could never be reached.
    pickup->pos = obstacles::placeItem(state, pos);
    pickup->type = type;
}

void dropGem(GameState& state, Vec2 pos, int xp) {
    if (xp <= 0) return;
    Gem* gem = state.gems.add();
    if (!gem) {
        // The XP goes to the standing player nearest to the drop, who would most likely have got it.
        const int nearest = nearestStanding(state, pos);
        upgrades::addXp(state, nearest < 0 ? 0 : nearest, xp);
        return;
    }
    gem->pos = obstacles::placeItem(state, pos);
    gem->prevPos = gem->pos;
    gem->xp = xp;
}

void onEnemyKilled(GameState& state, Vec2 pos, bool elite) {
    RunState& run = state.run;
    int dropped = 0;
    const auto drop = [&](PickupType type) {
        // Later drops from the same kill sit a little to the side so they do not overlap.
        const Vec2 offset = fromAngle(static_cast<float>(dropped) * balance::kPickupDropFanRadians) * (balance::kPickupDropOffset * static_cast<float>(dropped));
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
    const float collectDistance = balance::kPlayerRadius + balance::kGemCollectMargin;
    const int players = state.playerCount();

    for (int i = state.gems.count - 1; i >= 0; --i) {
        Gem& gem = state.gems[i];
        gem.prevPos = gem.pos;
        // Gems belong to whoever collects them. Two players matter for each gem: the nearest
        // standing one, who collects it when close enough, and the nearest one whose pickup
        // range reaches it, who pulls it in. With one player both are that player.
        int nearest = -1;
        float nearestSq = 0.0f;
        int puller = -1;
        float pullerSq = 0.0f;
        for (int slot = 0; slot < players; ++slot) {
            if (!standing(state, slot)) continue;
            const Player& player = state.players[slot];
            const float distSq = lengthSq(player.pos - gem.pos);
            if (nearest < 0 || distSq < nearestSq) {
                nearest = slot;
                nearestSq = distSq;
            }
            const float range = player.stats.pickupRange;
            if (distSq < range * range && (puller < 0 || distSq < pullerSq)) {
                puller = slot;
                pullerSq = distSq;
            }
        }
        if (nearest < 0) continue;  // Everyone is down. The run ends at the end of this step.
        if (!gem.attracted && puller >= 0) gem.attracted = true;
        if (gem.attracted) {
            // A magnet pulls gems from out of everyone's range, to the nearest player.
            const Vec2 toPlayer = state.players[puller >= 0 ? puller : nearest].pos - gem.pos;
            gem.pos += normalizedOr(toPlayer, Vec2{}) * (balance::kGemAttractSpeed * kDt);
        }
        if (nearestSq < collectDistance * collectDistance) {
            const int xp = gem.xp;
            pushPlayerEvent(state, EventType::GemCollected, nearest, gem.pos, static_cast<float>(xp));
            state.gems.removeAt(i);
            upgrades::addXp(state, nearest, xp);
        }
    }

    collectPickups(state);

    if (state.gems.count > balance::kGemMergeStartCount && state.run.tick % balance::kGemMergeIntervalSteps == 0) {
        mergeGems(state);
    }
}

}  // namespace ls::pickups
