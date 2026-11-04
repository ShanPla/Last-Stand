#pragma once

// How strongly a hit figure mixes toward the flash color. One zombie hit should be
// obvious, but a whole crowd under fire must not turn into a white blob, so the mix
// gets weaker as more of the screen is flashing, and only some of the crowd flashes.
namespace ls::effects {

inline constexpr int kMostFlashingAtOnce = 36;
inline constexpr float kFlashLone = 0.55f;
inline constexpr float kFlashCrowd = 0.26f;
inline constexpr int kFlashLoneCount = 6;    // Up to this many, each flash is at full strength.
inline constexpr int kFlashCrowdCount = 60;  // From this many, each flash is at crowd strength.
inline constexpr float kFlashReducedMost = 0.30f;  // Reduced motion: a gentle tint, never a strobe.

inline float crowdFlashStrength(int flashingNow) {
    if (flashingNow <= kFlashLoneCount) return kFlashLone;
    if (flashingNow >= kFlashCrowdCount) return kFlashCrowd;
    const float t = static_cast<float>(flashingNow - kFlashLoneCount) / static_cast<float>(kFlashCrowdCount - kFlashLoneCount);
    return kFlashLone + (kFlashCrowd - kFlashLone) * t;
}

// Which share of the crowd may flash: every Nth zombie by id. Using the id keeps the
// same ones picked from frame to frame, so the crowd doesn't sparkle.
inline int flashStride(int flashingNow) {
    if (flashingNow <= kMostFlashingAtOnce) return 1;
    return (flashingNow + kMostFlashingAtOnce - 1) / kMostFlashingAtOnce;
}

// secondsLeft counts down from fullSeconds. The square makes it snap on and fall away
// fast, so the flash is brief even though the simulation's timer runs a little longer.
inline float hitFlashAmount(float secondsLeft, float fullSeconds, float strength, float intensity, bool reducedMotion) {
    float t = secondsLeft / fullSeconds;
    if (t > 1.0f) t = 1.0f;
    if (t < 0.0f) t = 0.0f;
    float amount = strength * t * t;
    if (reducedMotion && amount > kFlashReducedMost) amount = kFlashReducedMost;
    return amount * intensity;
}

}  // namespace ls::effects
