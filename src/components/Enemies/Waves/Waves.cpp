#include "components/Enemies/Waves/Waves.h"

#include "components/Enemies/Enemies.h"
#include "components/Pickups/Pickups.h"
#include "data/Balance.h"

#include <iterator>

namespace ls::waves {

namespace {

Vec2 spawnPoint(GameState& state) {
    return state.player.pos + fromAngle(state.rng.range(0.0f, kTau)) * balance::kSpawnRadius;
}

}  // namespace

float rowRate(int rowIndex, float seconds) {
    const balance::WaveRow& row = balance::kWaves[rowIndex];
    if (seconds < row.startSec || seconds >= row.endSec) return 0.0f;
    const float t = (seconds - row.startSec) / (row.endSec - row.startSec);
    return lerpf(row.startRate, row.endRate, t);
}

void update(GameState& state) {
    RunState& run = state.run;
    const float seconds = state.timeSeconds();
    if (run.debugEnemyTarget > 0) return;

    const int rowCount = static_cast<int>(std::size(balance::kWaves));
    for (int i = 0; i < rowCount; ++i) {
        const balance::WaveRow& row = balance::kWaves[i];
        run.waveAccumulators[i] += rowRate(i, seconds) * kDt;
        while (run.waveAccumulators[i] >= 1.0f) {
            run.waveAccumulators[i] -= 1.0f;
            // A group spawns together at one point, so it arrives as a pack.
            const Vec2 groupCenter = spawnPoint(state);
            for (int member = 0; member < row.groupSize; ++member) {
                const Vec2 jitter = fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(0.0f, 44.0f);
                enemies::spawn(state, row.type, groupCenter + jitter, false);
            }
        }
    }

    const int eliteCount = static_cast<int>(std::size(balance::kEliteEvents));
    while (run.nextEliteEvent < eliteCount && seconds >= balance::kEliteEvents[run.nextEliteEvent].timeSec) {
        enemies::spawn(state, balance::kEliteEvents[run.nextEliteEvent].type, spawnPoint(state), true);
        run.nextEliteEvent++;
    }

    // The wave table ends at kRunSeconds, so from here only the boss arrives.
    if (!run.bossSpawned && seconds >= balance::kRunSeconds) {
        // The horde clears when the boss arrives. Its attacks must be readable, and
        // shots must reach it rather than being soaked up by leftover zombies.
        state.enemies.clear();
        pushEvent(state, EventType::BombBlast, state.player.pos, balance::kBombRadius);
        const Vec2 pos = spawnPoint(state);
        if (enemies::spawn(state, EnemyType::Boss, pos, false)) {
            run.bossSpawned = true;
            run.bossPhase = 0;
            run.bossHpFraction = 1.0f;
            pushEvent(state, EventType::BossSpawned, pos);
        }
    }

    if (run.bossSpawned) {
        // A few Walkers keep XP coming during the fight. The wave table never uses
        // the last accumulator, because it has fewer rows than the array has slots.
        float& adds = run.waveAccumulators[std::size(run.waveAccumulators) - 1];
        adds += balance::kBossAddsPerSecond * kDt;
        while (adds >= 1.0f) {
            adds -= 1.0f;
            enemies::spawn(state, EnemyType::Walker, spawnPoint(state), false);
        }
        // Crates normally come from kills, and a player with empty guns gets few
        // kills. Timed crates mean the boss fight can never become unwinnable.
        const uint32_t crateTicks = static_cast<uint32_t>(balance::kBossAmmoCrateSeconds / kDt);
        if (run.tick % crateTicks == 0) {
            const Vec2 offset = fromAngle(state.rng.range(0.0f, kTau)) * balance::kBossAmmoCrateDistance;
            pickups::spawnPickup(state, state.player.pos + offset, PickupType::Ammo);
        }
    }
}

}  // namespace ls::waves
