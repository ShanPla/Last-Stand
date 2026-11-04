#include "components/Audio/Layers/Layers.h"

#include <algorithm>
#include <cmath>

namespace ls::audio::layers {

namespace {

// 0 below `low`, 1 above `high`, smooth in between. Stops layers popping in.
float ramp(float x, float low, float high) {
    const float t = std::clamp((x - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

constexpr float kMasterGain = 0.65f;

}  // namespace

Targets targetsFor(float intensity, bool boss) {
    const float i = std::clamp(intensity, 0.0f, 1.0f);
    Targets t;
    if (boss) {
        t.gain[music::kBossBass] = 1.0f;
        t.gain[music::kBossDrums] = 1.0f;
        t.gain[music::kBossLead] = 1.0f;
        t.gain[music::kBossPad] = 1.0f;
        return t;
    }
    t.gain[music::kPad] = 1.0f - 0.25f * ramp(i, 0.5f, 1.0f);
    t.gain[music::kBass] = 0.55f + 0.45f * ramp(i, 0.1f, 0.3f);
    t.gain[music::kDrumsLight] = ramp(i, 0.15f, 0.3f);
    t.gain[music::kArp] = ramp(i, 0.28f, 0.45f);
    t.gain[music::kDrumsFull] = ramp(i, 0.5f, 0.65f);
    t.gain[music::kLead] = ramp(i, 0.7f, 0.85f);
    return t;
}

void StemMixer::snapToTargets() {
    for (int s = 0; s < music::kStemCount; ++s) current_[s] = targets_.gain[s];
}

void StemMixer::mix(int16_t* out, int frames) {
    if (frames > kMaxBlock) frames = kMaxBlock;
    if (frames <= 0) return;
    std::fill(accum_, accum_ + frames, 0.0f);
    const size_t mainLength = static_cast<size_t>(music::stemFrames(music::kPad));
    const float maxStep = kFadePerSecond * static_cast<float>(frames) / static_cast<float>(music::kSampleRate);

    if (bank_) {
        for (int s = 0; s < music::kStemCount; ++s) {
            const std::vector<int16_t>& data = bank_->stems[s];
            const float from = current_[s];
            // Move toward the target a little per block. Inside the block the gain
            // ramps smoothly, so volume changes never click.
            const float to = from + std::clamp(targets_.gain[s] - from, -maxStep, maxStep);
            current_[s] = to;
            if ((from == 0.0f && to == 0.0f) || data.empty()) continue;
            const size_t length = data.size();
            size_t p = position_ % length;
            const float slope = (to - from) / static_cast<float>(frames);
            float g = from;
            int done = 0;
            while (done < frames) {
                const int segment = static_cast<int>(std::min<size_t>(static_cast<size_t>(frames - done), length - p));
                for (int i = 0; i < segment; ++i) {
                    accum_[done + i] += static_cast<float>(data[p + static_cast<size_t>(i)]) * g;
                    g += slope;
                }
                done += segment;
                p = 0;
            }
        }
    }
    for (int i = 0; i < frames; ++i) {
        const float x = accum_[i] * (kMasterGain / 32768.0f);
        out[i] = static_cast<int16_t>(std::lround(synth::softClip(x) * 32767.0f));
    }
    position_ = (position_ + static_cast<size_t>(frames)) % mainLength;
}

}  // namespace ls::audio::layers
