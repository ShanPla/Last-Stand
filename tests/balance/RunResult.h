#pragma once

#include "Names.h"
#include "core/GameState.h"

#include <cstdint>
#include <vector>

namespace ls::tools {

inline constexpr float kSampleSeconds = 30.0f;  // Spacing of the zombie and HP samples.
inline constexpr float kBinSeconds = 5.0f;      // Spacing of the pressure bins.

inline constexpr int kDamageSources = static_cast<int>(EnemyType::Count) + 1;

inline const char* damageSourceName(int index) {
    static const char* const names[kDamageSources] = {"Walker",  "Runner",  "Brute",  "Spitter", "Bloater",
                                                      "Crawler", "Charger", "Boss",   "Shots/zones"};
    return names[index];
}

// What happened during one 5 second window, for spotting the calmer moments.
struct PressureBin {
    float damageTaken = 0.0f;
    float zombieSum = 0.0f;  // Zombie count summed over every step in the bin.
    int kills = 0;
    int steps = 0;
};

// Everything the report needs from one finished game.
struct RunResult {
    uint64_t seed = 0;
    RunPhase outcome = RunPhase::Playing;  // Playing means the step limit ran out.
    float endSeconds = 0.0f;
    int level = 0;
    int kills = 0;
    int peakZombies = 0;
    float damageTaken = 0.0f;
    // Damage taken by who touched the player. The last slot is shots and boss attack zones.
    float damageBySource[kDamageSources] = {};
    int favoriteWeapon = -1;  // The weapon a focus run was built around, or -1.

    bool bossReached = false;
    float bossMaxHp = 0.0f;
    float bossSpawnSeconds = 0.0f;
    bool bossKilled = false;
    float bossFightSeconds = 0.0f;  // From the boss arriving to the run ending.
    float bossHpLeft = 1.0f;        // Share of the boss's HP left when the run ended.
    float bossDamageTaken = 0.0f;   // Damage the player took while the boss was alive.
    float bossLowestHp = 100.0f;    // Lowest player HP seen during the boss fight.

    float damage[kWeaponCount] = {};
    float heldSeconds[kWeaponCount] = {};
    float drySeconds[kWeaponCount] = {};  // Time with no ammo at all, only counted for guns that use ammo.
    bool everHeld[kWeaponCount] = {};
    bool evolved[kWeaponCount] = {};
    int finalLevel[kWeaponCount] = {};
    float firstEvolutionSeconds = -1.0f;

    float allAmmoGunsDrySeconds = 0.0f;  // Every gun that uses ammo is empty.
    float fullyDrySeconds = 0.0f;        // The same, and no weapon without ammo is held either.
    float playedSeconds = 0.0f;          // Time with the game actually running.
    int cratesCollected = 0;
    int rerollsUsed = 0;
    int dashes = 0;
    float carHugSeconds = 0.0f;  // Time with a solid car this close, which would show hiding.
    int zombiesUnstuck = 0;      // Zombies that got caught among cars and had to be moved.
    int carsCrushed = 0;         // Cars the boss flattened.

    std::vector<float> sampleZombies;  // One entry every kSampleSeconds.
    std::vector<float> sampleHp;
    std::vector<float> sampleLevel;
    std::vector<PressureBin> bins;     // One entry every kBinSeconds.
};

}  // namespace ls::tools
