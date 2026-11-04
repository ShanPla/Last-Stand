#pragma once

#include "components/Effects/Budget/Budget.h"
#include "components/WorldView/Projection.h"
#include "core/Math.h"

#include <cstdint>

// Damage numbers that stay readable with thousands of zombies. Nearby hits add up into
// one number instead of each showing its own, new small numbers are rate limited, and
// there is a hard cap. A hit far above the usual size is shown bigger, like a crit.
// The logic here needs no window, so a unit test can throw any amount of hits at it.
namespace ls::effects {

using worldview::View;
using worldview::ScreenPoint;
using worldview::project;
using worldview::onTarget;

enum NumberTier : uint8_t { kTierSmall, kTierBig, kTierHuge };

struct DamageNumber {
    Vec2 world;
    float height = 0.0f;  // Art pixels above the ground.
    float value = 0.0f;
    float age = 0.0f;     // Seconds since it was last added to. It fades by this.
    float total = 0.0f;   // Seconds since it first showed. It rises by this.
    float pop = 0.0f;     // Seconds left of being drawn larger.
    float life = 0.0f;
    NumberTier tier = kTierSmall;
    bool used = false;
};

class DamageNumberPool {
public:
    static constexpr int kCap = 16;
    static constexpr float kSmallLife = 0.5f;
    static constexpr float kBigLife = 0.75f;
    static constexpr float kHugeLife = 0.95f;
    static constexpr float kMergeRadius = 44.0f;    // World units.
    static constexpr float kMergeLonger = 2.5f;     // The radius grows this much once the pool is filling.
    static constexpr float kMergeWindow = 0.22f;    // A number only takes hits while this young.
    static constexpr float kMergeLifetime = 0.9f;   // A long stream starts a fresh number after this.
    static constexpr float kPopSeconds = 0.09f;
    static constexpr float kBigOverTypical = 2.5f;
    static constexpr float kHugeOverTypical = 7.0f;
    static constexpr float kTypicalFollow = 0.015f;  // How fast "a usual hit" follows the build as it grows.

    void clear() {
        for (DamageNumber& number : numbers_) number = DamageNumber{};
        live_ = 0;
        typical_ = 0.0f;
    }

    void add(Vec2 world, float height, float amount) {
        if (amount <= 0.0f) return;
        typical_ = typical_ <= 0.0f ? amount : typical_ + (amount - typical_) * kTypicalFollow;

        // Even big hits are rate limited: if every number shouts, none of them stands out.
        NumberTier tier = tierFor(amount);
        if (tier == kTierHuge && !hugeBudget_.take()) tier = kTierBig;
        if (tier == kTierBig && !bigBudget_.take()) tier = kTierSmall;

        // Merging first: it costs no space and keeps the screen calm. Only numbers of the
        // same size add up, so a run of small hits can't turn into a giant total.
        const float radius = live_ * 4 >= kCap * 3 ? kMergeRadius * kMergeLonger : kMergeRadius;
        DamageNumber* nearest = nullptr;
        float nearestSq = radius * radius;
        for (DamageNumber& number : numbers_) {
            if (!number.used || number.tier != tier || number.age > kMergeWindow || number.total > kMergeLifetime) continue;
            const float distSq = distanceSq(number.world, world);
            if (distSq < nearestSq) {
                nearestSq = distSq;
                nearest = &number;
            }
        }
        if (nearest) {
            nearest->value += amount;
            nearest->age = 0.0f;
            merged_++;
            return;
        }

        if (tier == kTierSmall && !smallBudget_.take()) {
            dropped_++;
            return;
        }
        DamageNumber* slot = findSlot(tier);
        if (!slot) {
            dropped_++;
            return;
        }
        if (!slot->used) live_++;
        *slot = DamageNumber{};
        slot->used = true;
        slot->world = world;
        slot->height = height;
        slot->value = amount;
        slot->tier = tier;
        slot->life = tier == kTierHuge ? kHugeLife : (tier == kTierBig ? kBigLife : kSmallLife);
        if (tier != kTierSmall) slot->pop = kPopSeconds;
        spawned_++;
    }

    void update(float dt) {
        smallBudget_.refill(dt, 1.0f);
        bigBudget_.refill(dt, 1.0f);
        hugeBudget_.refill(dt, 1.0f);
        for (DamageNumber& number : numbers_) {
            if (!number.used) continue;
            number.age += dt;
            number.total += dt;
            number.pop -= dt;
            if (number.age >= number.life) {
                number.used = false;
                live_--;
            }
        }
    }

    const DamageNumber* begin() const { return numbers_; }
    const DamageNumber* end() const { return numbers_ + kCap; }
    int live() const { return live_; }
    int spawned() const { return spawned_; }
    int merged() const { return merged_; }
    int dropped() const { return dropped_; }

private:
    NumberTier tierFor(float value) const {
        if (value >= typical_ * kHugeOverTypical) return kTierHuge;
        if (value >= typical_ * kBigOverTypical) return kTierBig;
        return kTierSmall;
    }

    // A free slot, or for a big hit the smallest number showing, so that bigger always
    // wins over smaller. A small hit never pushes anything out.
    DamageNumber* findSlot(NumberTier tier) {
        DamageNumber* weakest = nullptr;
        for (DamageNumber& number : numbers_) {
            if (!number.used) return &number;
            if (number.tier < tier && (!weakest || number.value < weakest->value)) weakest = &number;
        }
        return weakest;
    }

    DamageNumber numbers_[kCap];
    Budget smallBudget_{9.0f, 5.0f};
    Budget bigBudget_{5.0f, 3.0f};
    Budget hugeBudget_{2.0f, 2.0f};
    float typical_ = 0.0f;
    int live_ = 0;
    int spawned_ = 0;
    int merged_ = 0;
    int dropped_ = 0;
};

// Bakes the digit texture. Needs a live graphics context.
void loadNumberGlyphs();

// Draws every number as one textured quad per digit, on whole art pixels. rise is false
// under reduced motion. overTelegraph says where a number must be see-through.
void drawNumberPool(const View& view, const DamageNumberPool& pool, bool rise, bool (*overTelegraph)(Vec2 world));

}  // namespace ls::effects
