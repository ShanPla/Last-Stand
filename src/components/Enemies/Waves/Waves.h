#pragma once

#include "core/GameState.h"

namespace ls::waves {

// Spawns enemies from the wave table, elite events, and the boss at 10:00.
void update(GameState& state);

// The boss's HP for the player's current build. Stronger builds get a tougher boss.
float bossHpForBuild(const GameState& state);

// Spawn rate of one wave row at a given time, in enemies per second.
float rowRate(int rowIndex, float seconds);

}  // namespace ls::waves
