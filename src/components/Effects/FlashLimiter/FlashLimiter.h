#pragma once

// The one gate every full-screen flash goes through. Fast full-screen flashing can
// cause seizures, and the usual safety limit is 3 flashes per second, so this stays
// well under it no matter how many big events land at once. It is a pure function on
// plain data, so a unit test can prove the limit without a window.
namespace ls::effects {

inline constexpr float kFlashMinGapSeconds = 1.0f;  // At most one a second.

struct FlashLimiter {
    float lastFlashSeconds = -1000.0f;
};

// Asks to flash at this strength, from 0 to 1. Returns the strength to really draw:
// scaled by the player's intensity setting, or 0 when it came too soon after the last
// one. A flash scaled to nothing doesn't count, so it can't use up the next real one.
inline float limitFlash(FlashLimiter& limiter, float nowSeconds, float strength, float intensity) {
    const float scaled = strength * intensity;
    if (scaled <= 0.0f) return 0.0f;
    if (nowSeconds - limiter.lastFlashSeconds < kFlashMinGapSeconds) return 0.0f;
    limiter.lastFlashSeconds = nowSeconds;
    return scaled;
}

}  // namespace ls::effects
