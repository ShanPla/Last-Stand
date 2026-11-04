#pragma once

#include "components/Audio/Audio.h"
#include "components/Audio/Sfx/Sfx.h"
#include "components/Audio/Synth/Synth.h"
#include "core/GameState.h"

// Decides which sound a simulation event should make, and whether to make it at
// all. Thousands of events can arrive each second, so each sound has a minimum
// gap between starts. This keeps the mix clean and the CPU cost low. It has no
// raylib calls, so tests can feed it events and check the answers.
namespace ls::audio::router {

struct PlayRequest {
    sfx::SfxId id = sfx::SfxId::Count;
    float volume = 1.0f;  // 0 to 1, before the player's effects volume.
    float pitch = 1.0f;   // 1 is the recorded pitch.
};

// Remembers when each sound last started, and refuses starts that come too soon.
class Throttle {
public:
    Throttle() { reset(); }
    void reset() {
        for (float& t : last_) t = -1000.0f;
    }
    bool allow(sfx::SfxId slot, float now, float minInterval) {
        float& last = last_[static_cast<int>(slot)];
        if (now - last < minInterval) return false;
        last = now;
        return true;
    }

private:
    float last_[sfx::kSfxCount];
};

// Gems picked up close together play a rising tune. A short gap resets it.
class GemChain {
public:
    static constexpr float kResetGap = 0.55f;  // Seconds without a gem before the pitch drops back.
    int next(float now) {
        count_ = (now - last_ > kResetGap) ? 0 : count_ + 1;
        if (count_ >= sfx::kGemSteps) count_ = sfx::kGemSteps - 1;
        last_ = now;
        return count_;
    }
    void reset() {
        count_ = 0;
        last_ = -1000.0f;
    }

private:
    int count_ = 0;
    float last_ = -1000.0f;
};

class Router {
public:
    explicit Router(uint32_t seed = 0x51f0u) : noise_(seed) {}

    // True when the event should make a sound now. `now` is seconds on a clock
    // that only moves forward.
    bool route(const SimEvent& event, float now, PlayRequest& out);
    bool routeUi(UiSound sound, float now, PlayRequest& out);
    void reset();

private:
    bool start(sfx::SfxId id, sfx::SfxId slot, float now, float volume, float pitchVariation, PlayRequest& out);

    Throttle throttle_;
    GemChain gems_;
    synth::Noise noise_;  // Pitch variation only. Never the simulation's generator.
};

}  // namespace ls::audio::router
