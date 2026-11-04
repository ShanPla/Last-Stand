#pragma once

// A tiny freeze on big moments. The app loop asks how much of each frame's time the
// simulation may have, and the rest is simply not given to it. Steps still run in the
// same order with the same input, only a little later, so a run stays deterministic.
namespace ls::effects {

inline constexpr float kHitPauseLongest = 0.12f;  // Short enough that it never feels like lost input.
inline constexpr float kHitPauseCooldown = 0.7f;  // Stops several big moments adding up to a stutter.

class HitPause {
public:
    // Starts a pause unless one is running or has only just ended. Pauses never stack.
    bool trigger(float seconds) {
        if (left_ > 0.0f || cooldown_ > 0.0f) return false;
        left_ = seconds < kHitPauseLongest ? seconds : kHitPauseLongest;
        return true;
    }

    // Takes this frame's real time, and returns the part the simulation may have.
    float filter(float dt) {
        if (left_ > 0.0f) {
            const float taken = dt < left_ ? dt : left_;
            left_ -= taken;
            if (left_ <= 0.0f) cooldown_ = kHitPauseCooldown;
            return dt - taken;
        }
        if (cooldown_ > 0.0f) cooldown_ -= dt;
        return dt;
    }

    bool active() const { return left_ > 0.0f; }

    void clear() {
        left_ = 0.0f;
        cooldown_ = 0.0f;
    }

private:
    float left_ = 0.0f;
    float cooldown_ = 0.0f;
};

}  // namespace ls::effects
