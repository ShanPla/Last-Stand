#include "components/Audio/Synth/Synth.h"

#include <algorithm>

namespace ls::audio::synth {

float polyBlep(float phase, float dt) {
    if (phase < dt) {
        const float t = phase / dt;
        return t + t - t * t - 1.0f;
    }
    if (phase > 1.0f - dt) {
        const float t = (phase - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float square(float phase, float dt, float width) {
    float shifted = phase + (1.0f - width);
    if (shifted >= 1.0f) shifted -= 1.0f;
    const float value = phase < width ? 1.0f : -1.0f;
    return value + polyBlep(phase, dt) - polyBlep(shifted, dt);
}

void OnePole::setCutoff(float hz) {
    const float clamped = std::clamp(hz, 5.0f, static_cast<float>(kSampleRate) * 0.45f);
    a = 1.0f - std::exp(-kTwoPi * clamped / static_cast<float>(kSampleRate));
}

namespace {

// Shared by the three Biquad shapes. Formulas from the common audio EQ cookbook.
void setBiquad(Biquad& f, float hz, float q, int shape) {
    const float w = kTwoPi * std::clamp(hz, 20.0f, static_cast<float>(kSampleRate) * 0.45f) / static_cast<float>(kSampleRate);
    const float cw = std::cos(w);
    const float alpha = std::sin(w) / (2.0f * std::max(q, 0.1f));
    float b0, b1, b2;
    if (shape == 0) {  // Low-pass.
        b0 = (1.0f - cw) * 0.5f;
        b1 = 1.0f - cw;
        b2 = b0;
    } else if (shape == 1) {  // High-pass.
        b0 = (1.0f + cw) * 0.5f;
        b1 = -(1.0f + cw);
        b2 = b0;
    } else {  // Band-pass with constant peak gain.
        b0 = alpha;
        b1 = 0.0f;
        b2 = -alpha;
    }
    const float a0 = 1.0f + alpha;
    f.b0 = b0 / a0;
    f.b1 = b1 / a0;
    f.b2 = b2 / a0;
    f.a1 = -2.0f * cw / a0;
    f.a2 = (1.0f - alpha) / a0;
}

}  // namespace

void Biquad::lowpass(float hz, float q) { setBiquad(*this, hz, q, 0); }
void Biquad::highpass(float hz, float q) { setBiquad(*this, hz, q, 1); }
void Biquad::bandpass(float hz, float q) { setBiquad(*this, hz, q, 2); }

void mixInto(Samples& dst, const Samples& src, float atSeconds, float gain) {
    const size_t offset = frames(atSeconds);
    for (size_t i = 0; i < src.size() && offset + i < dst.size(); ++i) dst[offset + i] += src[i] * gain;
}

void addEchoes(Samples& buffer, float delaySeconds, float feedback, int count) {
    const size_t delay = frames(delaySeconds);
    if (delay == 0) return;
    // Walk backward so each echo reads samples that are not yet changed.
    for (int k = count; k >= 1; --k) {
        const float g = std::pow(feedback, static_cast<float>(k));
        for (size_t i = buffer.size(); i-- > delay * static_cast<size_t>(k);) buffer[i] += buffer[i - delay * static_cast<size_t>(k)] * g;
    }
}

Stats measure(const Samples& buffer) {
    Stats s;
    double sum = 0.0, sq = 0.0;
    for (float v : buffer) {
        s.peak = std::max(s.peak, std::fabs(v));
        sum += v;
        sq += static_cast<double>(v) * v;
    }
    if (!buffer.empty()) {
        s.dc = static_cast<float>(sum / static_cast<double>(buffer.size()));
        s.rms = static_cast<float>(std::sqrt(sq / static_cast<double>(buffer.size())));
    }
    s.seconds = static_cast<float>(buffer.size()) / static_cast<float>(kSampleRate);
    return s;
}

void finish(Samples& buffer, float targetPeak, float fadeOutSeconds) {
    if (buffer.empty()) return;
    double sum = 0.0;
    for (float v : buffer) sum += v;
    const float mean = static_cast<float>(sum / static_cast<double>(buffer.size()));
    const size_t fadeIn = std::min(buffer.size() / 2, frames(0.002f));
    const size_t fadeOut = std::min(buffer.size() / 2, std::max<size_t>(frames(fadeOutSeconds), 1));
    // Fade first and measure after, so the peak that is scaled is the peak that is heard.
    float peak = 0.0f;
    for (size_t i = 0; i < buffer.size(); ++i) {
        float g = 1.0f;
        if (i < fadeIn) g *= static_cast<float>(i) / static_cast<float>(fadeIn);
        const size_t fromEnd = buffer.size() - 1 - i;
        if (fromEnd < fadeOut) g *= static_cast<float>(fromEnd) / static_cast<float>(fadeOut);
        buffer[i] = (buffer[i] - mean) * g;
        peak = std::max(peak, std::fabs(buffer[i]));
    }
    const float scale = peak > 1e-9f ? targetPeak / peak : 0.0f;
    for (float& v : buffer) v *= scale;
}

Limiter::Limiter(float threshold, float releaseSeconds, int sampleRate)
    : threshold_(threshold), release_(1.0f - std::exp(-1.0f / (releaseSeconds * static_cast<float>(sampleRate)))) {}

void Limiter::process(float* interleaved, unsigned frameCount, unsigned channels) {
    for (unsigned f = 0; f < frameCount; ++f) {
        float* frame = interleaved + static_cast<size_t>(f) * channels;
        float peak = 0.0f;
        for (unsigned c = 0; c < channels; ++c) peak = std::max(peak, std::fabs(frame[c]));
        const float wanted = peak > threshold_ ? threshold_ / peak : 1.0f;
        // Drop at once so the peak never gets through, then recover slowly.
        gain_ = std::min(wanted, gain_ + (1.0f - gain_) * release_);
        for (unsigned c = 0; c < channels; ++c) frame[c] *= gain_;
    }
}

}  // namespace ls::audio::synth
