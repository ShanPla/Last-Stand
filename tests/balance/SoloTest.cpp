#include "SoloTest.h"

#include "GameRunner.h"

#include "Names.h"
#include "Parallel.h"
#include "Stats.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <filesystem>
#include <fstream>
#include <memory>

namespace ls::tools {

namespace {

// Level 1 to 5, then the evolved level 5. An evolved weapon has no passive here.
constexpr int kVariants = 6;
constexpr int kEvolvedVariant = 5;
constexpr int kSeedsPerCell = 3;
constexpr int kSparseCrowd = 40;
constexpr int kDenseCrowd = 150;
constexpr int kKillCrowd = 100;
constexpr int kStartTick = 150 * 60;        // Tests start with the zombie HP scale of 2:30. The clock then runs on.
constexpr int kWarmUpSteps = 12 * 60;       // Time for the crowd to walk in and gather around the soldier.
constexpr int kMeasureSteps = 30 * 60;      // Half a minute of shooting for the damage tests.
constexpr int kKillSteps = 60 * 60;         // One minute for the kill test.
constexpr int kDryLimitSteps = 180 * 60;    // Stop waiting for an empty gun after three minutes.
constexpr float kToughHp = 1.0e7f;          // A zombie that never dies, so damage is never limited by supply.

struct SoloCell {
    double dpsSparse = 0.0;       // Damage per second against 40 tough zombies.
    double dpsDense = 0.0;        // The same against 150.
    double killsPerMinute = 0.0;  // Normal zombies, which die and are replaced.
    double secondsToEmpty = 0.0;  // How long one full load lasts. Zero when the weapon uses no ammo.
    double damagePerLoad = 0.0;
};

bool hasEvolution(int weapon) {
    for (const balance::Evolution& evolution : balance::kEvolutions) {
        if (static_cast<int>(evolution.weapon) == weapon) return true;
    }
    return false;
}

const char* variantName(int variant) {
    static const char* const names[kVariants] = {"Lv1", "Lv2", "Lv3", "Lv4", "Lv5", "Evolved"};
    return names[variant];
}

// A standing, unkillable soldier with one weapon, in the middle of a crowd that is refilled every step.
std::unique_ptr<GameState> makeArena(WeaponId weapon, int variant, int crowd, uint64_t seed, Difficulty difficulty) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, difficulty, gameMap());
    state->firstPlayer().weaponCount = 0;
    state->firstPlayer().xpToNext = 1 << 30;  // No level-up screens.
    weapons::grant(*state, 0, weapon);
    WeaponSlot& slot = state->firstPlayer().weapons[0];
    slot.level = static_cast<uint8_t>(variant == kEvolvedVariant ? kMaxLevelPerItem : variant + 1);
    slot.evolved = variant == kEvolvedVariant;
    weapons::refill(state->firstPlayer(), slot);
    state->run.tick = kStartTick;
    sim::setStressTargets(*state, crowd, 0);
    return state;
}

void makeCrowdTough(GameState& state) {
    for (Enemy& enemy : state.enemies) {
        enemy.hp = kToughHp;
        enemy.maxHp = kToughHp;
    }
}

// Runs the arena. With tough zombies and ammo supplied, this is the weapon's real output.
double measureDps(WeaponId weapon, int variant, int crowd, uint64_t seed, Difficulty difficulty) {
    auto state = makeArena(weapon, variant, crowd, seed, difficulty);
    WeaponSlot& slot = state->firstPlayer().weapons[0];
    const int id = static_cast<int>(weapon);
    float before = 0.0f;
    for (int step = 0; step < kWarmUpSteps + kMeasureSteps; ++step) {
        slot.reserve = weapons::effectiveStats(state->firstPlayer(), slot).maxReserve;
        sim::step(*state, InputFrame{});
        state->events.clear();
        makeCrowdTough(*state);
        if (step == kWarmUpSteps - 1) before = state->run.damageByWeapon[id];
    }
    return static_cast<double>(state->run.damageByWeapon[id] - before) / (kMeasureSteps * static_cast<double>(kDt));
}

double measureKillsPerMinute(WeaponId weapon, int variant, uint64_t seed, Difficulty difficulty) {
    auto state = makeArena(weapon, variant, kKillCrowd, seed, difficulty);
    WeaponSlot& slot = state->firstPlayer().weapons[0];
    for (int step = 0; step < kKillSteps; ++step) {
        slot.reserve = weapons::effectiveStats(state->firstPlayer(), slot).maxReserve;
        sim::step(*state, InputFrame{});
        state->events.clear();
        // Drops are not the weapon's doing. A bomb picked up from a corpse would kill the whole
        // crowd at once, and every one of those kills would be credited to this weapon.
        state->pickups.clear();
    }
    return static_cast<double>(state->run.kills) / (kKillSteps * static_cast<double>(kDt) / 60.0);
}

// One full load and no crates, against the tough dense crowd, until the gun is empty.
void measureLoad(WeaponId weapon, int variant, uint64_t seed, Difficulty difficulty, SoloCell& cell) {
    auto state = makeArena(weapon, variant, kDenseCrowd, seed, difficulty);
    const WeaponSlot& slot = state->firstPlayer().weapons[0];
    if (weapons::effectiveStats(state->firstPlayer(), slot).magazine <= 0) return;
    const int id = static_cast<int>(weapon);
    int step = 0;
    int emptyAt = -1;
    for (; step < kDryLimitSteps; ++step) {
        sim::step(*state, InputFrame{});
        state->events.clear();
        makeCrowdTough(*state);
        if (slot.magazine <= 0 && slot.reserve <= 0 && slot.reloadLeft <= 0.0f) {
            emptyAt = step;
            break;
        }
    }
    cell.secondsToEmpty = static_cast<double>(emptyAt >= 0 ? emptyAt : step) * static_cast<double>(kDt);
    cell.damagePerLoad = static_cast<double>(state->run.damageByWeapon[id]);
}

SoloCell measureCell(WeaponId weapon, int variant, uint64_t seed, Difficulty difficulty) {
    SoloCell cell;
    cell.dpsSparse = measureDps(weapon, variant, kSparseCrowd, seed, difficulty);
    cell.dpsDense = measureDps(weapon, variant, kDenseCrowd, seed, difficulty);
    cell.killsPerMinute = measureKillsPerMinute(weapon, variant, seed, difficulty);
    measureLoad(weapon, variant, seed, difficulty, cell);
    return cell;
}

}  // namespace

void runSoloTest(std::FILE* out, int threadCount, const std::string& csvFolder, Difficulty difficulty) {
    // One slot per weapon, variant and seed, so threads never share a slot.
    std::vector<SoloCell> cells(static_cast<size_t>(kWeaponCount) * kVariants * kSeedsPerCell);
    parallelFor(cells.size(), threadCount, [&](size_t i) {
        const int seed = static_cast<int>(i % kSeedsPerCell);
        const int variant = static_cast<int>((i / kSeedsPerCell) % kVariants);
        const int weapon = static_cast<int>(i / (kSeedsPerCell * kVariants));
        cells[i] = measureCell(static_cast<WeaponId>(weapon), variant, 1000 + static_cast<uint64_t>(seed), difficulty);
    });

    const auto average = [&](int weapon, int variant, double SoloCell::*field) {
        double sum = 0.0;
        for (int s = 0; s < kSeedsPerCell; ++s) sum += cells[static_cast<size_t>((weapon * kVariants + variant) * kSeedsPerCell + s)].*field;
        return sum / kSeedsPerCell;
    };

    // The kill test lasts a minute and the clock runs, so zombie HP rises while it is measured.
    const double hpMultiplier = static_cast<double>(balance::difficultyTuning(difficulty).enemyHp);
    const double hpAtStart = static_cast<double>(balance::enemyHpScale(kStartTick * kDt)) * hpMultiplier;
    const double hpAtEnd = static_cast<double>(balance::enemyHpScale((kStartTick + kKillSteps) * kDt)) * hpMultiplier;

    std::fprintf(out, "\n==============================================================================\n");
    std::fprintf(out, " Solo weapon test: one standing, unkillable soldier with one weapon. Difficulty: %s.\n", difficultyName(difficulty));
    std::fprintf(out, " Averaged over %d seeds. Evolved = Lv5 plus evolution, no passives.\n", kSeedsPerCell);
    std::fprintf(out, " Damage tests use zombies that never die, so supply never limits a weapon.\n");
    std::fprintf(out, " Kill test: drops are removed, so only the weapon kills. Zombie HP starts at x%.2f (the 2:30 scale)\n", hpAtStart);
    std::fprintf(out, " and reaches x%.2f by the last second of the minute.\n", hpAtEnd);
    std::fprintf(out, "==============================================================================\n");

    const auto printTable = [&](const std::string& title, double SoloCell::*field, const char* fmt) {
        std::fprintf(out, "\n%s\n%-13s", title.c_str(), "Weapon");
        for (int v = 0; v < kVariants; ++v) std::fprintf(out, " %8s", variantName(v));
        std::fprintf(out, "\n");
        for (int w = 0; w < kWeaponCount; ++w) {
            std::fprintf(out, "%-13s", weaponName(w));
            for (int v = 0; v < kVariants; ++v) {
                if (v == kEvolvedVariant && !hasEvolution(w)) std::fprintf(out, " %8s", "-");
                else std::fprintf(out, fmt, average(w, v, field));
            }
            std::fprintf(out, "\n");
        }
    };
    printTable(format("Damage per second, %d zombies around the soldier", kSparseCrowd), &SoloCell::dpsSparse, " %8.0f");
    printTable(format("Damage per second, %d zombies around the soldier", kDenseCrowd), &SoloCell::dpsDense, " %8.0f");
    printTable(format("Kills per minute, normal zombies (HP x%.2f rising to x%.2f) replaced as they die (%d around)", hpAtStart, hpAtEnd,
                      kKillCrowd),
               &SoloCell::killsPerMinute, " %8.0f");

    std::fprintf(out, "\nOne full load, no crates, %d tough zombies around: seconds until empty / damage dealt (- = no ammo)\n",
                 kDenseCrowd);
    std::fprintf(out, "%-13s", "Weapon");
    for (int v = 0; v < kVariants; ++v) std::fprintf(out, " %14s", variantName(v));
    std::fprintf(out, "\n");
    for (int w = 0; w < kWeaponCount; ++w) {
        std::fprintf(out, "%-13s", weaponName(w));
        for (int v = 0; v < kVariants; ++v) {
            const bool skip = (v == kEvolvedVariant && !hasEvolution(w)) || average(w, v, &SoloCell::damagePerLoad) <= 0.0;
            if (skip) {
                std::fprintf(out, " %14s", "-");
            } else {
                std::fprintf(out, " %6.0fs/%6.0f", average(w, v, &SoloCell::secondsToEmpty), average(w, v, &SoloCell::damagePerLoad));
            }
        }
        std::fprintf(out, "\n");
    }

    if (csvFolder.empty()) return;
    std::filesystem::create_directories(csvFolder);
    std::ofstream file(csvFolder + "/solo.csv");
    file << "difficulty,weapon,variant,dps_sparse,dps_dense,kills_per_minute,seconds_to_empty,damage_per_load\n";
    for (int w = 0; w < kWeaponCount; ++w) {
        for (int v = 0; v < kVariants; ++v) {
            if (v == kEvolvedVariant && !hasEvolution(w)) continue;
            file << difficultyName(difficulty) << "," << weaponName(w) << "," << variantName(v) << "," << average(w, v, &SoloCell::dpsSparse) << ","
                 << average(w, v, &SoloCell::dpsDense) << "," << average(w, v, &SoloCell::killsPerMinute) << ","
                 << average(w, v, &SoloCell::secondsToEmpty) << "," << average(w, v, &SoloCell::damagePerLoad) << "\n";
        }
    }
}

}  // namespace ls::tools
