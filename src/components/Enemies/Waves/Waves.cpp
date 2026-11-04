#include "components/Enemies/Waves/Waves.h"

#include "components/Enemies/Enemies.h"
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
}

}  // namespace ls::waves
