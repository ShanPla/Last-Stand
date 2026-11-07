#include "components/Enemies/Waves/Waves.h"

#include "components/Enemies/Enemies.h"
#include "components/Pickups/Pickups.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ls::waves {

namespace {

Vec2 spawnPoint(GameState& state, float extraDistance = 0.0f) {
    return state.player.pos + fromAngle(state.rng.range(0.0f, kTau)) * (balance::kSpawnRadius + extraDistance);
}

}  // namespace

float bossHpForBuild(const GameState& state) {
    const float strength = weapons::estimatedBossDps(state) / balance::kBossReferenceDps;
    const float hp = balance::kBossHpAtReferenceDps * std::pow(strength, balance::kBossHpScalePower);
    return clampf(hp, balance::kBossHpMin, balance::kBossHpMax) * balance::difficultyTuning(state.run.difficulty).enemyHp;
}

float rowRate(int rowIndex, float seconds) {
    const balance::WaveRow& row = balance::kWaves[rowIndex];
    if (seconds < row.startSec || seconds >= row.endSec) return 0.0f;
    const float t = (seconds - row.startSec) / (row.endSec - row.startSec);
    return lerpf(row.startRate, row.endRate, t);
}

namespace {

bool endless(const RunState& run) { return run.mode == RunMode::Endless; }

uint32_t loopTicks() {
    return static_cast<uint32_t>(std::lround((balance::kEndlessLoopEndSec - balance::kEndlessLoopStartSec) / kDt));
}

// Steps since the current pass of the Endless loop began.
uint32_t intoLoop(const RunState& run) { return (run.tick - run.lastBossKillTick) % loopTicks(); }

// The moment of the wave table to play now. Story reads the table by the clock, and so
// does Endless until its first boss falls. After that, Endless replays the loop stretch.
float tableSeconds(const GameState& state) {
    const RunState& run = state.run;
    if (!endless(run) || run.bossesKilled == 0) return state.timeSeconds();
    return balance::kEndlessLoopStartSec + static_cast<float>(intoLoop(run)) * kDt;
}

// Story's boss comes once, at 10:00. Endless's comes at every 10 minute mark, but never
// sooner than a short break after the last one died, so a slow kill still gets a breather.
bool bossDue(const GameState& state) {
    const RunState& run = state.run;
    if (run.bossSpawned) return false;
    const float seconds = state.timeSeconds();
    if (!endless(run)) return seconds >= balance::kRunSeconds;
    const float mark = balance::kEndlessBossEverySeconds * static_cast<float>(run.bossesKilled + 1);
    const float afterBreak = run.bossesKilled == 0 ? 0.0f : static_cast<float>(run.lastBossKillTick) * kDt + balance::kEndlessBreakSeconds;
    return seconds >= std::max(mark, afterBreak);
}

// Endless zombies keep getting tougher after 10:00, where Story's growth stops. Spawn
// gives every zombie its Story strength, and this adds the Endless share on top.
Enemy* spawnWave(GameState& state, EnemyType type, Vec2 pos, bool elite) {
    Enemy* enemy = enemies::spawn(state, type, pos, elite);
    if (enemy && endless(state.run)) {
        const float seconds = state.timeSeconds();
        enemy->maxHp *= balance::endlessHpGrowth(seconds);
        enemy->hp = enemy->maxHp;
        enemy->contactDamage *= balance::endlessDamageGrowth(seconds);
    }
    return enemy;
}

void spawnBoss(GameState& state) {
    RunState& run = state.run;
    // The horde clears when the boss arrives. Its attacks must be readable, and
    // shots must reach it rather than being soaked up by leftover zombies.
    state.enemies.clear();
    pushEvent(state, EventType::BombBlast, state.player.pos, balance::kBombRadius);
    const Vec2 pos = spawnPoint(state, balance::kBossSpawnExtra);
    if (Enemy* boss = enemies::spawn(state, EnemyType::Boss, pos, false)) {
        boss->maxHp = bossHpForBuild(state);
        if (endless(run)) {
            // Each return is tougher and fights with a harder move set, which also sets its first wait.
            boss->maxHp *= balance::endlessBossHpGrowth(run.bossesKilled);
            boss->timer = balance::bossMovesFor(run).attackInterval[0];
        }
        boss->hp = boss->maxHp;
        run.bossSpawned = true;
        run.bossPhase = 0;
        run.bossHpFraction = 1.0f;
        pushEvent(state, EventType::BossSpawned, pos);
    }
}

}  // namespace

void update(GameState& state) {
    RunState& run = state.run;
    const float seconds = state.timeSeconds();
    if (run.debugEnemyTarget > 0) return;

    // Story's table simply runs out at 10:00. Endless pauses it while a boss is alive instead.
    const bool tableRuns = !(endless(run) && run.bossSpawned);
    const float tableTime = tableSeconds(state);
    const float spawnRate = balance::difficultyTuning(run.difficulty).spawnRate;
    const float rateScale = endless(run) ? spawnRate * balance::endlessSpawnGrowth(seconds) : spawnRate;
    const int rowCount = static_cast<int>(std::size(balance::kWaves));
    for (int i = 0; tableRuns && i < rowCount; ++i) {
        const balance::WaveRow& row = balance::kWaves[i];
        run.waveAccumulators[i] += rowRate(i, tableTime) * rateScale * kDt;
        while (run.waveAccumulators[i] >= 1.0f) {
            run.waveAccumulators[i] -= 1.0f;
            // A group spawns together at one point, so it arrives as a pack.
            const Vec2 groupCenter = spawnPoint(state);
            for (int member = 0; member < row.groupSize; ++member) {
                const Vec2 jitter = fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(0.0f, balance::kWaveGroupSpread);
                spawnWave(state, row.type, groupCenter + jitter, false);
            }
        }
    }

    // Each new pass of the Endless loop brings its elites again.
    if (endless(run) && run.bossesKilled > 0 && intoLoop(run) == 0) run.nextEliteEvent = balance::firstLoopElite();
    const int eliteCount = static_cast<int>(std::size(balance::kEliteEvents));
    while (tableRuns && run.nextEliteEvent < eliteCount && tableTime >= balance::kEliteEvents[run.nextEliteEvent].timeSec) {
        spawnWave(state, balance::kEliteEvents[run.nextEliteEvent].type, spawnPoint(state), true);
        run.nextEliteEvent++;
    }

    if (bossDue(state)) spawnBoss(state);

    if (run.bossSpawned) {
        // A few Walkers keep XP coming during the fight. The wave table never uses
        // the last accumulator, because it has fewer rows than the array has slots.
        float& adds = run.waveAccumulators[std::size(run.waveAccumulators) - 1];
        adds += balance::kBossAddsPerSecond * kDt;
        while (adds >= 1.0f) {
            adds -= 1.0f;
            spawnWave(state, EnemyType::Walker, spawnPoint(state), false);
        }
    }

    // Crates normally come from kills, and a player with empty guns gets few kills.
    // Timed crates mean ammo keeps arriving anyway, faster during the boss fight, so
    // no run can become unwinnable. Stress tests skip this like the rest of the waves.
    const float crateSeconds = run.bossSpawned ? balance::kBossAmmoCrateSeconds : balance::kTimedAmmoCrateSeconds;
    const float crateDistance = run.bossSpawned ? balance::kBossAmmoCrateDistance : balance::kTimedAmmoCrateDistance;
    // Rounded, because the division can land just under a whole number of steps.
    if (run.tick % static_cast<uint32_t>(std::lround(crateSeconds / kDt)) == 0) {
        const Vec2 offset = fromAngle(state.rng.range(0.0f, kTau)) * crateDistance;
        pickups::spawnPickup(state, state.player.pos + offset, PickupType::Ammo);
    }
}

}  // namespace ls::waves
