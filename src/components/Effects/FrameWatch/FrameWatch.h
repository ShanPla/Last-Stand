#pragma once

// Decides when a device is really too slow for the extras. A long frame alone can't prove
// that, because a screen may simply refresh slowly: a 30 Hz monitor or a phone in Low Power
// Mode gives 33 ms frames with the graphics chip mostly idle, and cutting effects would
// lose the look for nothing. So this first learns how fast frames can come on this device
// (the menus are light, so they show it), and only frames well past that count as slow.
namespace ls::effects {

class FrameWatch {
public:
    // Call once for every frame, on every screen, with its real length in seconds. fighting
    // says whether a run is being played right now. Returns true when the device has been
    // too slow for long enough.
    bool update(float frameSeconds, bool fighting) {
        if (frameSeconds > kStallSeconds) {
            // A hidden tab, a stall or the page loading. It says nothing about the device, and
            // a sum that spans it wouldn't be a steady run of slow frames.
            slowSeconds_ = 0.0f;
            filled_ = 0;
            return false;
        }
        learnFastest(frameSeconds);
        average_ += (frameSeconds - average_) * kSmoothing;
        if (!fighting) {
            slowSeconds_ = 0.0f;
            return false;
        }
        slowSeconds_ = average_ > slowLimit() ? slowSeconds_ + frameSeconds : 0.0f;
        return slowSeconds_ > kSlowForSeconds;
    }

    float slowLimit() const {
        const float forgiving = fastest_ * kRoomOverFastest;
        if (forgiving < kNormalLimit) return kNormalLimit;
        return forgiving > kMostForgiven ? kMostForgiven : forgiving;
    }

private:
    // Every frame in a short run has to be this quick to count. One short frame proves nothing,
    // as a browser sometimes delivers two close together after a late one.
    void learnFastest(float frameSeconds) {
        recent_[next_] = frameSeconds;
        next_ = (next_ + 1) % kSteadyFrames;
        if (filled_ < kSteadyFrames) filled_++;
        if (filled_ < kSteadyFrames) return;
        float slowestRecent = 0.0f;
        for (const float seconds : recent_) {
            if (seconds > slowestRecent) slowestRecent = seconds;
        }
        if (slowestRecent < fastest_) fastest_ = slowestRecent;
    }

    static constexpr float kStallSeconds = 0.25f;
    static constexpr float kSmoothing = 0.05f;
    static constexpr int kSteadyFrames = 8;
    static constexpr float kSlowForSeconds = 4.0f;
    // A frame rate that started high and fell to worse than about 38 a second is slow.
    static constexpr float kNormalLimit = 0.026f;
    // The room a slow screen gets, so its own steady rate is never called slow.
    static constexpr float kRoomOverFastest = 1.15f;
    // No screen caps lower than 30 a second, so past about 26 a second it is a slow device,
    // however slowly it has always run.
    static constexpr float kMostForgiven = 0.038f;

    float recent_[kSteadyFrames] = {};
    int next_ = 0;
    int filled_ = 0;
    float fastest_ = 1.0f;  // Nothing learned yet.
    float average_ = 1.0f / 60.0f;
    float slowSeconds_ = 0.0f;
};

}  // namespace ls::effects
