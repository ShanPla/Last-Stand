#pragma once

#include <string>
#include <string_view>

// Saved data: the player's settings and their best runs. This file has no raylib
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
};

struct SaveData {
    int version = kSaveVersion;
    Settings settings;
    BestRun best;
};

namespace save {

// Turns data into text. The text uses plain ASCII lines like "music=0.70".
std::string serialize(const SaveData& data);

// Reads text back. It never fails: anything missing, broken or out of range keeps
// the matching value from fallback, or is clamped into its valid range.
SaveData parse(std::string_view text, const SaveData& fallback = SaveData{});

// The data the running game uses. Set once at startup, changed by the menus.
SaveData& current();

}  // namespace save
}  // namespace ls
