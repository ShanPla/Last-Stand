// The co-op shield bubble: how strong it looks at each moment. The drawing itself needs a
// window, so the browser test (coopshield.test.mjs) looks at that.
#include "doctest.h"

#include <algorithm>

#include "components/WorldView/ShieldBubble.h"
#include "data/Balance.h"

using namespace ls;
using namespace ls::worldview;

TEST_CASE("shield bubble: nothing is drawn once the shield is gone") {
    CHECK(shieldBubbleLook(0.0f, 10, true, 1.0f).outline == 0.0f);
    CHECK(shieldBubbleLook(-0.5f, 10, false, 1.0f).fill == 0.0f);
}

TEST_CASE("shield bubble: while the shield is up it is always visible, and never too strong") {
    for (uint32_t tick = 0; tick < 400; ++tick) {
        for (const bool animate : {true, false}) {
            for (const float left : {5.0f, 3.3f, 1.0f}) {
                const ShieldBubbleLook look = shieldBubbleLook(left, tick, animate, 1.0f);
                CHECK(look.outline > 0.5f);
                CHECK(look.outline <= 0.9f + 1e-5f);
                CHECK(look.fill > 0.0f);
                CHECK(look.fill < look.outline);
            }
        }
    }
}

TEST_CASE("shield bubble: it swells slowly, and the flash setting scales the swell") {
    float lowest = 10.0f;
    float highest = 0.0f;
    for (uint32_t tick = 0; tick < balance::kCoopShieldPulseTicks; ++tick) {
        const float v = shieldBubbleLook(4.0f, tick, true, 1.0f).outline;
        lowest = std::min(lowest, v);
        highest = std::max(highest, v);
    }
    CHECK(highest == doctest::Approx(0.9f));
    CHECK(lowest == doctest::Approx(0.9f * (1.0f - balance::kCoopShieldPulseDepth)).epsilon(0.02));
    // With flashes at zero the swell is gone but the bubble stays.
    for (uint32_t tick = 0; tick < balance::kCoopShieldPulseTicks; ++tick) CHECK(shieldBubbleLook(4.0f, tick, true, 0.0f).outline == doctest::Approx(0.9f));
    // Half the flash intensity gives half the swell.
    float halfLowest = 10.0f;
    for (uint32_t tick = 0; tick < balance::kCoopShieldPulseTicks; ++tick) halfLowest = std::min(halfLowest, shieldBubbleLook(4.0f, tick, true, 0.5f).outline);
    CHECK(halfLowest == doctest::Approx(0.9f * (1.0f - balance::kCoopShieldPulseDepth * 0.5f)).epsilon(0.02));
}

TEST_CASE("shield bubble: reduced motion keeps it perfectly steady until the last second") {
    const float steady = shieldBubbleLook(4.0f, 0, false, 1.0f).outline;
    for (uint32_t tick = 0; tick < 300; ++tick) {
        CHECK(shieldBubbleLook(4.0f, tick, false, 1.0f).outline == doctest::Approx(steady));
        CHECK(shieldBubbleLook(1.5f, tick, false, 1.0f).outline == doctest::Approx(steady));
    }
}

TEST_CASE("shield bubble: the last second blinks, or fades smoothly where flicker is off") {
    // Animated: both a strong and a weak look appear in the last second, and the blink keeps
    // to 3 changes a second at most.
    const float before = shieldBubbleLook(balance::kCoopShieldWarnSeconds + 0.5f, 0, true, 1.0f).outline;
    bool sawStrong = false;
    bool sawWeak = false;
    int changes = 0;
    bool last = false;
    for (uint32_t tick = 0; tick < 60; ++tick) {
        const float left = balance::kCoopShieldWarnSeconds - 0.0001f - static_cast<float>(tick) / 60.0f;
        const float v = shieldBubbleLook(left, tick, true, 1.0f).outline;
        const bool strong = v > before * 0.5f;
        if (tick > 0 && strong != last) ++changes;
        last = strong;
        sawStrong = sawStrong || strong;
        sawWeak = sawWeak || !strong;
    }
    CHECK(sawStrong);
    CHECK(sawWeak);
    CHECK(changes <= 6);  // 3 blinks a second is 6 changes.

    // Not animated: no blinking at all, only a steady drop that never reaches zero before the end.
    float previous = 10.0f;
    for (int ms = 999; ms >= 5; ms -= 7) {
        const float v = shieldBubbleLook(static_cast<float>(ms) / 1000.0f, 7, false, 1.0f).outline;
        CHECK(v <= previous + 1e-6f);
        CHECK(v > 0.1f);
        previous = v;
    }
    CHECK(shieldBubbleLook(0.9f, 0, false, 1.0f).outline > shieldBubbleLook(0.1f, 0, false, 1.0f).outline);
}
