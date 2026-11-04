#pragma once

#include <algorithm>
#include <cmath>

// Screen shake as "trauma": big events add to one number that drains away, and the
// shake grows with its square, so small events stay subtle and big ones really hit.
// The result is always a whole number of art pixels, added to the camera's whole-pixel
// position, so pixels never turn uneven and nothing shimmers against the ground.
namespace ls::effects {

class ScreenShake {
public:
    static constexpr float kMaxPixels = 5.0f;         // At full trauma and full setting.
    static constexpr float kDrainPerSecond = 2.2f;
    static constexpr float kSampleSeconds = 1.0f / 30.0f;  // The same speed on any display.

    void add(float trauma) { trauma_ = std::min(1.0f, trauma_ + trauma); }

    // scale is the player's setting: 0 to 1, or 0 with reduced motion. randomX and
    // randomY are from -1 to 1, and are passed in so a test can choose them.
    void update(float dt, float scale, float randomX, float randomY) {
        sampleLeft_ -= dt;
        if (sampleLeft_ <= 0.0f) {
            sampleLeft_ = kSampleSeconds;
            const float size = trauma_ * trauma_ * kMaxPixels * scale;
            x_ = static_cast<int>(std::lround(randomX * size));
            y_ = static_cast<int>(std::lround(randomY * size));
        }
        trauma_ = std::max(0.0f, trauma_ - kDrainPerSecond * dt);
        if (trauma_ <= 0.0f) x_ = y_ = 0;
    }

    // Takes a new sample on the next update. Call when the setting changes, so that the
    // new size shows at once rather than after the current sample ends.
    void resample() { sampleLeft_ = 0.0f; }

    void clear() {
        trauma_ = 0.0f;
        x_ = y_ = 0;
        sampleLeft_ = 0.0f;
    }

    int x() const { return x_; }
    int y() const { return y_; }
    float trauma() const { return trauma_; }

private:
    float trauma_ = 0.0f;
    float sampleLeft_ = 0.0f;
    int x_ = 0;
    int y_ = 0;
};

}  // namespace ls::effects
