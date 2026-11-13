#pragma once

#include "data/Balance.h"

#include <cstdint>

// How strong the shield bubble around a co-op soldier looks. This is only arithmetic, so the
// unit tests can check it without a window. The drawing itself is in Characters.cpp.
namespace ls::worldview {

struct ShieldBubbleLook {
    float outline = 0.0f;  // 0 to 1: how solid the bubble's edge is. 0 means nothing is drawn.
    float fill = 0.0f;     // 0 to 1: how much of the faint body shows.
};

// secondsLeft is the player's cardShield. tick is the simulation's step counter, which every
// player in the run shares. animate is false under reduced motion or with flashes turned off:
// the bubble then stays steady and only fades out at the end. flashScale is the flash
// intensity setting from 0 to 1, which scales how far the swell dims the bubble.
inline ShieldBubbleLook shieldBubbleLook(float secondsLeft, uint32_t tick, bool animate, float flashScale) {
    ShieldBubbleLook look;
    if (!(secondsLeft > 0.0f)) return look;

    // Most of the time the bubble swells slowly, between full strength and a little dimmer.
    float factor = 1.0f;
    if (animate) {
        const float half = static_cast<float>(balance::kCoopShieldPulseTicks) * 0.5f;
        const float at = static_cast<float>(tick % balance::kCoopShieldPulseTicks) - half;
        const float wave = 1.0f - (at < 0.0f ? -at : at) / half;  // 0 to 1 to 0.
        const float scale = flashScale < 0.0f ? 0.0f : (flashScale > 1.0f ? 1.0f : flashScale);
        factor = 1.0f - balance::kCoopShieldPulseDepth * scale * (1.0f - wave);
    }

    // In the last second it fades out, and where flicker is allowed it blinks as well.
    // The fade keeps a floor, so the bubble is still plainly there until it ends.
    if (secondsLeft < balance::kCoopShieldWarnSeconds) {
        const float left = secondsLeft / balance::kCoopShieldWarnSeconds;
        factor *= 0.2f + 0.8f * left;
        if (animate && ((tick / balance::kCoopShieldBlinkTicks) & 1u) != 0u) factor *= 0.25f;
    }

    look.outline = 0.9f * factor;
    look.fill = 0.22f * factor;
    return look;
}

}  // namespace ls::worldview
