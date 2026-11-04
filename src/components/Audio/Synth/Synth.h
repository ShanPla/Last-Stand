#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Small building blocks for generating sound: noise, oscillators, filters and
// helpers to finish a buffer. Nothing here touches raylib or the simulation, so
// every function gives the same result on every run and can be unit tested.
namespace ls::audio::synth {

inline constexpr int kSampleRate = 22050;  // Low on purpose: it keeps startup rendering fast.
inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTwoPi = 6.28318530717959f;

using Samples = std::vector<float>;

// Audio uses its own random numbers, never the simulation's, so sound can never
// change how a run plays out. A fixed seed makes each sound identical every time.
class Noise {
public:
    explicit Noise(uint32_t seed = 1u) : state_(seed ? seed : 1u) {}
    uint32_t nextU32() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    float next01() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }
    float next() { return next01() * 2.0f - 1.0f; }  // White noise in [-1, 1).
    float range(float low, float high) { return low + (high - low) * next01(); }

private:
    uint32_t state_;
};

// Frequency of a MIDI note number (69 is A4 at 440 Hz).
inline float midiToHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

// Exponential decay that reaches about 1/1000 after `seconds`.
inline float decay(float t, float seconds) { return std::exp(-6.9f * t / seconds); }

// Gentle saturation. Keeps loud peaks round instead of hard clipped.
inline float softClip(float x) {
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) * (4.0f / 27.0f);
}

// Phase is 0 to 1. dt is the phase step per sample, used to smooth the jump of
// saw and square waves so they do not alias into harsh whine.
float polyBlep(float phase, float dt);
inline float saw(float phase, float dt) { return 2.0f * phase - 1.0f - polyBlep(phase, dt); }
float square(float phase, float dt, float width = 0.5f);
inline float sine(float phase) { return std::sin(kTwoPi * phase); }
inline float triangle(float phase) { return 4.0f * std::fabs(phase - 0.5f) - 1.0f; }

inline float advance(float& phase, float step) {
    phase += step;
    if (phase >= 1.0f) phase -= 1.0f;
    return phase;
}

// Smooth low-pass filter with one pole. Cheap, so it is used for most tone shaping.
struct OnePole {
    float a = 1.0f;
    float z = 0.0f;
    void setCutoff(float hz);
    float low(float x) {
        z += a * (x - z);
        return z;
    }
    float high(float x) { return x - low(x); }
};

// Two-pole filter for band and resonant shapes.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void lowpass(float hz, float q = 0.707f);
    void highpass(float hz, float q = 0.707f);
    void bandpass(float hz, float q = 1.0f);
    float process(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

inline size_t frames(float seconds) { return static_cast<size_t>(seconds * static_cast<float>(kSampleRate) + 0.5f); }

// Builds a buffer by calling fn(t, i) for every sample, t being seconds from the start.
template <typename Fn>
Samples render(float seconds, Fn&& fn) {
    Samples out(frames(seconds));
    const float inv = 1.0f / static_cast<float>(kSampleRate);
    for (size_t i = 0; i < out.size(); ++i) out[i] = fn(static_cast<float>(i) * inv, i);
    return out;
}

// Adds src into dst starting at a time offset. Anything past the end is dropped.
void mixInto(Samples& dst, const Samples& src, float atSeconds, float gain);

// Repeats the signal a few times, quieter and later each time, for a roomy tail.
void addEchoes(Samples& buffer, float delaySeconds, float feedback, int count);

struct Stats {
    float peak = 0.0f;
    float rms = 0.0f;
    float dc = 0.0f;  // Average value. Should be near 0.
    float seconds = 0.0f;
};
Stats measure(const Samples& buffer);

// Final pass for every sound: removes DC offset, fades the first and last few
// milliseconds so nothing clicks, then scales to the wanted peak.
void finish(Samples& buffer, float targetPeak, float fadeOutSeconds = 0.02f);

// Keeps the final mix from clipping when many sounds overlap. Gain drops at
// once when the signal gets too loud and recovers slowly, so it works like a
// ducking compressor. It runs on interleaved float frames.
class Limiter {
public:
    Limiter(float threshold = 0.9f, float releaseSeconds = 0.12f, int sampleRate = 48000);
    void process(float* interleaved, unsigned frameCount, unsigned channels);
    float gain() const { return gain_; }

private:
    float threshold_;
    float release_;
    float gain_ = 1.0f;
};

}  // namespace ls::audio::synth
