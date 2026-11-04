#include "components/Pickups/Pickups.h"

#include "components/Upgrades/Upgrades.h"
#include "data/Balance.h"

#include <cmath>
#include <cstring>

namespace ls::pickups {

namespace {

constexpr int kMergeBuckets = 512;

}  // namespace

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
    (void)state;
    (void)pos;
    (void)elite;
}

void mergeGems(GameState& state) {
    // Each bucket remembers the first gem seen in that map cell. Later gems in
    // the same cell give it their XP and disappear.
    int16_t firstInBucket[kMergeBuckets];
    std::memset(firstInBucket, -1, sizeof(firstInBucket));
    for (int i = state.gems.count - 1; i >= 0; --i) {
        Gem& gem = state.gems[i];
        if (gem.attracted) continue;
        const int cellX = static_cast<int>(std::floor(gem.pos.x / balance::kGemMergeCellSize));
        const int cellY = static_cast<int>(std::floor(gem.pos.y / balance::kGemMergeCellSize));
        const int bucket = (cellX * 31 + cellY * 17) & (kMergeBuckets - 1);
        const int first = firstInBucket[bucket];
        if (first < 0) {
            firstInBucket[bucket] = static_cast<int16_t>(i);
            continue;
        }
        state.gems[first].xp += gem.xp;
        // Moves the last gem into slot i. Any gem at the last index was already
        // visited, so if a bucket pointed at it, that bucket must now point at i.
        const int last = state.gems.count - 1;
        state.gems.removeAt(i);
        if (last != i) {
            for (int16_t& entry : firstInBucket) {
                if (entry == last) entry = static_cast<int16_t>(i);
            }
        }
    }
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

    if (state.gems.count > balance::kGemMergeStartCount && state.run.tick % 30 == 0) {
        mergeGems(state);
    }
}

}  // namespace ls::pickups
