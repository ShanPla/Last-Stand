#pragma once

#include "core/GameState.h"

#include <string>
#include <string_view>

// Saved data: the player's settings, the chosen difficulty, map and mode, and their best runs. This file has no raylib
// and no browser code, so unit tests can parse and serialize without a window.
// Reading and writing the actual storage lives in Platform.
namespace ls {

inline constexpr int kSaveVersion = 1;

struct Settings {
    float musicVolume = 0.7f;     // 0 to 1.
    float effectsVolume = 0.8f;   // 0 to 1.
    float screenShake = 1.0f;     // 0 to 1. Scales all screen shake.
    float flashIntensity = 1.0f;  // 0 to 1. Scales hit, screen and explosion flashes, and the low health pulse.
    bool damageNumbers = true;    // Turns damage numbers on or off.
    // No shake or hit pause, fewer particles, no flicker, simple fades and a steady low
    // health warning. Starts from the system preference when nothing is saved.
    bool reducedMotion = false;
};

struct BestRun {
    float bestTimeSeconds = 0.0f;
    int mostKills = 0;
    int highestLevel = 0;
    int wins = 0;
    int runs = 0;  // Finished runs, won or lost.
    int mostBosses = 0;  // Endless only: the most bosses killed in one run.
};

inline constexpr int kDifficultyCount = static_cast<int>(Difficulty::Count);

// The format did not change in a way an older file can't be read in, so the version
// stays 1. A file with no difficulty means Normal, and its single best run is Normal's.
// A file with no mode means Story, and it has no Endless best runs yet.
struct SaveData {
    int version = kSaveVersion;
    Settings settings;
    Difficulty difficulty = Difficulty::Normal;  // The level the title screen has selected.
    int map = 0;  // The map the title screen has selected. A number, because maps are not part of this file's world.
    RunMode mode = RunMode::Story;               // The mode the title screen has selected.
    BestRun best[kDifficultyCount];              // Story's best run per level, indexed by Difficulty.
    BestRun endlessBest[kDifficultyCount];       // Endless keeps its own, because its runs are far longer.

    BestRun& bestFor(Difficulty level) { return best[static_cast<int>(level)]; }
    const BestRun& bestFor(Difficulty level) const { return best[static_cast<int>(level)]; }
    BestRun& bestFor(RunMode runMode, Difficulty level) {
        return runMode == RunMode::Endless ? endlessBest[static_cast<int>(level)] : best[static_cast<int>(level)];
    }
    const BestRun& bestFor(RunMode runMode, Difficulty level) const {
        return runMode == RunMode::Endless ? endlessBest[static_cast<int>(level)] : best[static_cast<int>(level)];
    }
};

namespace save {

// Turns data into text. The text uses plain ASCII lines like "music=0.70".
std::string serialize(const SaveData& data);

// Reads text back. It never fails: anything missing, broken or out of range keeps
// the matching value from fallback, or is clamped into its valid range.
SaveData parse(std::string_view text, const SaveData& fallback = SaveData{});

// Raises each best run in mine to the stored value when the stored one is higher. Another
// tab or the installed app may have saved a better run since this copy was loaded, and
// saving without this would take that record back. Settings are not touched: the
// settings in mine are the player's latest choice.
void mergeBestRuns(SaveData& mine, const SaveData& stored);

// One finished run, as the best-run table needs it.
struct FinishedRun {
    RunMode mode = RunMode::Story;
    Difficulty difficulty = Difficulty::Normal;
    float timeSeconds = 0.0f;
    int kills = 0;
    int level = 1;
    int bossesKilled = 0;
    bool won = false;
};

// Which figures of a run beat the record it was compared with.
struct NewBests {
    bool time = false;
    bool kills = false;
    bool level = false;
};

// Adds a run to the best run of its own mode and level, and says what it beat. Endless is
// scored by time alone, so there only a longer run counts as a new best.
NewBests recordRun(SaveData& data, const FinishedRun& run);

// The data the running game uses. Set once at startup, changed by the menus.
SaveData& current();

}  // namespace save
}  // namespace ls
