#include "components/Audio/Music/Music.h"

#include <algorithm>

namespace ls::audio::music {

using namespace synth;

namespace {

constexpr float kSr = static_cast<float>(kSampleRate);
constexpr double kStepFrames = static_cast<double>(kBarFrames) / 16.0;  // One sixteenth note.

// Where notes are written. Anything that runs past the end wraps to the start,
// which is what makes the loop join with no click: the last notes ring out into
// the first bar, exactly as they would if the song kept playing.
struct Ctx {
    float* buf;
    size_t n;
};

void put(Ctx& c, size_t index, float value) {
    if (index >= c.n) index -= c.n;  // No note is longer than the loop, so once is enough.
    c.buf[index] += value;
}

size_t stepFrame(int bar, float step) { return static_cast<size_t>((static_cast<double>(bar) * 16.0 + static_cast<double>(step)) * kStepFrames + 0.5); }
float stepSeconds(float steps) { return steps * static_cast<float>(kStepFrames) / kSr; }
float releaseGate(float t, float hold, float release) { return t < hold ? 1.0f : std::max(0.0f, 1.0f - (t - hold) / release); }

// --- Instruments -------------------------------------------------------------

void bassNote(Ctx& c, size_t start, float hz, float hold, float vel, float drive, float cutoff) {
    float p = 0.0f, sub = 0.0f;
    OnePole lp;
    const float dt = hz / kSr;
    const size_t count = frames(hold + 0.08f);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSr;
        if ((i & 15) == 0) lp.setCutoff(180.0f + cutoff * std::exp(-t / 0.12f));
        const float osc = 1.0f * saw(p, dt) + 0.35f * triangle(sub);
        advance(p, dt);
        advance(sub, dt * 0.5f);
        put(c, start + i, softClip(lp.low(osc) * drive) * std::min(1.0f, t / 0.004f) * releaseGate(t, hold, 0.08f) * vel);
    }
}

// A slow, soft saw voice. A new chord fades in while the old one fades out.
void padNote(Ctx& c, size_t start, float hz, float hold, float vel, float lfoOffset) {
    float p = lfoOffset * 0.37f;
    p -= std::floor(p);
    OnePole lp;
    const float dt = hz / kSr;
    const size_t count = frames(hold + 0.8f);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSr;
        if ((i & 31) == 0) lp.setCutoff(750.0f + 350.0f * std::sin(kTwoPi * 0.15f * t + lfoOffset));
        const float env = std::min(1.0f, t / 0.5f) * releaseGate(t, hold, 0.8f);
        put(c, start + i, lp.low(saw(p, dt)) * env * vel);
        advance(p, dt);
    }
}

void arpNote(Ctx& c, size_t start, float hz, float vel) {
    float p1 = 0.0f, p2 = 0.3f;
    OnePole lp;
    const float dt1 = hz / kSr, dt2 = hz * 1.005f / kSr;
    const size_t count = frames(0.3f);
    const float fall = std::exp(-6.9f / (0.22f * kSr));  // Per-sample decay step, so no exp call per sample.
    float level = vel;
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSr;
        if ((i & 15) == 0) lp.setCutoff(600.0f + 3500.0f * std::exp(-t / 0.09f));
        const float osc = 0.5f * saw(p1, dt1) + 0.4f * square(p2, dt2, 0.5f);
        advance(p1, dt1);
        advance(p2, dt2);
        put(c, start + i, lp.low(osc) * std::min(1.0f, t / 0.002f) * level);
        level *= fall;
    }
}

void leadNote(Ctx& c, size_t start, float hz, float hold, float vel, float drive, float cutoff) {
    float p1 = 0.0f, p2 = 0.0f;
    OnePole lp;
    lp.setCutoff(cutoff);
    const size_t count = frames(hold + 0.14f);
    float dt = hz / kSr;
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSr;
        // Vibrato fades in late, as a singer or a synth wheel would add it. It moves slowly,
        // so updating it every few samples is enough.
        if ((i & 7) == 0) dt = hz * (1.0f + 0.004f * std::sin(kTwoPi * 5.5f * t) * std::min(1.0f, t / 0.25f)) / kSr;
        const float osc = 0.6f * saw(p1, dt) + 0.4f * square(p2, dt, 0.35f);
        advance(p1, dt);
        advance(p2, dt);
        put(c, start + i, softClip(lp.low(osc) * drive) * std::min(1.0f, t / 0.012f) * releaseGate(t, hold, 0.14f) * vel);
    }
}

// --- Boss instruments ----------------------------------------------------------
// The boss layers aim for deep and dark without needing speakers that can play sub-bass.
// Each low note is pushed through soft saturation, which creates harmonics between about
// 150 and 500 Hz that small speakers and phones can reproduce, so the weight is still heard.

// A growling bass: two detuned saws beat against each other slowly, over a sub sine.
void growlNote(Ctx& c, size_t start, float hz, float hold, float vel) {
    float p1 = 0.0f, p2 = 0.37f, sub = 0.0f;
    OnePole lp, tone;
    tone.setCutoff(1100.0f);  // Keeps the top end dark after the saturation.
    Biquad mids;
    mids.bandpass(260.0f, 0.7f);  // Lifts the 150 to 500 Hz range that small speakers can play.
    const float dt1 = hz * 0.992f / kSr, dt2 = hz * 1.008f / kSr, dtSub = hz / kSr;
    const size_t count = frames(hold + 0.2f);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSr;
        if ((i & 31) == 0) lp.setCutoff(700.0f + 350.0f * std::sin(kTwoPi * 0.45f * t) + 250.0f * std::exp(-t / 0.3f));
        const float osc = 0.9f * saw(p1, dt1) + 0.9f * saw(p2, dt2) + 0.3f * sine(sub);
        advance(p1, dt1);
        advance(p2, dt2);
        advance(sub, dtSub);
        float grown = tone.low(softClip(lp.low(osc) * 4.5f));
        grown += 1.6f * mids.process(grown);
        put(c, start + i, grown * std::min(1.0f, t / 0.02f) * releaseGate(t, hold, 0.2f) * vel);
    }
}

// A dark choir-like drone: detuned saws, a low-pass that drifts slowly, and a formant
// band that gives it an open vowel sound.
void darkPadNote(Ctx& c, size_t start, float hz, float hold, float vel, float lfoOffset) {
    float p1 = lfoOffset * 0.31f, p2 = lfoOffset * 0.53f + 0.2f;
    p1 -= std::floor(p1);
    p2 -= std::floor(p2);
    OnePole lp;
    Biquad vowel;
    vowel.bandpass(480.0f, 1.6f);
    const float dt1 = hz * 0.996f / kSr, dt2 = hz * 1.004f / kSr;
    const size_t count = frames(hold + 1.2f);
    for (size_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / kSr;
        if ((i & 63) == 0) lp.setCutoff(520.0f + 220.0f * std::sin(kTwoPi * 0.11f * t + lfoOffset));
        const float osc = saw(p1, dt1) + saw(p2, dt2);
        advance(p1, dt1);
        advance(p2, dt2);
        const float body = softClip(lp.low(osc) * 1.8f);
        const float env = std::min(1.0f, t / 1.2f) * releaseGate(t, hold, 1.2f);
        put(c, start + i, (body + 0.9f * vowel.process(body)) * env * vel);
    }
}

// Drum hits sound the same every time, as on a real drum machine, so each one is
// rendered once and then copied into the song. That is much cheaper than
// synthesizing hundreds of identical hits.
// A sine that glides from f0 down to f1, evaluated at time t.
float glideSineCycles(float t, float f0, float f1, float tau) {
    const float cycles = f1 * t + (f0 - f1) * tau * (1.0f - std::exp(-t / tau));
    return std::sin(kTwoPi * (cycles - std::floor(cycles)));
}

void addWave(Ctx& c, size_t start, const Samples& wave, float gain) {
    for (size_t i = 0; i < wave.size(); ++i) put(c, start + i, wave[i] * gain);
}

const Samples& kickWave() {
    static const Samples wave = [] {
        Noise n(101);
        return render(0.38f, [&](float t, size_t) {
            const float cycles = 45.0f * t + (150.0f - 45.0f) * 0.035f * (1.0f - std::exp(-t / 0.035f));
            const float body = std::sin(kTwoPi * (cycles - std::floor(cycles))) * decay(t, 0.26f);
            const float click = n.next() * decay(t, 0.008f) * 0.5f;
            // Saturation adds upper harmonics so the kick still reads on small speakers.
            return softClip(body * 1.8f) * 0.8f + click;
        });
    }();
    return wave;
}

const Samples& snareWave() {
    static const Samples wave = [] {
        Noise n(102);
        Biquad bp;
        bp.bandpass(1900.0f, 0.7f);
        return render(0.3f, [&](float t, size_t) {
            const float cycles = 150.0f * t + 70.0f * 0.02f * (1.0f - std::exp(-t / 0.02f));
            const float tone = std::sin(kTwoPi * (cycles - std::floor(cycles))) * decay(t, 0.1f) * 0.6f;
            return bp.process(n.next()) * decay(t, 0.14f) * 1.5f + tone;
        });
    }();
    return wave;
}

// Three quick noise bursts, like several hands clapping a little apart.
const Samples& clapWave() {
    static const Samples wave = [] {
        Noise n(103);
        Biquad bp;
        bp.bandpass(1300.0f, 0.8f);
        return render(0.22f, [&](float t, size_t) {
            float burst = decay(std::fmod(t, 0.011f), 0.01f);
            if (t >= 0.033f) burst = decay(t - 0.033f, 0.12f);
            return bp.process(n.next()) * burst * 1.4f;
        });
    }();
    return wave;
}

enum class HatKind { Tick, Closed, Open };
const Samples& hatWave(HatKind kind) {
    static const Samples waves[3] = {
        [] {
            Noise n(104);
            OnePole hp;
            hp.setCutoff(6500.0f);
            return render(0.036f, [&](float t, size_t) { return hp.high(n.next()) * decay(t, 0.018f); });
        }(),
        [] {
            Noise n(105);
            OnePole hp;
            hp.setCutoff(6500.0f);
            return render(0.044f, [&](float t, size_t) { return hp.high(n.next()) * decay(t, 0.022f); });
        }(),
        [] {
            Noise n(106);
            OnePole hp;
            hp.setCutoff(6500.0f);
            return render(0.22f, [&](float t, size_t) { return hp.high(n.next()) * decay(t, 0.11f); });
        }(),
    };
    return waves[static_cast<int>(kind)];
}

const Samples& crashWave() {
    static const Samples wave = [] {
        Noise n(107);
        OnePole hp;
        hp.setCutoff(3500.0f);
        return render(1.4f, [&](float t, size_t) { return hp.high(n.next()) * std::min(1.0f, t / 0.003f) * decay(t, 1.0f) * 0.45f; });
    }();
    return wave;
}

Samples makeTom(float hz) {
    return render(0.3f, [hz](float t, size_t) {
        const float f = hz * 0.5f;
        const float cycles = f * t + f * 0.05f * (1.0f - std::exp(-t / 0.05f));
        return std::sin(kTwoPi * (cycles - std::floor(cycles))) * decay(t, 0.2f);
    });
}

const Samples& tomWave(int index) {
    static const Samples waves[4] = {makeTom(200.0f), makeTom(170.0f), makeTom(140.0f), makeTom(110.0f)};
    return waves[index];
}

// Boss drums are slow and heavy. The boom is saturated and has a mid "knock" on top,
// so the hit is still felt on speakers that cannot play the lowest notes.
const Samples& bossKickWave() {
    static const Samples wave = [] {
        Noise n(111);
        OnePole lp;
        lp.setCutoff(1400.0f);
        return render(0.55f, [&](float t, size_t) {
            const float cycles = 38.0f * t + (125.0f - 38.0f) * 0.05f * (1.0f - std::exp(-t / 0.05f));
            const float body = std::sin(kTwoPi * (cycles - std::floor(cycles))) * decay(t, 0.45f);
            const float knock = std::sin(kTwoPi * (210.0f * t - 70.0f * 0.03f * std::exp(-t / 0.03f))) * decay(t, 0.09f) * 0.5f;
            return softClip(body * 3.0f) * 0.5f + knock * 1.5f + lp.low(n.next()) * decay(t, 0.01f) * 0.35f;
        });
    }();
    return wave;
}

// A big dark snare: low noise, a falling tone, and a long boom underneath.
const Samples& bossSnareWave() {
    static const Samples wave = [] {
        Noise n(112);
        Biquad bp;
        bp.bandpass(750.0f, 0.6f);
        OnePole lp;
        lp.setCutoff(2600.0f);
        return render(0.7f, [&](float t, size_t) {
            const float tone = glideSineCycles(t, 190.0f, 78.0f, 0.04f);
            const float boom = std::sin(kTwoPi * (55.0f * t + 40.0f * 0.06f * (1.0f - std::exp(-t / 0.06f)))) * decay(t, 0.5f);
            const float noise = lp.low(bp.process(n.next())) * decay(t, 0.3f) * 2.4f;
            return noise + softClip(tone * 2.0f) * decay(t, 0.25f) * 0.9f + softClip(boom * 2.0f) * 0.6f;
        });
    }();
    return wave;
}

Samples makeDarkTom(float hz) {
    return render(0.6f, [hz](float t, size_t) {
        const float cycles = hz * 0.5f * t + hz * 0.5f * 0.05f * (1.0f - std::exp(-t / 0.05f));
        return softClip(std::sin(kTwoPi * (cycles - std::floor(cycles))) * 2.5f) * decay(t, 0.35f);
    });
}

const Samples& darkTomWave(int index) {
    static const Samples waves[3] = {makeDarkTom(330.0f), makeDarkTom(260.0f), makeDarkTom(200.0f)};
    return waves[index];
}

void bossKick(Ctx& c, size_t start, float vel) { addWave(c, start, bossKickWave(), vel); }
void bossSnare(Ctx& c, size_t start, float vel) { addWave(c, start, bossSnareWave(), vel); }
void darkTom(Ctx& c, size_t start, int index, float vel) { addWave(c, start, darkTomWave(index), vel); }
void kick(Ctx& c, size_t start, float vel) { addWave(c, start, kickWave(), vel); }
void snare(Ctx& c, size_t start, float vel) { addWave(c, start, snareWave(), vel); }
void clap(Ctx& c, size_t start, float vel) { addWave(c, start, clapWave(), vel); }
void hat(Ctx& c, size_t start, float vel, HatKind kind) { addWave(c, start, hatWave(kind), vel * 1.1f); }
void crash(Ctx& c, size_t start, float vel) { addWave(c, start, crashWave(), vel); }
void tom(Ctx& c, size_t start, int index, float vel) { addWave(c, start, tomWave(index), vel); }

// --- Song layout -------------------------------------------------------------

struct Chord {
    int root;  // MIDI note near the middle of the keyboard.
    bool major;
};
int third(const Chord& chord) { return chord.root + (chord.major ? 4 : 3); }
int fifth(const Chord& chord) { return chord.root + 7; }

// Four 8-bar sections. The tune moves around A minor, ends each pass on E major to pull back to the start.
constexpr Chord kChords[4][8] = {
    {{57, false}, {57, false}, {53, true}, {53, true}, {60, true}, {60, true}, {55, true}, {55, true}},   // A: Am F C G
    {{57, false}, {53, true}, {60, true}, {55, true}, {57, false}, {53, true}, {50, false}, {52, true}},  // B: one chord per bar
    {{53, true}, {53, true}, {55, true}, {55, true}, {57, false}, {57, false}, {52, true}, {52, true}},   // C: breakdown
    {{50, false}, {50, false}, {57, false}, {57, false}, {53, true}, {55, true}, {52, true}, {52, true}},  // D: building back up
};
const Chord& chordAt(int bar) { return kChords[bar / 8][bar % 8]; }

// Folds a bass note into one octave range so low chords stay audible on small speakers.
float bassMidi(int root) { return static_cast<float>(28 + ((root - 28) % 12)); }

struct Hit {
    float step;
    int offset;  // Semitones above the root.
    float len;   // In sixteenth notes.
    float vel;
};

constexpr Hit kBassDrive[] = {{0, 0, 1.7f, 1}, {2, 0, 1.7f, 0.85f}, {4, 0, 1.7f, 1}, {6, 12, 1.7f, 0.8f}, {8, 0, 1.7f, 1}, {10, 0, 1.7f, 0.85f}, {12, 0, 1.7f, 1}, {14, 12, 1.7f, 0.8f}};
constexpr Hit kBassGallop[] = {{0, 0, 1.2f, 1}, {2, 0, 0.9f, 0.8f}, {3, 12, 0.9f, 0.7f}, {4, 0, 1.2f, 1}, {6, 0, 0.9f, 0.8f}, {7, 12, 0.9f, 0.7f},
                               {8, 0, 1.2f, 1}, {10, 0, 0.9f, 0.8f}, {11, 12, 0.9f, 0.7f}, {12, 0, 1.2f, 1}, {14, 0, 0.9f, 0.8f}, {15, 7, 0.9f, 0.7f}};
constexpr Hit kBassLong[] = {{0, 0, 11.0f, 1}, {12, 7, 3.5f, 0.8f}};
constexpr Hit kBassBounce[] = {{0, 0, 3.0f, 1}, {4, 0, 1.8f, 0.9f}, {6, 12, 1.8f, 0.8f}, {8, 0, 3.0f, 1}, {12, 0, 1.8f, 0.9f}, {14, 7, 1.8f, 0.8f}};

struct Pattern {
    const Hit* hits;
    int count;
};
#define LS_PATTERN(array) {array, static_cast<int>(sizeof(array) / sizeof(array[0]))}
constexpr Pattern kBassPatterns[4] = {LS_PATTERN(kBassDrive), LS_PATTERN(kBassGallop), LS_PATTERN(kBassLong), LS_PATTERN(kBassBounce)};

// Hand written lead for section A. Each entry is step in the 8 bar section, MIDI note and length in sixteenths.
struct Melody {
    int step;
    int midi;
    float len;
};
constexpr Melody kLeadA[] = {
    {0, 69, 6},   {6, 72, 2},   {8, 76, 8},   {16, 74, 4},  {20, 72, 4},  {24, 69, 8},  {32, 72, 6},  {38, 69, 2},
    {40, 77, 8},  {48, 76, 4},  {52, 74, 4},  {56, 72, 8},  {64, 76, 6},  {70, 79, 2},  {72, 79, 8},  {80, 77, 4},
    {84, 76, 4},  {88, 74, 8},  {96, 74, 6},  {102, 71, 2}, {104, 79, 8}, {112, 76, 4}, {116, 74, 4}, {120, 71, 8},
};

// Lead for sections B and D is built from chord tones, so it always fits the harmony.
struct Motif {
    float step;
    int tone;  // 0 root, 1 third, 2 fifth.
    float len;
};
constexpr Motif kMotifB[] = {{0, 2, 3}, {4, 1, 2}, {6, 0, 2}, {8, 1, 3}, {12, 2, 2}, {14, 1, 2}};
constexpr Motif kMotifD[] = {{0, 0, 6}, {6, 1, 2}, {8, 2, 6}, {14, 1, 2}};

// Folds a pitch up into the lead's range.
int intoLeadRange(int midi) {
    while (midi < 65) midi += 12;
    while (midi > 83) midi -= 12;
    return midi;
}

// --- Boss layer data ---------------------------------------------------------

// A phrygian: the flat second (B flat) and the tritone are the sour notes that make it dread.
constexpr int kBossRoots[kBossBars] = {33, 33, 33, 33, 34, 34, 33, 33, 33, 33, 36, 36, 34, 34, 32, 40};

// The lead is a sparse, low motif: two long notes per bar, as semitones above the root.
// One row per bar within a group of four. The tritone (6) and flat second (1) give the dread.
struct Phrase {
    int step[2];
    int offset[2];
    float len[2];
};
constexpr Phrase kBossMotif[4] = {
    {{0, 12}, {0, 1}, {11.0f, 4.0f}},
    {{0, 8}, {0, 6}, {7.0f, 8.0f}},
    {{0, 8}, {0, 1}, {7.0f, 8.0f}},
    {{0, 8}, {7, 6}, {7.0f, 8.0f}},
};

// --- Rendering per stem and bar ------------------------------------------------

void renderPad(Ctx& c, int bar) {
    const Chord& chord = chordAt(bar);
    if (bar > 0 && chordAt(bar - 1).root == chord.root && chordAt(bar - 1).major == chord.major) return;  // Held from the last bar.
    int span = 1;
    while (bar + span < kMainBars && chordAt(bar + span).root == chord.root && chordAt(bar + span).major == chord.major) ++span;
    const float hold = static_cast<float>(span * kBarFrames) / kSr + 0.2f;
    const int notes[4] = {chord.root, third(chord), fifth(chord), chord.root + 12};
    for (int i = 0; i < 4; ++i) padNote(c, stepFrame(bar, 0), midiToHz(static_cast<float>(notes[i])), hold, 0.5f, static_cast<float>(i) * 1.7f + static_cast<float>(bar));
}

void renderBass(Ctx& c, int bar) {
    const Pattern& pattern = kBassPatterns[bar / 8];
    const float root = bassMidi(chordAt(bar).root);
    for (int i = 0; i < pattern.count; ++i) {
        const Hit& h = pattern.hits[i];
        const float len = stepSeconds(h.len);
        bassNote(c, stepFrame(bar, h.step), midiToHz(root + static_cast<float>(h.offset)), len, h.vel * 0.9f, 2.2f, 1800.0f);
    }
}

void renderDrumsLight(Ctx& c, int bar) {
    const int section = bar / 8;
    const bool half = section == 2;  // The breakdown thins out to a slow pulse.
    if (half) {
        kick(c, stepFrame(bar, 0), 1.0f);
        if (bar % 2 == 1) kick(c, stepFrame(bar, 10), 0.8f);
    } else {
        for (int beat = 0; beat < 4; ++beat) kick(c, stepFrame(bar, static_cast<float>(beat * 4)), beat == 0 ? 1.0f : 0.9f);
    }
    for (int s = 2; s < 16; s += 4) hat(c, stepFrame(bar, static_cast<float>(s)), 0.7f, HatKind::Closed);
    if (section == 1 || section == 3) {
        for (int s = 1; s < 16; s += 2) hat(c, stepFrame(bar, static_cast<float>(s)), 0.3f, HatKind::Tick);
    }
}

void renderDrumsFull(Ctx& c, int bar) {
    const int section = bar / 8;
    const int inSection = bar % 8;
    if (inSection == 0 && section != 2) crash(c, stepFrame(bar, 0), 0.8f);
    if (section == 2) {
        snare(c, stepFrame(bar, 12), 0.8f);
        if (inSection == 7) {
            for (int i = 0; i < 4; ++i) tom(c, stepFrame(bar, 8.0f + 2.0f * static_cast<float>(i)), i, 0.9f);
        }
        return;
    }
    snare(c, stepFrame(bar, 4), 0.9f);
    clap(c, stepFrame(bar, 4), 0.6f);
    if (inSection == 7) {
        // A short snare roll that speeds the section into the next one.
        for (int i = 0; i < 4; ++i) snare(c, stepFrame(bar, 12.0f + static_cast<float>(i)), 0.6f + 0.15f * static_cast<float>(i));
    } else {
        snare(c, stepFrame(bar, 12), 0.9f);
        clap(c, stepFrame(bar, 12), 0.6f);
    }
    hat(c, stepFrame(bar, 14), 0.8f, HatKind::Open);
}

void renderArp(Ctx& c, int bar) {
    static constexpr int kPatterns[4][16] = {
        {0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0, 1, 2, 3},
        {0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 1, 3, 0, 2, 3, 2},
        {0, -1, 1, -1, 2, -1, 3, -1, 2, -1, 1, -1, 0, -1, 1, -1},
        {3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0, 1, 2, 3, 2},
    };
    const Chord& chord = chordAt(bar);
    const int tones[4] = {chord.root + 12, third(chord) + 12, fifth(chord) + 12, chord.root + 24};
    for (int s = 0; s < 16; ++s) {
        const int index = kPatterns[bar / 8][s];
        if (index < 0) continue;
        arpNote(c, stepFrame(bar, static_cast<float>(s)), midiToHz(static_cast<float>(tones[index])), (s % 4 == 0) ? 1.0f : 0.7f);
    }
}

void renderLead(Ctx& c, int bar) {
    const int section = bar / 8;
    if (section == 2) return;  // Rest in the breakdown so the return hits harder.
    if (section == 0) {
        const int base = (bar % 8) * 16;
        for (const Melody& m : kLeadA) {
            if (m.step < base || m.step >= base + 16) continue;
            leadNote(c, stepFrame(bar, static_cast<float>(m.step - base)), midiToHz(static_cast<float>(m.midi)), stepSeconds(m.len), 0.85f, 1.3f, 3200.0f);
        }
        return;
    }
    const Chord& chord = chordAt(bar);
    const int tones[3] = {chord.root, third(chord), fifth(chord)};
    const Motif* motif = section == 1 ? kMotifB : kMotifD;
    const int count = section == 1 ? static_cast<int>(sizeof(kMotifB) / sizeof(kMotifB[0])) : static_cast<int>(sizeof(kMotifD) / sizeof(kMotifD[0]));
    for (int i = 0; i < count; ++i) {
        const int midi = intoLeadRange(tones[motif[i].tone]);
        leadNote(c, stepFrame(bar, motif[i].step), midiToHz(static_cast<float>(midi)), stepSeconds(motif[i].len), 0.8f, 1.3f, 3200.0f);
    }
}

// Two long growling notes per bar. At the end of a four bar group the second one slides
// up a semitone to the flat second, so the line leans without ever resolving.
void renderBossBass(Ctx& c, int bar) {
    const float root = static_cast<float>(kBossRoots[bar]);
    growlNote(c, stepFrame(bar, 0), midiToHz(root), stepSeconds(7.5f), 1.0f);
    const float second = (bar % 4 == 3) ? root + 1.0f : root;
    growlNote(c, stepFrame(bar, 8), midiToHz(second), stepSeconds(5.5f), 0.9f);
    growlNote(c, stepFrame(bar, 14), midiToHz(root), stepSeconds(1.5f), 0.6f);
}

// Half-time: a heavy kick on the downbeat, one a little later, and a huge snare on beat three.
// Every fourth bar toms fall away after the snare. Nothing busy and no bright cymbals.
void renderBossDrums(Ctx& c, int bar) {
    const int inGroup = bar % 4;
    bossKick(c, stepFrame(bar, 0), 1.0f);
    bossKick(c, stepFrame(bar, 10), 0.8f);
    bossSnare(c, stepFrame(bar, 8), 1.0f);
    if (inGroup == 3) {
        for (int i = 0; i < 3; ++i) darkTom(c, stepFrame(bar, 11.0f + 2.0f * static_cast<float>(i)), i, 0.9f - 0.1f * static_cast<float>(i));
    }
    if (bar % 8 == 0) darkTom(c, stepFrame(bar, 0), 2, 0.8f);
}

void renderBossLead(Ctx& c, int bar) {
    const Phrase& phrase = kBossMotif[bar % 4];
    const float root = static_cast<float>(kBossRoots[bar]);
    for (int i = 0; i < 2; ++i) {
        const float semitones = root + static_cast<float>(phrase.offset[i]);
        const float hold = stepSeconds(phrase.len[i]);
        const size_t at = stepFrame(bar, static_cast<float>(phrase.step[i]));
        // An octave pair: the low note carries weight, the upper one the harmonics speakers can play.
        leadNote(c, at, midiToHz(semitones + 12.0f), hold, 1.0f, 3.4f, 900.0f);
        leadNote(c, at, midiToHz(semitones + 24.0f), hold, 0.45f, 2.4f, 1100.0f);
    }
}

// A held tritone and flat ninth drone, sung by a dark choir. It never resolves.
// The chord's four notes are spread over the bars it lasts, so no single step does all the work.
void renderBossPad(Ctx& c, int bar) {
    const int root = kBossRoots[bar];
    int first = bar;
    while (first > 0 && kBossRoots[first - 1] == root) --first;
    int span = 1;
    while (first + span < kBossBars && kBossRoots[first + span] == root) ++span;
    const float hold = static_cast<float>(span * kBarFrames) / kSr + 0.2f;
    const int notes[4] = {root + 12, root + 18, root + 24, root + 25};
    for (int i = 0; i < 4; ++i) {
        if (i % span != bar - first) continue;
        darkPadNote(c, stepFrame(first, 0), midiToHz(static_cast<float>(notes[i])), hold, 0.5f, static_cast<float>(i) * 2.3f);
    }
}

// A few quieter copies, a dotted eighth note apart, give plucks the classic synthwave echo.
// Works from the end of the loop backward, so every echo reads samples not yet changed. The
// saved tail lets echoes at the start of the loop come from the end, which keeps the loop seamless.
constexpr int kEchoTaps = 3;
constexpr size_t kChunk = 131072;  // Samples handled per step, about 6 seconds of audio.

void echoRange(float* buf, size_t delay, const float* gains, const float* tail, size_t lo, size_t hi) {
    const size_t reach = delay * kEchoTaps;
    for (size_t i = hi; i-- > lo;) {
        float add = 0.0f;
        for (int k = 1; k <= kEchoTaps; ++k) {
            const size_t back = delay * static_cast<size_t>(k);
            if (i >= back) add += buf[i - back] * gains[k - 1];
            else add += tail[reach - (back - i)] * gains[k - 1];
        }
        buf[i] += add;
    }
}

struct StemSettings {
    const char* name;
    float rms;      // Loudness target, which sets the balance between layers.
    int echoSteps;  // Echo spacing in sixteenths, or 0 for none.
    float echo;     // First echo level.
};
constexpr StemSettings kSettings[kStemCount] = {
    {"pad", 0.070f, 0, 0},          {"bass", 0.075f, 0, 0},         {"drums_light", 0.060f, 0, 0}, {"arp", 0.080f, 3, 0.45f},
    {"drums_full", 0.070f, 0, 0},   {"lead", 0.085f, 3, 0.35f},     {"boss_bass", 0.100f, 0, 0},   {"boss_drums", 0.085f, 0, 0},
    {"boss_lead", 0.080f, 6, 0.35f}, {"boss_pad", 0.085f, 0, 0},
};

}  // namespace

const char* stemName(int stem) { return kSettings[stem].name; }

int Renderer::stepsTotal() {
    int total = 0;
    for (int stem = 0; stem < kStemCount; ++stem) {
        const int chunks = static_cast<int>((static_cast<size_t>(stemFrames(stem)) + kChunk - 1) / kChunk);
        total += stemBars(stem) + 2 * chunks + (kSettings[stem].echoSteps > 0 ? chunks : 0);
    }
    return total;
}

Renderer::Renderer(Bank& bank) : bank_(bank), scratch_(static_cast<size_t>(stemFrames(kPad)), 0.0f) {
    for (int s = 0; s < kStemCount; ++s) bank_.stems[s].assign(static_cast<size_t>(stemFrames(s)), 0);
}

void Renderer::renderBar(int stem, int bar) {
    Ctx c{scratch_.data(), static_cast<size_t>(stemFrames(stem))};
    switch (stem) {
        case kPad: renderPad(c, bar); break;
        case kBass: renderBass(c, bar); break;
        case kDrumsLight: renderDrumsLight(c, bar); break;
        case kArp: renderArp(c, bar); break;
        case kDrumsFull: renderDrumsFull(c, bar); break;
        case kLead: renderLead(c, bar); break;
        case kBossBass: renderBossBass(c, bar); break;
        case kBossDrums: renderBossDrums(c, bar); break;
        case kBossLead: renderBossLead(c, bar); break;
        case kBossPad: renderBossPad(c, bar); break;
        default: break;
    }
}

bool Renderer::step() {
    if (done()) return false;
    const size_t n = static_cast<size_t>(stemFrames(stem_));
    const StemSettings& set = kSettings[stem_];
    float* buf = scratch_.data();
    switch (phase_) {
        case Phase::Notes:
            if (bar_ == 0) std::fill(scratch_.begin(), scratch_.end(), 0.0f);
            renderBar(stem_, bar_);
            if (++bar_ == stemBars(stem_)) {
                bar_ = 0;
                if (set.echoSteps > 0) {
                    const size_t reach = static_cast<size_t>(static_cast<double>(set.echoSteps) * kStepFrames + 0.5) * kEchoTaps;
                    echoTail_.assign(buf + (n - reach), buf + n);
                    phase_ = Phase::Echo;
                    cursor_ = n;
                } else {
                    phase_ = Phase::Measure;
                    cursor_ = 0;
                    squares_ = 0.0;
                    sum_ = 0.0;
                    peak_ = 0.0f;
                }
            }
            break;
        case Phase::Echo: {
            const float gains[kEchoTaps] = {set.echo, set.echo * 0.5f, set.echo * 0.22f};
            const size_t delay = static_cast<size_t>(static_cast<double>(set.echoSteps) * kStepFrames + 0.5);
            const size_t lo = cursor_ > kChunk ? cursor_ - kChunk : 0;
            echoRange(buf, delay, gains, echoTail_.data(), lo, cursor_);
            cursor_ = lo;
            if (cursor_ == 0) {
                phase_ = Phase::Measure;
                squares_ = 0.0;
                sum_ = 0.0;
                peak_ = 0.0f;
            }
            break;
        }
        case Phase::Measure: {
            const size_t hi = std::min(n, cursor_ + kChunk);
            for (size_t i = cursor_; i < hi; ++i) {
                sum_ += buf[i];
                squares_ += static_cast<double>(buf[i]) * buf[i];
                peak_ = std::max(peak_, std::fabs(buf[i]));
            }
            cursor_ = hi;
            if (cursor_ == n) {
                // Spiky layers such as kicks lean to one side. Removing that average keeps the mix centered.
                const double count = static_cast<double>(n);
                mean_ = static_cast<float>(sum_ / count);
                const float rms = static_cast<float>(std::sqrt(std::max(0.0, squares_ / count - static_cast<double>(mean_) * mean_)));
                peak_ += std::fabs(mean_);
                // Match the loudness target. Spiky layers such as drums would need to be far
                // louder than their peaks allow, so the peaks are rounded off softly instead.
                scale_ = rms > 1e-9f ? set.rms / rms : 0.0f;
                if (peak_ * scale_ > 1.3f) scale_ = 1.3f / peak_;
                phase_ = Phase::Convert;
                cursor_ = 0;
            }
            break;
        }
        case Phase::Convert: {
            std::vector<int16_t>& out = bank_.stems[stem_];
            const size_t hi = std::min(n, cursor_ + kChunk);
            for (size_t i = cursor_; i < hi; ++i) out[i] = static_cast<int16_t>(std::lround(std::clamp(softClip((buf[i] - mean_) * scale_), -1.0f, 1.0f) * 32767.0f));
            cursor_ = hi;
            if (cursor_ == n) {
                ++stem_;
                phase_ = Phase::Notes;
            }
            break;
        }
    }
    ++stepsDone_;
    return !done();
}

void renderAll(Bank& bank) {
    Renderer renderer(bank);
    while (renderer.step()) {
    }
}

}  // namespace ls::audio::music
