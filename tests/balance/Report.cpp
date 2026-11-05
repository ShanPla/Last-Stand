#include "Report.h"

#include "Stats.h"
#include "components/Enemies/Waves/Waves.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace ls::tools {

namespace {

constexpr float kBreathers[][2] = {{170.0f, 185.0f}, {345.0f, 360.0f}, {480.0f, 490.0f}};  // Gaps in the wave table.

bool won(const RunResult& run) { return run.outcome == RunPhase::Won; }
bool died(const RunResult& run) { return run.outcome == RunPhase::Dead; }

template <typename Pick>
std::vector<double> collect(const std::vector<RunResult>& runs, Pick pick) {
    std::vector<double> values;
    for (const RunResult& run : runs) values.push_back(pick(run));
    return values;
}

template <typename Filter, typename Pick>
std::vector<double> collectIf(const std::vector<RunResult>& runs, Filter filter, Pick pick) {
    std::vector<double> values;
    for (const RunResult& run : runs) {
        if (filter(run)) values.push_back(pick(run));
    }
    return values;
}

int count(const std::vector<RunResult>& runs, bool (*test)(const RunResult&)) {
    int total = 0;
    for (const RunResult& run : runs) total += test(run) ? 1 : 0;
    return total;
}

std::string percent(double part, double whole) { return format("%5.1f%%", whole > 0.0 ? 100.0 * part / whole : 0.0); }

std::string spread(const std::vector<double>& values, bool asClock) {
    if (values.empty()) return "n/a";
    const double p10 = percentile(values, 10.0);
    const double mid = median(values);
    const double p90 = percentile(values, 90.0);
    if (asClock) return format("%s / %s / %s", mmss(p10).c_str(), mmss(mid).c_str(), mmss(p90).c_str());
    return format("%.0f / %.0f / %.0f", p10, mid, p90);
}

void printOutcome(std::FILE* out, const std::vector<RunResult>& runs) {
    const int total = static_cast<int>(runs.size());
    const int wins = count(runs, won);
    const int deaths = count(runs, died);
    const int timeouts = total - wins - deaths;
    const int reached = count(runs, [](const RunResult& r) { return r.bossReached; });
    const int bossKills = count(runs, [](const RunResult& r) { return r.bossKilled; });

    std::fprintf(out, "Runs %d | wins %d (%s) | deaths %d | timeouts %d\n", total, wins, percent(wins, total).c_str(), deaths,
                 timeouts);
    const auto deathTimes = collectIf(runs, died, [](const RunResult& r) { return r.endSeconds; });
    std::fprintf(out, "Death time p10 / median / p90 (dead runs): %s   (mean %s)\n", spread(deathTimes, true).c_str(),
                 mmss(mean(deathTimes)).c_str());
    const auto levels = collect(runs, [](const RunResult& r) { return static_cast<double>(r.level); });
    const auto kills = collect(runs, [](const RunResult& r) { return static_cast<double>(r.kills); });
    std::fprintf(out, "Level reached p10 / median / p90: %s   (mean %.1f)\n", spread(levels, false).c_str(), mean(levels));
    std::fprintf(out, "Kills p10 / median / p90: %s   (mean %.0f)\n", spread(kills, false).c_str(), mean(kills));

    const auto peaks = collect(runs, [](const RunResult& r) { return static_cast<double>(r.peakZombies); });
    const int over2000 = count(runs, [](const RunResult& r) { return r.peakZombies > 2000; });
    const int atCap = count(runs, [](const RunResult& r) { return r.peakZombies >= kMaxEnemies; });
    std::fprintf(out, "Peak zombies mean %.0f, max %.0f | runs above 2000: %d | runs at the %d cap: %d\n", mean(peaks),
                 percentile(peaks, 100.0), over2000, kMaxEnemies, atCap);

    std::fprintf(out, "Boss reached: %d of %d (%s) | boss killed: %d\n", reached, total, percent(reached, total).c_str(), bossKills);
    const auto killTimes = collectIf(runs, [](const RunResult& r) { return r.bossKilled; },
                                     [](const RunResult& r) { return static_cast<double>(r.bossFightSeconds); });
    const auto bossHp = collectIf(runs, [](const RunResult& r) { return r.bossReached; },
                                  [](const RunResult& r) { return static_cast<double>(r.bossMaxHp); });
    if (!bossHp.empty()) {
        std::fprintf(out, "Boss HP set from build, p10 / median / p90: %.0f / %.0f / %.0f\n", percentile(bossHp, 10.0),
                     percentile(bossHp, 50.0), percentile(bossHp, 90.0));
    }
    if (!killTimes.empty()) {
        std::fprintf(out, "Boss kill time p10 / median / p90: %s seconds   (mean %.0f)\n", spread(killTimes, false).c_str(),
                     mean(killTimes));
    }
    if (reached > 0) {
        const auto crushed = collectIf(runs, [](const RunResult& r) { return r.bossReached; }, [](const RunResult& r) { return static_cast<double>(r.carsCrushed); });
        std::fprintf(out, "Cars the boss flattened per fight (mean): %.1f, most: %.0f\n", mean(crushed), *std::max_element(crushed.begin(), crushed.end()));
    }
    const auto lost = [](const RunResult& r) { return r.bossReached && !r.bossKilled; };
    const auto survived = collectIf(runs, lost, [](const RunResult& r) { return static_cast<double>(r.bossFightSeconds); });
    const auto hpLeft = collectIf(runs, lost, [](const RunResult& r) { return 100.0 * static_cast<double>(r.bossHpLeft); });
    if (!survived.empty()) {
        std::fprintf(out, "Boss fights lost: %zu, lasting median %.0fs, boss HP left median %.0f%%\n", survived.size(),
                     median(survived), median(hpLeft));
    }
    const auto fought = [](const RunResult& r) { return r.bossReached; };
    const auto lowest = collectIf(runs, fought, [](const RunResult& r) { return static_cast<double>(r.bossLowestHp); });
    if (!lowest.empty()) {
        int close = 0;
        for (const double hp : lowest) close += hp < 30.0 ? 1 : 0;
        const auto hits = collectIf(runs, fought, [](const RunResult& r) { return static_cast<double>(r.bossDamageTaken); });
        std::fprintf(out, "Boss fight: lowest player HP median %.0f (p10 %.0f), fights dipping under 30 HP: %d of %zu, damage taken mean %.0f\n",
                     median(lowest), percentile(lowest, 10.0), close, lowest.size(), mean(hits));
    }
    const auto taken = collect(runs, [](const RunResult& r) {
        return r.playedSeconds > 0.0f ? 60.0 * static_cast<double>(r.damageTaken / r.playedSeconds) : 0.0;
    });
    std::fprintf(out, "Damage taken per minute (mean): %.0f   rerolls used (mean): %.2f   dashes (mean): %.1f\n", mean(taken),
                 mean(collect(runs, [](const RunResult& r) { return static_cast<double>(r.rerollsUsed); })),
                 mean(collect(runs, [](const RunResult& r) { return static_cast<double>(r.dashes); })));
    std::fprintf(out, "Near a car (within 60 units): %.0f%% of play time   zombies moved after getting stuck, per game (mean): %.2f\n",
                 100.0 * mean(collect(runs, [](const RunResult& r) { return r.playedSeconds > 0.0f ? static_cast<double>(r.carHugSeconds / r.playedSeconds) : 0.0; })),
                 mean(collect(runs, [](const RunResult& r) { return static_cast<double>(r.zombiesUnstuck); })));

    double bySource[kDamageSources] = {};
    double totalTaken = 0.0;
    for (const RunResult& run : runs) {
        for (int s = 0; s < kDamageSources; ++s) {
            bySource[s] += static_cast<double>(run.damageBySource[s]);
            totalTaken += static_cast<double>(run.damageBySource[s]);
        }
    }
    std::fprintf(out, "Damage taken by source:");
    for (int s = 0; s < kDamageSources; ++s) {
        if (bySource[s] > 0.0) std::fprintf(out, " %s %.0f%%", damageSourceName(s), 100.0 * bySource[s] / totalTaken);
    }
    std::fprintf(out, "\n");
}

void printWeapons(std::FILE* out, const std::vector<RunResult>& runs) {
    std::fprintf(out, "\nWeapons (holders = runs that held it at some point)\n");
    std::fprintf(out, "%-13s %7s %8s %9s %9s %8s %9s %7s %6s %7s\n", "Weapon", "picked", "held s", "dmg/run", "dmg/held s",
                 "share", "dry s", "dry %", "avg lv", "evolved");
    double totalDamage = 0.0;
    for (const RunResult& run : runs) {
        for (int w = 0; w < kWeaponCount; ++w) totalDamage += static_cast<double>(run.damage[w]);
    }
    for (int w = 0; w < kWeaponCount; ++w) {
        int holders = 0;
        int evolved = 0;
        double damage = 0.0;
        double held = 0.0;
        double dry = 0.0;
        double levels = 0.0;
        for (const RunResult& run : runs) {
            if (!run.everHeld[w]) continue;
            ++holders;
            evolved += run.evolved[w] ? 1 : 0;
            damage += static_cast<double>(run.damage[w]);
            held += static_cast<double>(run.heldSeconds[w]);
            dry += static_cast<double>(run.drySeconds[w]);
            levels += run.finalLevel[w];
        }
        const double perHeldSecond = held > 0.0 ? damage / held : 0.0;
        const std::string picked = w == 0 ? "start" : format("%5.0f%%", 100.0 * holders / static_cast<double>(runs.size()));
        std::fprintf(out, "%-13s %7s %8.0f %9.0f %9.1f %7.1f%% %9.1f %6.1f%% %6.1f %6.0f%%\n", weaponName(w), picked.c_str(),
                     holders ? held / holders : 0.0, holders ? damage / holders : 0.0, perHeldSecond,
                     totalDamage > 0.0 ? 100.0 * damage / totalDamage : 0.0, holders ? dry / holders : 0.0,
                     held > 0.0 ? 100.0 * dry / held : 0.0, holders ? levels / holders : 0.0,
                     holders ? 100.0 * evolved / holders : 0.0);
    }
}

void printAmmo(std::FILE* out, const std::vector<RunResult>& runs) {
    const auto played = [](const RunResult& r) { return static_cast<double>(r.playedSeconds); };
    const double totalPlayed = mean(collect(runs, played)) * static_cast<double>(runs.size());
    double allDry = 0.0;
    double fullyDry = 0.0;
    double crates = 0.0;
    double availableCrates = 0.0;
    for (const RunResult& run : runs) {
        allDry += static_cast<double>(run.allAmmoGunsDrySeconds);
        fullyDry += static_cast<double>(run.fullyDrySeconds);
        crates += run.cratesCollected;
        availableCrates += run.kills / balance::kKillsPerAmmoCrate;
    }
    const double n = static_cast<double>(runs.size());
    std::fprintf(out, "\nAmmo\n");
    std::fprintf(out, "Every ammo gun empty (Rifle's weak free shots still fire): %.1f s per run, %.1f%% of play time\n",
                 allDry / n, totalPlayed > 0 ? 100.0 * allDry / totalPlayed : 0.0);
    std::fprintf(out, "... and no ammo-free weapon either: %.1f s per run, %.1f%% of play time\n", fullyDry / n,
                 totalPlayed > 0 ? 100.0 * fullyDry / totalPlayed : 0.0);
    std::fprintf(out, "Crates collected: %.1f per run (about %.1f dropped by kills), %.1f per minute of play\n", crates / n,
                 availableCrates / n, totalPlayed > 0 ? 60.0 * crates / totalPlayed : 0.0);
}

// A run is over at its end time, whether it died, won or hit the step limit. The small
// tolerance is because the end time is a tick count times a float step.
bool endedBy(const RunResult& run, double seconds) { return static_cast<double>(run.endSeconds) <= seconds + 0.01; }

// What the runs looked like at one sample time. Runs that already ended have no sample
// to give, so zombies, HP and level are taken from the runs still being played. Winners
// are counted separately, so a win never looks like a death.
struct SampleRow {
    int notDead = 0;  // Not dead yet. A run that won counts as not dead.
    int won = 0;
    std::vector<double> zombies;
    std::vector<double> hp;
    std::vector<double> level;
};

SampleRow sampleRow(const std::vector<RunResult>& runs, size_t s) {
    const double t = static_cast<double>(s + 1) * static_cast<double>(kSampleSeconds);
    SampleRow row;
    for (const RunResult& run : runs) {
        const bool ended = endedBy(run, t);
        if (!(ended && died(run))) ++row.notDead;
        if (ended && won(run)) ++row.won;
        if (ended || s >= run.sampleZombies.size()) continue;
        row.zombies.push_back(static_cast<double>(run.sampleZombies[s]));
        row.hp.push_back(static_cast<double>(run.sampleHp[s]));
        row.level.push_back(static_cast<double>(run.sampleLevel[s]));
    }
    return row;
}

size_t longestSeries(const std::vector<RunResult>& runs) {
    size_t longest = 0;
    for (const RunResult& run : runs) longest = std::max(longest, run.sampleZombies.size());
    return longest;
}

void printTimeSeries(std::FILE* out, const std::vector<RunResult>& runs) {
    std::fprintf(out, "\nEvery %.0f s. alive = runs not dead yet (a win counts as alive), won = runs already won.\n", kSampleSeconds);
    std::fprintf(out, "zombies, HP and level are the mean over the runs still being played (HP p10 is their 10th percentile).\n");
    std::fprintf(out, "%-7s %7s %7s %9s %9s %9s %7s\n", "time", "alive", "won", "zombies", "HP mean", "HP p10", "level");
    const double total = static_cast<double>(runs.size());
    for (size_t s = 0; s < longestSeries(runs); ++s) {
        const SampleRow row = sampleRow(runs, s);
        if (row.zombies.empty()) {
            std::fprintf(out, "%-7s %6.0f%% %6.0f%% %9s %9s %9s %7s\n", mmss(static_cast<double>(s + 1) * kSampleSeconds).c_str(),
                         100.0 * row.notDead / total, 100.0 * row.won / total, "-", "-", "-", "-");
            continue;
        }
        std::fprintf(out, "%-7s %6.0f%% %6.0f%% %9.0f %9.0f %9.0f %7.1f\n", mmss(static_cast<double>(s + 1) * kSampleSeconds).c_str(),
                     100.0 * row.notDead / total, 100.0 * row.won / total, mean(row.zombies), mean(row.hp), percentile(row.hp, 10.0),
                     mean(row.level));
    }
}

void printEvolutions(std::FILE* out, const std::vector<RunResult>& runs) {
    int withEvolution = 0;
    int winsWith = 0;
    int winsWithout = 0;
    std::vector<double> firstTimes;
    for (const RunResult& run : runs) {
        const bool any = run.firstEvolutionSeconds >= 0.0f;
        withEvolution += any ? 1 : 0;
        if (any) firstTimes.push_back(static_cast<double>(run.firstEvolutionSeconds));
        if (won(run)) (any ? winsWith : winsWithout)++;
    }
    const int total = static_cast<int>(runs.size());
    std::fprintf(out, "\nEvolutions: %d of %d runs got at least one (%s); first one at median %s\n", withEvolution, total,
                 percent(withEvolution, total).c_str(), firstTimes.empty() ? "n/a" : mmss(median(firstTimes)).c_str());
    std::fprintf(out, "Win rate with an evolution %s, without %s\n", percent(winsWith, withEvolution).c_str(),
                 percent(winsWithout, total - withEvolution).c_str());
}

void printCorrelation(std::FILE* out, const std::vector<RunResult>& runs) {
    std::fprintf(out, "\nPer-weapon correlation with the result (held at some point vs. not)\n");
    std::fprintf(out, "%-13s %6s %9s %10s %9s %11s\n", "Weapon", "held", "win held", "win other", "r(win)", "r(survival)");
    const auto winFlags = collect(runs, [](const RunResult& r) { return won(r) ? 1.0 : 0.0; });
    const auto endTimes = collect(runs, [](const RunResult& r) { return static_cast<double>(r.endSeconds); });
    for (int w = 0; w < kWeaponCount; ++w) {
        std::vector<double> heldFlags;
        int held = 0;
        int winsHeld = 0;
        int winsOther = 0;
        for (const RunResult& run : runs) {
            heldFlags.push_back(run.everHeld[w] ? 1.0 : 0.0);
            if (run.everHeld[w]) {
                ++held;
                winsHeld += won(run) ? 1 : 0;
            } else {
                winsOther += won(run) ? 1 : 0;
            }
        }
        const double rWin = correlation(heldFlags, winFlags);
        const double rTime = correlation(heldFlags, endTimes);
        const auto cell = [](double r) { return std::isnan(r) ? std::string("     n/a") : format("%+8.2f", r); };
        std::fprintf(out, "%-13s %6d %9s %10s %9s %11s\n", weaponName(w), held, percent(winsHeld, held).c_str(),
                     percent(winsOther, static_cast<double>(runs.size()) - held).c_str(), cell(rWin).c_str(), cell(rTime).c_str());
    }
}

// Zombies on the map and damage taken in one 5 second bin, over all runs that reached it.
struct BinAverage {
    double zombies = 0.0;
    double damagePerSecond = 0.0;
    int runs = 0;
};

BinAverage averageBin(const std::vector<RunResult>& runs, size_t bin) {
    double zombieSum = 0.0;
    double damage = 0.0;
    double steps = 0.0;
    BinAverage result;
    for (const RunResult& run : runs) {
        if (bin >= run.bins.size() || run.bins[bin].steps < static_cast<int>(kBinSeconds / kDt) / 2) continue;
        zombieSum += static_cast<double>(run.bins[bin].zombieSum);
        damage += static_cast<double>(run.bins[bin].damageTaken);
        steps += run.bins[bin].steps;
        ++result.runs;
    }
    if (steps > 0.0) {
        result.zombies = zombieSum / steps;
        result.damagePerSecond = damage / (steps * static_cast<double>(kDt));
    }
    return result;
}

// What the spawner makes at one moment on this difficulty: enemies per second, and HP per
// second after scaling. The spawner multiplies the table's rates by the level's spawn rate,
// and zombie HP by the level's HP, so this must as well to match the games it sits beside.
void waveRates(float seconds, Difficulty difficulty, double& enemiesPerSecond, double& hpPerSecond) {
    enemiesPerSecond = 0.0;
    hpPerSecond = 0.0;
    const balance::DifficultyTuning& tuning = balance::difficultyTuning(difficulty);
    const int rows = static_cast<int>(std::size(balance::kWaves));
    for (int i = 0; i < rows; ++i) {
        const double spawned =
            static_cast<double>(waves::rowRate(i, seconds)) * static_cast<double>(tuning.spawnRate) * balance::kWaves[i].groupSize;
        enemiesPerSecond += spawned;
        hpPerSecond += spawned * static_cast<double>(balance::kEnemyStats[static_cast<int>(balance::kWaves[i].type)].hp) *
                       static_cast<double>(balance::enemyHpScale(seconds)) * static_cast<double>(tuning.enemyHp);
    }
}

}  // namespace

void printRunList(std::FILE* out, const ProfileRuns& group) {
    std::fprintf(out, "\nRuns of %s\n%-7s %-8s %7s %6s %7s %7s  %s\n", group.profile->name, "seed", "result", "time", "level",
                 "kills", "peak", "worst damage source");
    for (const RunResult& run : group.runs) {
        int worst = 0;
        for (int s = 1; s < kDamageSources; ++s) {
            if (run.damageBySource[s] > run.damageBySource[worst]) worst = s;
        }
        const char* outcome = won(run) ? "won" : (died(run) ? "dead" : "timeout");
        std::fprintf(out, "%-7llu %-8s %7s %6d %7d %7d  %s\n", static_cast<unsigned long long>(run.seed), outcome,
                     mmss(static_cast<double>(run.endSeconds)).c_str(), run.level, run.kills, run.peakZombies,
                     damageSourceName(worst));
    }
}

void printProfileReport(std::FILE* out, const ProfileRuns& group, bool withCorrelation) {
    std::fprintf(out, "\n==============================================================================\n");
    std::fprintf(out, " Profile: %s\n", group.profile->name);
    std::fprintf(out, "==============================================================================\n");
    printOutcome(out, group.runs);
    printWeapons(out, group.runs);
    printAmmo(out, group.runs);
    printEvolutions(out, group.runs);
    printTimeSeries(out, group.runs);
    if (withCorrelation) printCorrelation(out, group.runs);
}

void printFocusTable(std::FILE* out, const BotProfile& profile, const std::vector<std::vector<RunResult>>& byWeapon) {
    std::fprintf(out, "\n==============================================================================\n");
    std::fprintf(out, " Focus runs, %s bot: each row starts with the Rifle plus that weapon and builds toward it\n", profile.name);
    std::fprintf(out, "==============================================================================\n");
    std::fprintf(out, "%-13s %5s %6s %8s %8s %8s %7s %8s %9s %7s\n", "Favorite", "runs", "win", "surv p10", "surv med", "surv p90",
                 "level", "evolved", "its share", "dry s");
    for (int w = 0; w < kWeaponCount; ++w) {
        const std::vector<RunResult>& runs = byWeapon[static_cast<size_t>(w)];
        if (runs.empty()) continue;
        const int wins = count(runs, won);
        const auto ends = collect(runs, [](const RunResult& r) { return static_cast<double>(r.endSeconds); });
        double mine = 0.0;
        double all = 0.0;
        double dry = 0.0;
        int evolved = 0;
        for (const RunResult& run : runs) {
            mine += static_cast<double>(run.damage[w]);
            dry += static_cast<double>(run.drySeconds[w]);
            evolved += run.evolved[w] ? 1 : 0;
            for (int i = 0; i < kWeaponCount; ++i) all += static_cast<double>(run.damage[i]);
        }
        const double n = static_cast<double>(runs.size());
        std::fprintf(out, "%-13s %5zu %5.0f%% %8s %8s %8s %7.1f %7.0f%% %8.1f%% %7.1f\n", weaponName(w), runs.size(),
                     100.0 * wins / n, mmss(percentile(ends, 10.0)).c_str(), mmss(median(ends)).c_str(),
                     mmss(percentile(ends, 90.0)).c_str(),
                     mean(collect(runs, [](const RunResult& r) { return static_cast<double>(r.level); })), 100.0 * evolved / n,
                     all > 0.0 ? 100.0 * mine / all : 0.0, dry / n);
    }
    std::fprintf(out, "(surv = run length: the time of death, or of the win. Rifle row has no extra weapon.)\n");
}

void printDifficultyHeader(std::FILE* out, Difficulty difficulty) {
    const balance::DifficultyTuning& tuning = balance::difficultyTuning(difficulty);
    std::fprintf(out, "Difficulty: %s (zombie HP x%.2f, zombie damage x%.2f, spawn rate x%.2f)\n", difficultyName(difficulty),
                 static_cast<double>(tuning.enemyHp), static_cast<double>(tuning.enemyDamage), static_cast<double>(tuning.spawnRate));
}

void printWaveAnalysis(std::FILE* out, Difficulty difficulty) {
    std::fprintf(out, "\nWave table: what spawns (before kills), every 30 s, on %s\n", difficultyName(difficulty));
    std::fprintf(out, "%-7s %10s %12s %10s\n", "time", "enemies/s", "HP to chew/s", "HP scale");
    const double hpMultiplier = static_cast<double>(balance::difficultyTuning(difficulty).enemyHp);
    for (int t = 0; t <= 600; t += 30) {
        double enemies = 0.0;
        double hp = 0.0;
        waveRates(static_cast<float>(t) + 0.5f, difficulty, enemies, hp);
        std::fprintf(out, "%-7s %10.1f %12.0f %10.2f\n", mmss(t).c_str(), enemies, hp,
                     static_cast<double>(balance::enemyHpScale(static_cast<float>(t))) * hpMultiplier);
    }
}

void printBreatherTable(std::FILE* out, const std::vector<ProfileRuns>& groups, Difficulty difficulty) {
    std::fprintf(out, "\nBreathers on %s: wave table against measured play (5 s bins, 30 s either side of each gap)\n",
                 difficultyName(difficulty));
    std::fprintf(out, "%-7s %9s %10s", "time", "spawn/s", "HP/s");
    for (const ProfileRuns& group : groups) std::fprintf(out, " | %-7s zomb  dmg/s", group.profile->name);
    std::fprintf(out, "\n");
    for (const auto& breather : kBreathers) {
        const int firstBin = static_cast<int>((breather[0] - 30.0f) / kBinSeconds);
        const int lastBin = static_cast<int>((breather[1] + 30.0f) / kBinSeconds);
        for (int bin = firstBin; bin <= lastBin; ++bin) {
            const float mid = (static_cast<float>(bin) + 0.5f) * kBinSeconds;
            double enemies = 0.0;
            double hp = 0.0;
            waveRates(mid, difficulty, enemies, hp);
            const bool inGap = mid >= breather[0] && mid <= breather[1];
            std::fprintf(out, "%-7s %9.1f %10.0f", (mmss(bin * kBinSeconds) + (inGap ? " *" : "")).c_str(), enemies, hp);
            for (const ProfileRuns& group : groups) {
                const BinAverage average = averageBin(group.runs, static_cast<size_t>(bin));
                if (average.runs == 0) std::fprintf(out, " | %-7s %4s  %5s", "", "-", "-");
                else std::fprintf(out, " | %-7s %4.0f  %5.1f", "", average.zombies, average.damagePerSecond);
            }
            std::fprintf(out, "\n");
        }
        std::fprintf(out, "\n");
    }
    std::fprintf(out, "(* marks the gap itself)\n");
}

void writeCsv(const std::string& folder, const std::vector<ProfileRuns>& groups, Difficulty difficulty) {
    const char* level = difficultyName(difficulty);  // First column of every file, so files of different levels never mix.
    std::filesystem::create_directories(folder);

    std::ofstream runsFile(folder + "/runs.csv");
    runsFile << "difficulty,profile,seed,outcome,end_seconds,level,kills,peak_zombies,damage_taken,boss_reached,boss_killed,"
                "boss_fight_seconds,boss_hp_left,crates,all_ammo_dry_seconds,fully_dry_seconds,first_evolution_seconds";
    for (int w = 0; w < kWeaponCount; ++w) runsFile << "," << weaponName(w) << "_damage," << weaponName(w) << "_held_s";
    runsFile << "\n";
    for (const ProfileRuns& group : groups) {
        for (const RunResult& run : group.runs) {
            const char* outcome = won(run) ? "won" : (died(run) ? "dead" : "timeout");
            runsFile << level << "," << group.profile->name << "," << run.seed << "," << outcome << "," << run.endSeconds << "," << run.level
                     << "," << run.kills << "," << run.peakZombies << "," << run.damageTaken << "," << run.bossReached << ","
                     << run.bossKilled << "," << run.bossFightSeconds << "," << run.bossHpLeft << "," << run.cratesCollected
                     << "," << run.allAmmoGunsDrySeconds << "," << run.fullyDrySeconds << "," << run.firstEvolutionSeconds;
            for (int w = 0; w < kWeaponCount; ++w) runsFile << "," << run.damage[w] << "," << run.heldSeconds[w];
            runsFile << "\n";
        }
    }

    std::ofstream seriesFile(folder + "/timeseries.csv");
    // alive_runs counts runs not dead yet, so a win stays in it. The means are over playing_runs.
    seriesFile << "difficulty,profile,seconds,alive_runs,won_runs,playing_runs,mean_zombies,mean_hp,mean_level\n";
    for (const ProfileRuns& group : groups) {
        for (size_t s = 0; s < longestSeries(group.runs); ++s) {
            const SampleRow row = sampleRow(group.runs, s);
            seriesFile << level << "," << group.profile->name << "," << (s + 1) * kSampleSeconds << "," << row.notDead << ","
                       << row.won << "," << row.zombies.size() << "," << mean(row.zombies) << "," << mean(row.hp) << ","
                       << mean(row.level) << "\n";
        }
    }

    std::ofstream binsFile(folder + "/pressure_bins.csv");
    binsFile << "difficulty,profile,seconds,runs,mean_zombies,damage_per_second,wave_enemies_per_second,wave_hp_per_second\n";
    for (const ProfileRuns& group : groups) {
        size_t longest = 0;
        for (const RunResult& run : group.runs) longest = std::max(longest, run.bins.size());
        for (size_t bin = 0; bin < longest; ++bin) {
            const BinAverage average = averageBin(group.runs, bin);
            double enemies = 0.0;
            double hp = 0.0;
            waveRates((static_cast<float>(bin) + 0.5f) * kBinSeconds, difficulty, enemies, hp);
            binsFile << level << "," << group.profile->name << "," << bin * kBinSeconds << "," << average.runs << "," << average.zombies << ","
                     << average.damagePerSecond << "," << enemies << "," << hp << "\n";
        }
    }
}

}  // namespace ls::tools
