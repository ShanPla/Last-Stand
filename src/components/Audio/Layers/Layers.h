#pragma once

#include "components/Audio/Music/Music.h"

#include <cstddef>
#include <cstdint>

// Turns the game's intensity into a volume for each music layer, and mixes the
// layers into one stream. Pure code with no raylib, so it can be tested.
namespace ls::audio::layers {

struct Targets {
    float gain[music::kStemCount] = {};
};

// Intensity is 0 to 1. Layers come in one after another as it rises:
//   calm      pad and a soft bass
//   building  drums (kick and hats) and the arpeggio
//   intense   snare and claps, then the lead melody
// During the boss fight the main layers fade out and the heavy boss layers fade in.
Targets targetsFor(float intensity, bool boss);

class StemMixer {
public:
    static constexpr int kMaxBlock = 4096;
    static constexpr float kFadePerSecond = 0.6f;  // A layer takes a little under two seconds to fade fully in or out.

    void setBank(const music::Bank* bank) { bank_ = bank; }
    void setTargets(const Targets& targets) { targets_ = targets; }
    // Starts every layer at its target at once. Used for the first block only.
    void snapToTargets();

    // Writes `frames` mono samples (at most kMaxBlock) and moves the song forward.
    void mix(int16_t* out, int frames);

    size_t position() const { return position_; }
    void setPosition(size_t position) { position_ = position % static_cast<size_t>(music::stemFrames(music::kPad)); }
    float gain(int stem) const { return current_[stem]; }

private:
    const music::Bank* bank_ = nullptr;
    Targets targets_;
    float current_[music::kStemCount] = {};
    size_t position_ = 0;
    float accum_[kMaxBlock];
};

}  // namespace ls::audio::layers
