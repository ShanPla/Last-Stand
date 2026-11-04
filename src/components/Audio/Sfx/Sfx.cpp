#include "components/Audio/Sfx/Sfx.h"

#include <algorithm>

namespace ls::audio::sfx {

using namespace synth;

namespace {

constexpr float kSr = static_cast<float>(kSampleRate);

// Cycles of a wave that glides from f0 to f1. Pitch drops are what make most
// thumps and booms sound heavy, so almost every effect uses this.
float glideCycles(float t, float f0, float f1, float tau) { return f1 * t + (f0 - f1) * tau * (1.0f - std::exp(-t / tau)); }
float glideHz(float t, float f0, float f1, float tau) { return f1 + (f0 - f1) * std::exp(-t / tau); }
float frac(float x) { return x - std::floor(x); }
float glideSine(float t, float f0, float f1, float tau) { return sine(frac(glideCycles(t, f0, f1, tau))); }
// A pitch drop run through soft saturation. The saturation adds upper harmonics,
// so a deep thump is still heard on laptop and phone speakers that cannot play bass.
float thump(float t, float f0, float f1, float tau, float drive = 2.0f) { return softClip(glideSine(t, f0, f1, tau) * drive); }
float glideSaw(float t, float f0, float f1, float tau) {
    return saw(frac(glideCycles(t, f0, f1, tau)), glideHz(t, f0, f1, tau) / kSr);
}

// Fast attack, exponential decay. The attack stops a click at the very start.
float env(float t, float attack, float decayTime) { return std::min(1.0f, t / attack) * decay(t, decayTime); }

// A short pitched note: two slightly detuned saws through a low-pass.
// Used for pickups, level up and fanfares.
Samples tone(float hz, float seconds, float decayTime, float cutoff, float attack = 0.004f) {
    float p1 = 0.0f, p2 = 0.25f;
    OnePole lp;
    lp.setCutoff(cutoff);
    const float s1 = hz / kSr, s2 = hz * 1.006f / kSr;
    return render(seconds, [&](float t, size_t) {
        const float body = 0.5f * (saw(p1, s1) + saw(p2, s2));
        advance(p1, s1);
        advance(p2, s2);
        const float release = std::min(1.0f, (seconds - t) / 0.02f);
        return lp.low(body) * env(t, attack, decayTime) * release;
    });
}

// A soft bell-like note made of sine partials. Gentler than tone(), so it is used for UI.
Samples bell(float hz, float seconds, float decayTime) {
    return render(seconds, [&](float t, size_t) {
        const float p = hz * t;
        const float s = sine(frac(p)) + 0.35f * sine(frac(2.0f * p)) + 0.12f * sine(frac(3.0f * p));
        return s * env(t, 0.003f, decayTime) * std::min(1.0f, (seconds - t) / 0.02f);
    });
}

// A tiny mechanical click: a burst of high noise plus a short ring.
Samples click(float ringHz, float noiseHz, float seconds, uint32_t seed) {
    Noise n(seed);
    Biquad bp;
    bp.bandpass(noiseHz, 0.9f);
    return render(seconds, [&](float t, size_t) {
        return bp.process(n.next()) * decay(t, seconds * 0.35f) * 0.9f + sine(frac(ringHz * t)) * decay(t, seconds * 0.5f) * 0.6f;
    });
}

// Layers a boom, a noisy rumble and a short crack. Bigger inputs give a bigger explosion.
Samples explosion(float seconds, float boomHz, float boomDecay, float rumbleCut, float rumbleDecay, uint32_t seed) {
    Noise n(seed);
    OnePole rumble, crackHp;
    crackHp.setCutoff(1800.0f);
    int tick = 0;
    return render(seconds, [&](float t, size_t) {
        if ((tick++ & 15) == 0) rumble.setCutoff(rumbleCut * std::exp(-t / 0.22f) + 110.0f);
        const float noise = n.next();
        const float boom = glideSine(t, boomHz * 1.6f, boomHz * 0.45f, 0.09f) * decay(t, boomDecay) * 1.2f;
        const float low = rumble.low(rumble.low(noise)) * 2.6f * decay(t, rumbleDecay);
        const float crack = crackHp.high(noise) * decay(t, 0.05f) * 0.6f;
        return boom + low + crack;
    });
}

Samples fireRifle() {
    Noise n(11);
    Biquad bp;
    bp.bandpass(2200.0f, 0.8f);
    OnePole lp;
    lp.setCutoff(6000.0f);
    return render(0.22f, [&](float t, size_t) {
        const float crack = lp.low(bp.process(n.next())) * decay(t, 0.05f) * 3.2f;
        const float body = thump(t, 420.0f, 110.0f, 0.025f) * decay(t, 0.08f);
        return crack + body;
    });
}

Samples fireShotgun() {
    Noise n(12);
    OnePole lp, tail;
    lp.setCutoff(3500.0f);
    tail.setCutoff(500.0f);
    return render(0.5f, [&](float t, size_t) {
        const float x = n.next();
        const float blast = lp.low(x) * decay(t, 0.16f) * 2.4f;
        const float body = thump(t, 190.0f, 60.0f, 0.05f) * decay(t, 0.2f);
        const float rumble = tail.low(x) * decay(t, 0.35f) * 0.9f;
        return blast + body + rumble;
    });
}

Samples fireSmg() {
    Noise n(13);
    Biquad bp;
    bp.bandpass(2800.0f, 0.9f);
    return render(0.13f, [&](float t, size_t) {
        return bp.process(n.next()) * decay(t, 0.035f) * 2.8f + thump(t, 450.0f, 200.0f, 0.012f) * decay(t, 0.045f);
    });
}

Samples fireSniper() {
    Noise n(14);
    OnePole hp, tail, lp;
    hp.setCutoff(2500.0f);
    tail.setCutoff(1300.0f);
    lp.setCutoff(6000.0f);
    return render(0.85f, [&](float t, size_t) {
        const float x = n.next();
        const float crack = lp.low(hp.high(x)) * decay(t, 0.03f) * 3.0f;
        const float boom = thump(t, 170.0f, 48.0f, 0.08f, 2.5f) * decay(t, 0.45f);
        const float air = tail.low(x) * decay(t, 0.5f) * 1.5f;
        const float ring = sine(frac(1400.0f * t)) * decay(t, 0.25f) * 0.12f;
        return crack + boom + air + ring;
    });
}

// A soft whoosh with a Hann window. Fired every 0.2 s and overlapped with
// itself, the windows add up to a steady level, so it sounds like one stream.
Samples fireFlame() {
    constexpr float kLength = 0.4f;
    Noise n(15);
    Biquad bp;
    bp.bandpass(650.0f, 0.6f);
    OnePole lp;
    lp.setCutoff(1800.0f);
    float crackle = 0.0f;
    return render(kLength, [&](float t, size_t) {
        const float x = n.next();
        const float w = std::sin(kPi * t / kLength);
        const float window = w * w;
        const float flutter = 1.0f + 0.35f * sine(frac(17.0f * t)) + 0.2f * sine(frac(31.0f * t));
        if (n.next01() < 0.004f) crackle = 1.0f;
        crackle *= 0.9f;
        const float hiss = bp.process(x) * 1.2f + lp.low(x) * 0.5f;
        return (hiss * flutter + x * crackle * 0.6f) * window;
    });
}

Samples fireGrenade() {
    Noise n(16);
    OnePole lp;
    lp.setCutoff(2000.0f);
    return render(0.4f, [&](float t, size_t) {
        const float body = thump(t, 240.0f, 90.0f, 0.04f) * decay(t, 0.14f);
        const float puff = lp.low(n.next()) * decay(t, 0.07f) * 2.5f;
        const float pop = triangle(frac(glideCycles(t, 330.0f, 120.0f, 0.08f))) * decay(t, 0.16f) * 0.6f;
        return body + puff + pop;
    });
}

Samples fireRocket() {
    Noise n(17);
    Biquad bp;
    OnePole lp;
    lp.setCutoff(2500.0f);
    int tick = 0;
    return render(0.7f, [&](float t, size_t) {
        if ((tick++ & 31) == 0) bp.bandpass(400.0f + 1800.0f * std::min(1.0f, t / 0.5f), 0.9f);
        const float whoosh = lp.low(bp.process(n.next())) * env(t, 0.03f, 0.45f) * 2.0f;
        const float thump = glideSine(t, 120.0f, 45.0f, 0.05f) * decay(t, 0.15f);
        return whoosh + thump;
    });
}

Samples fireMine() {
    Samples out = render(0.4f, [&](float t, size_t) {
        const float clunk = glideSine(t, 480.0f, 300.0f, 0.02f) * decay(t, 0.08f) + 0.4f * sine(frac(810.0f * t)) * decay(t, 0.05f);
        return clunk;
    });
    // A short beep after the clunk tells the player the mine is now armed.
    const Samples beep = render(0.08f, [&](float t, size_t) { return sine(frac(1320.0f * t)) * env(t, 0.003f, 0.06f) * 0.5f; });
    mixInto(out, beep, 0.15f, 1.0f);
    return out;
}

Samples fireTesla() {
    Noise n(18);
    Biquad bp;
    bp.bandpass(3500.0f, 2.0f);
    OnePole lp;
    lp.setCutoff(1500.0f);
    float p = 0.0f, spark = 0.0f;
    const float s = 120.0f / kSr;
    return render(0.45f, [&](float t, size_t) {
        const float zap = glideSine(t, 2400.0f, 500.0f, 0.05f) * decay(t, 0.1f) * 0.5f;
        const float buzz = lp.low(saw(p, s)) * (0.6f + 0.4f * sine(frac(50.0f * t))) * decay(t, 0.3f) * 0.8f;
        advance(p, s);
        if (n.next01() < 0.02f * decay(t, 0.4f)) spark = 1.0f;
        spark *= 0.85f;
        return zap + buzz + bp.process(n.next()) * spark * 1.5f;
    });
}

Samples reload() {
    Samples out(frames(0.32f), 0.0f);
    mixInto(out, click(1100.0f, 2500.0f, 0.06f, 21), 0.0f, 1.0f);
    mixInto(out, click(650.0f, 1400.0f, 0.09f, 22), 0.15f, 1.2f);
    return out;
}

Samples dry() { return click(520.0f, 1500.0f, 0.09f, 23); }

Samples enemyHit() {
    Noise n(24);
    OnePole lp;
    lp.setCutoff(2200.0f);
    return render(0.06f, [&](float t, size_t) {
        return glideSine(t, 430.0f, 230.0f, 0.015f) * decay(t, 0.035f) + lp.low(n.next()) * decay(t, 0.015f) * 1.4f;
    });
}

Samples killSquelch() {
    Noise n(25);
    OnePole lp;
    int tick = 0;
    return render(0.26f, [&](float t, size_t) {
        if ((tick++ & 15) == 0) lp.setCutoff(1500.0f * std::exp(-t / 0.07f) + 220.0f);
        const float wet = lp.low(lp.low(n.next())) * 3.0f * decay(t, 0.1f);
        const float body = glideSine(t, 190.0f, 70.0f, 0.04f) * (0.7f + 0.3f * sine(frac(38.0f * t))) * decay(t, 0.11f);
        const float bubble = glideSine(t, 300.0f, 750.0f, 0.03f) * decay(t, 0.06f) * 0.3f;
        return wet + body + bubble;
    });
}

Samples killThud() {
    Noise n(26);
    OnePole lp;
    lp.setCutoff(900.0f);
    return render(0.24f, [&](float t, size_t) {
        return thump(t, 160.0f, 55.0f, 0.05f, 2.5f) * decay(t, 0.13f) + lp.low(n.next()) * decay(t, 0.05f) * 1.6f;
    });
}

Samples killPop() {
    Noise n(27);
    Biquad bp;
    bp.bandpass(900.0f, 1.0f);
    return render(0.18f, [&](float t, size_t) {
        return glideSine(t, 430.0f, 140.0f, 0.02f) * decay(t, 0.06f) + glideSine(t, 600.0f, 1000.0f, 0.03f) * decay(t, 0.05f) * 0.4f +
               bp.process(n.next()) * decay(t, 0.03f) * 0.8f;
    });
}

Samples eliteKilled() {
    Noise n(28);
    OnePole lp, hp;
    hp.setCutoff(1500.0f);
    int tick = 0;
    return render(0.75f, [&](float t, size_t) {
        if ((tick++ & 15) == 0) lp.setCutoff(3000.0f * std::exp(-t / 0.15f) + 150.0f);
        const float x = n.next();
        const float wet = lp.low(lp.low(x)) * 3.5f * decay(t, 0.3f);
        const float boom = thump(t, 170.0f, 40.0f, 0.08f, 2.5f) * decay(t, 0.4f);
        const float crunch = hp.high(x) * decay(t, 0.06f) * 0.9f;
        const float ping = (sine(frac(560.0f * t)) + 0.7f * sine(frac(803.0f * t))) * decay(t, 0.22f) * 0.12f;
        return wet + boom + crunch + ping;
    });
}

// Buzzy, a little sour, and short: it should be clear and unpleasant without being painful.
Samples playerHit() {
    Noise n(29);
    OnePole lp;
    lp.setCutoff(1400.0f);
    Biquad bp;
    bp.bandpass(800.0f, 0.8f);
    return render(0.36f, [&](float t, size_t) {
        const float buzz = lp.low(glideSaw(t, 170.0f, 80.0f, 0.1f) + glideSaw(t, 181.0f, 84.0f, 0.1f)) * decay(t, 0.2f) * 0.7f;
        const float thump = glideSine(t, 125.0f, 50.0f, 0.03f) * decay(t, 0.13f) * 0.9f;
        return buzz + thump + bp.process(n.next()) * decay(t, 0.06f) * 0.4f;
    });
}

Samples playerDied() {
    Noise n(30);
    OnePole lp, rumble;
    rumble.setCutoff(600.0f);
    int tick = 0;
    return render(1.7f, [&](float t, size_t) {
        if ((tick++ & 31) == 0) lp.setCutoff(1500.0f * std::exp(-t / 0.6f) + 80.0f);
        const float fall = lp.low(glideSaw(t, 300.0f, 42.0f, 0.5f) + glideSaw(t, 306.0f, 44.0f, 0.5f)) * env(t, 0.005f, 1.2f) * 0.9f;
        const float sub = glideSine(t, 80.0f, 28.0f, 0.4f) * decay(t, 1.3f) * 1.1f;
        return fall + sub + rumble.low(n.next()) * decay(t, 0.5f) * 0.5f;
    });
}

// Minor pentatonic steps above A4, so a run of gems plays a rising tune that fits the music.
Samples gem(int step) {
    static constexpr int kLadder[kGemSteps] = {0, 3, 5, 7, 10, 12, 15, 17, 19, 22};
    const float hz = midiToHz(69.0f + static_cast<float>(kLadder[step]));
    return render(0.24f, [&](float t, size_t) {
        const float p = hz * t;
        const float s = triangle(frac(p)) * 0.8f + sine(frac(2.0f * p)) * 0.3f;
        return s * env(t, 0.003f, 0.13f);
    });
}

Samples levelUp() {
    Samples out(frames(1.3f), 0.0f);
    const float notes[] = {69, 72, 76, 81};
    for (int i = 0; i < 4; ++i) mixInto(out, tone(midiToHz(notes[i]), 0.4f, 0.25f, 4500.0f), 0.07f * static_cast<float>(i), 0.7f);
    for (int i = 0; i < 4; ++i) mixInto(out, tone(midiToHz(notes[i]), 0.95f, 0.5f, 3500.0f), 0.3f, 0.35f);
    mixInto(out, bell(midiToHz(93), 0.6f, 0.3f), 0.3f, 0.25f);
    return out;
}

Samples pickupHeal() {
    Samples out(frames(0.6f), 0.0f);
    mixInto(out, bell(midiToHz(76), 0.4f, 0.22f), 0.0f, 1.0f);
    mixInto(out, bell(midiToHz(83), 0.5f, 0.28f), 0.1f, 1.0f);
    mixInto(out, bell(midiToHz(83) * 1.004f, 0.5f, 0.28f), 0.1f, 0.5f);
    return out;
}

Samples pickupMagnet() {
    Noise n(31);
    Biquad bp;
    int tick = 0;
    return render(0.65f, [&](float t, size_t) {
        const float u = std::min(1.0f, t / 0.5f);
        if ((tick++ & 31) == 0) bp.bandpass(300.0f + 1500.0f * u * u, 1.2f);
        const float tremolo = 0.65f + 0.35f * sine(frac(14.0f * t));
        const float swell = std::min(1.0f, t / 0.06f) * (t < 0.45f ? 1.0f : decay(t - 0.45f, 0.2f));
        return (glideSine(t, 140.0f, 700.0f, 0.4f) * 0.9f + bp.process(n.next()) * 0.35f) * tremolo * swell;
    });
}

Samples pickupBomb() {
    Samples out(frames(0.55f), 0.0f);
    const float starts[] = {0.0f, 0.11f, 0.22f};
    for (float s : starts) {
        mixInto(out, render(0.08f, [&](float t, size_t) { return square(frac(880.0f * t), 880.0f / kSr) * env(t, 0.002f, 0.05f) * 0.5f; }), s, 1.0f);
    }
    mixInto(out, render(0.2f, [&](float t, size_t) { return square(frac(440.0f * t), 440.0f / kSr) * env(t, 0.002f, 0.12f) * 0.55f; }), 0.33f, 1.0f);
    OnePole lp;
    lp.setCutoff(3000.0f);
    for (float& v : out) v = lp.low(v);
    return out;
}

Samples pickupAmmo() {
    Samples out(frames(0.34f), 0.0f);
    mixInto(out, click(900.0f, 2200.0f, 0.07f, 32), 0.0f, 1.0f);
    mixInto(out, click(500.0f, 1300.0f, 0.08f, 33), 0.07f, 1.1f);
    mixInto(out, bell(2100.0f, 0.2f, 0.1f), 0.13f, 0.25f);
    mixInto(out, bell(2900.0f, 0.18f, 0.08f), 0.15f, 0.15f);
    return out;
}

Samples bombBlast() {
    Samples out = explosion(1.9f, 90.0f, 0.9f, 2800.0f, 1.3f, 41);
    addEchoes(out, 0.09f, 0.4f, 3);
    return out;
}

Samples explosionSmall() { return explosion(0.8f, 120.0f, 0.35f, 1900.0f, 0.55f, 42); }

Samples evolution() {
    Samples out(frames(2.0f), 0.0f);
    const float run[] = {57, 64, 69, 73, 76};
    for (int i = 0; i < 5; ++i) mixInto(out, tone(midiToHz(run[i]), 0.3f, 0.2f, 5000.0f), 0.09f * static_cast<float>(i), 0.8f);
    const float chord[] = {57, 69, 73, 76, 81};
    for (float note : chord) mixInto(out, tone(midiToHz(note), 1.4f, 0.8f, 3800.0f, 0.01f), 0.5f, 0.45f);
    mixInto(out, bell(midiToHz(93), 0.8f, 0.4f), 0.55f, 0.3f);
    mixInto(out, bell(midiToHz(100), 0.8f, 0.4f), 0.75f, 0.2f);
    Noise n(43);
    Biquad bp;
    int tick = 0;
    const Samples sweep = render(0.5f, [&](float t, size_t) {
        if ((tick++ & 31) == 0) bp.bandpass(500.0f + 5000.0f * (t / 0.5f), 1.5f);
        return bp.process(n.next()) * std::sin(kPi * t / 0.5f) * 0.4f;
    });
    mixInto(out, sweep, 0.0f, 1.0f);
    addEchoes(out, 0.14f, 0.3f, 3);
    return out;
}

Samples bossSpawned() {
    Noise n(44);
    OnePole lp, rise;
    rise.setCutoff(300.0f);
    Biquad bp;
    float p1 = 0.0f, p2 = 0.0f, p3 = 0.0f;
    int tick = 0;
    const float f1 = 55.0f, f2 = 55.5f, f3 = 77.8f;  // The third one is a tritone: the interval that sounds ominous.
    return render(3.0f, [&](float t, size_t) {
        if ((tick++ & 31) == 0) {
            lp.setCutoff(250.0f + 700.0f * std::min(1.0f, t / 1.2f));
            bp.bandpass(200.0f + 1300.0f * std::min(1.0f, t / 1.15f), 1.0f);
        }
        const float swell = t < 1.2f ? (t / 1.2f) * (t / 1.2f) : decay(t - 1.2f, 1.8f);
        const float drone = lp.low(saw(p1, f1 / kSr) + saw(p2, f2 / kSr) + 0.6f * saw(p3, f3 / kSr)) * swell * 0.5f;
        advance(p1, f1 / kSr);
        advance(p2, f2 / kSr);
        advance(p3, f3 / kSr);
        const float riser = t < 1.15f ? bp.process(n.next()) * (t / 1.15f) * 0.5f : 0.0f;
        const float since = t - 1.15f;
        float impact = 0.0f;
        if (since > 0.0f) impact = glideSine(since, 85.0f, 24.0f, 0.1f) * decay(since, 0.9f) * 1.3f + rise.low(n.next()) * decay(since, 0.4f) * 0.6f;
        return drone + riser + impact;
    });
}

Samples bossPhase() {
    Samples out = explosion(1.3f, 100.0f, 0.5f, 1500.0f, 0.5f, 45);
    // Three notes a semitone apart make a sour stab, then a siren sweeps up.
    const float notes[] = {57, 58, 63};
    for (float note : notes) mixInto(out, tone(midiToHz(note), 0.9f, 0.5f, 1800.0f), 0.05f, 0.5f);
    const Samples siren = render(0.9f, [&](float t, size_t) { return glideSine(t, 400.0f, 900.0f, 0.3f) * std::sin(kPi * t / 0.9f) * 0.3f; });
    mixInto(out, siren, 0.1f, 1.0f);
    return out;
}

Samples bossKilled() {
    Samples out = explosion(4.2f, 80.0f, 1.2f, 3000.0f, 1.8f, 46);
    const float run[] = {57, 64, 69, 73, 76, 81, 85};
    for (int i = 0; i < 7; ++i) mixInto(out, tone(midiToHz(run[i]), 0.5f, 0.3f, 5000.0f), 0.7f + 0.1f * static_cast<float>(i), 0.6f);
    const float chord[] = {45, 57, 69, 73, 76, 81};
    for (float note : chord) mixInto(out, tone(midiToHz(note), 2.6f, 1.5f, 3500.0f, 0.02f), 1.5f, 0.45f);
    mixInto(out, bell(midiToHz(97), 1.5f, 0.8f), 1.6f, 0.3f);
    mixInto(out, bell(midiToHz(100), 1.5f, 0.8f), 1.8f, 0.25f);
    addEchoes(out, 0.21f, 0.4f, 4);
    return out;
}

Samples uiMove() { return render(0.07f, [&](float t, size_t) { return sine(frac(760.0f * t)) * env(t, 0.002f, 0.04f); }); }

Samples uiConfirm() {
    Samples out(frames(0.26f), 0.0f);
    mixInto(out, bell(midiToHz(76), 0.15f, 0.08f), 0.0f, 0.9f);
    mixInto(out, bell(midiToHz(81), 0.22f, 0.12f), 0.07f, 1.0f);
    return out;
}

Samples uiBack() {
    Samples out(frames(0.24f), 0.0f);
    mixInto(out, bell(midiToHz(81), 0.13f, 0.07f), 0.0f, 0.8f);
    mixInto(out, bell(midiToHz(74), 0.2f, 0.1f), 0.06f, 0.9f);
    return out;
}

Samples uiCardPick() {
    Samples out(frames(0.45f), 0.0f);
    const float notes[] = {76, 81, 88};
    for (float note : notes) mixInto(out, bell(midiToHz(note), 0.4f, 0.2f), 0.0f, 0.7f);
    mixInto(out, bell(midiToHz(93), 0.3f, 0.12f), 0.05f, 0.3f);
    return out;
}

Samples uiReroll() {
    Samples out(frames(0.45f), 0.0f);
    Noise n(47);
    for (int i = 0; i < 6; ++i) mixInto(out, click(n.range(600.0f, 1800.0f), n.range(1500.0f, 3000.0f), 0.04f, 50u + static_cast<uint32_t>(i)), 0.045f * static_cast<float>(i), 0.8f);
    mixInto(out, render(0.15f, [&](float t, size_t) { return glideSine(t, 500.0f, 900.0f, 0.05f) * env(t, 0.005f, 0.1f) * 0.5f; }), 0.28f, 1.0f);
    return out;
}

Samples uiPause() {
    Samples out(frames(0.32f), 0.0f);
    mixInto(out, bell(midiToHz(64), 0.2f, 0.1f), 0.0f, 0.9f);
    mixInto(out, bell(midiToHz(57), 0.26f, 0.14f), 0.1f, 1.0f);
    return out;
}

struct Entry {
    SfxInfo info;
    float peak;  // Loudness of the finished sound, 0 to 1. This sets the balance between sounds.
};

// voices = copies that can overlap, minInterval = seconds between starts.
const Entry kEntries[kSfxCount] = {
    {{"fire_rifle", 3, 0.035f}, 0.50f},    {{"fire_shotgun", 2, 0.05f}, 0.65f}, {{"fire_smg", 3, 0.04f}, 0.38f},
    {{"fire_sniper", 2, 0.10f}, 0.70f},    {{"fire_flame", 3, 0.17f}, 0.34f},   {{"fire_grenade", 2, 0.08f}, 0.55f},
    {{"fire_rocket", 3, 0.05f}, 0.48f},    {{"fire_mine", 2, 0.10f}, 0.42f},    {{"fire_tesla", 2, 0.10f}, 0.38f},
    {{"reload", 1, 0.20f}, 0.34f},         {{"dry", 1, 0.25f}, 0.30f},          {{"enemy_hit", 2, 0.08f}, 0.18f},
    {{"kill_squelch", 4, 0.05f}, 0.42f},   {{"kill_thud", 3, 0.05f}, 0.42f},    {{"kill_pop", 3, 0.05f}, 0.38f},
    {{"elite_killed", 2, 0.15f}, 0.62f},   {{"player_hit", 2, 0.12f}, 0.65f},   {{"player_died", 1, 2.0f}, 0.75f},
    {{"gem_0", 2, 0.035f}, 0.26f},         {{"gem_1", 2, 0.035f}, 0.26f},       {{"gem_2", 2, 0.035f}, 0.26f},
    {{"gem_3", 2, 0.035f}, 0.26f},         {{"gem_4", 2, 0.035f}, 0.26f},       {{"gem_5", 2, 0.035f}, 0.26f},
    {{"gem_6", 2, 0.035f}, 0.26f},         {{"gem_7", 2, 0.035f}, 0.26f},       {{"gem_8", 2, 0.035f}, 0.26f},
    {{"gem_9", 2, 0.035f}, 0.26f},         {{"level_up", 1, 0.5f}, 0.55f},      {{"pickup_heal", 1, 0.1f}, 0.45f},
    {{"pickup_magnet", 1, 0.3f}, 0.24f},   {{"pickup_bomb", 1, 0.3f}, 0.42f},   {{"pickup_ammo", 1, 0.1f}, 0.40f},
    {{"bomb_blast", 2, 0.3f}, 0.95f},      {{"explosion", 3, 0.07f}, 0.70f},    {{"evolution", 1, 1.0f}, 0.65f},
    {{"boss_spawned", 1, 2.0f}, 0.80f},    {{"boss_phase", 1, 1.0f}, 0.75f},    {{"boss_killed", 1, 5.0f}, 0.95f},
    {{"ui_move", 1, 0.04f}, 0.22f},        {{"ui_confirm", 1, 0.06f}, 0.32f},   {{"ui_back", 1, 0.06f}, 0.30f},
    {{"ui_card_pick", 1, 0.06f}, 0.34f},   {{"ui_reroll", 1, 0.1f}, 0.32f},     {{"ui_pause", 1, 0.1f}, 0.32f},
};

}  // namespace

const SfxInfo& info(SfxId id) { return kEntries[static_cast<int>(id)].info; }

Samples renderSfx(SfxId id) {
    Samples out;
    const int index = static_cast<int>(id);
    switch (id) {
        case SfxId::FireRifle: out = fireRifle(); break;
        case SfxId::FireShotgun: out = fireShotgun(); break;
        case SfxId::FireSmg: out = fireSmg(); break;
        case SfxId::FireSniper: out = fireSniper(); break;
        case SfxId::FireFlame: out = fireFlame(); break;
        case SfxId::FireGrenade: out = fireGrenade(); break;
        case SfxId::FireRocket: out = fireRocket(); break;
        case SfxId::FireMine: out = fireMine(); break;
        case SfxId::FireTesla: out = fireTesla(); break;
        case SfxId::Reload: out = reload(); break;
        case SfxId::Dry: out = dry(); break;
        case SfxId::EnemyHit: out = enemyHit(); break;
        case SfxId::KillSquelch: out = killSquelch(); break;
        case SfxId::KillThud: out = killThud(); break;
        case SfxId::KillPop: out = killPop(); break;
        case SfxId::EliteKilled: out = eliteKilled(); break;
        case SfxId::PlayerHit: out = playerHit(); break;
        case SfxId::PlayerDied: out = playerDied(); break;
        case SfxId::LevelUp: out = levelUp(); break;
        case SfxId::PickupHeal: out = pickupHeal(); break;
        case SfxId::PickupMagnet: out = pickupMagnet(); break;
        case SfxId::PickupBomb: out = pickupBomb(); break;
        case SfxId::PickupAmmo: out = pickupAmmo(); break;
        case SfxId::BombBlast: out = bombBlast(); break;
        case SfxId::Explosion: out = explosionSmall(); break;
        case SfxId::Evolution: out = evolution(); break;
        case SfxId::BossSpawned: out = bossSpawned(); break;
        case SfxId::BossPhase: out = bossPhase(); break;
        case SfxId::BossKilled: out = bossKilled(); break;
        case SfxId::UiMove: out = uiMove(); break;
        case SfxId::UiConfirm: out = uiConfirm(); break;
        case SfxId::UiBack: out = uiBack(); break;
        case SfxId::UiCardPick: out = uiCardPick(); break;
        case SfxId::UiReroll: out = uiReroll(); break;
        case SfxId::UiPause: out = uiPause(); break;
        default:
            if (index >= static_cast<int>(SfxId::Gem0) && index <= static_cast<int>(SfxId::Gem9)) out = gem(index - static_cast<int>(SfxId::Gem0));
            break;
    }
    // Flame is a windowed loop element, so it must not be faded harder than its window.
    finish(out, kEntries[index].peak, id == SfxId::FireFlame ? 0.002f : 0.02f);
    return out;
}

void renderAll(SfxBank& bank) {
    for (int i = 0; i < kSfxCount; ++i) bank.sounds[i] = renderSfx(static_cast<SfxId>(i));
}

}  // namespace ls::audio::sfx
