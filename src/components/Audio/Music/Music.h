#pragma once

#include "components/Audio/Synth/Synth.h"

#include <cstdint>
#include <vector>

// Dark synthwave in A minor at 120 BPM, built as separate layers ("stems").
// Each stem is one long loop. The game mixes the stems at run time, so adding
// or removing a layer is only a volume change, and no layer ever changes tempo.
namespace ls::audio::music {

inline constexpr int kSampleRate = synth::kSampleRate;
inline constexpr int kBarFrames = kSampleRate * 2;  // 4 beats at 120 BPM is exactly 2 seconds.
inline constexpr int kMainBars = 32;                // Four sections of 8 bars: 64 seconds before it repeats.
inline constexpr int kBossBars = 16;                // The boss layers loop every 32 seconds.

enum Stem : int {
    kPad, kBass, kDrumsLight, kArp, kDrumsFull, kLead,  // Main layers, added as intensity rises.
    kBossBass, kBossDrums, kBossLead, kBossPad,          // Heavier layers that replace the main ones in the boss fight.
    kStemCount
};

inline constexpr bool isBossStem(int stem) { return stem >= kBossBass; }
inline constexpr int stemBars(int stem) { return isBossStem(stem) ? kBossBars : kMainBars; }
inline constexpr int stemFrames(int stem) { return stemBars(stem) * kBarFrames; }

const char* stemName(int stem);

// Finished loops, as 16 bit samples to keep memory down.
struct Bank {
    std::vector<int16_t> stems[kStemCount];
};

// Renders the bank a small piece at a time. Each step() call does a short piece
// of work, so the caller can spread rendering over many frames and the game
// never freezes at startup.
class Renderer {
public:
    explicit Renderer(Bank& bank);
    // Does one piece of work. Returns false once everything is finished.
    bool step();
    bool done() const { return stem_ >= kStemCount; }
    int stem() const { return stem_; }  // The layer being worked on now.
    int stepsDone() const { return stepsDone_; }
    static int stepsTotal();

private:
    // Each layer goes through these in order. Long passes over the whole loop are
    // cut into chunks, so no single step takes more than a few milliseconds.
    enum class Phase { Notes, Echo, Measure, Convert };
    void renderBar(int stem, int bar);

    Bank& bank_;
    std::vector<float> scratch_;
    std::vector<float> echoTail_;  // Copy of the loop end, so echoes can wrap around to the start.
    Phase phase_ = Phase::Notes;
    int stem_ = 0;
    int bar_ = 0;
    size_t cursor_ = 0;
    double sum_ = 0.0;
    double squares_ = 0.0;
    float mean_ = 0.0f;
    float peak_ = 0.0f;
    float scale_ = 0.0f;
    int stepsDone_ = 0;
};

// Renders everything in one go. For tests and tools.
void renderAll(Bank& bank);

}  // namespace ls::audio::music
