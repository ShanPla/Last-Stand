#pragma once

#include "core/GameState.h"

namespace ls::pickups {

// Moves gems, collects them and pickups, and merges gems when too many exist.
void update(GameState& state);

// Adds a gem. When the pool is full, the XP goes straight to the player, so no
// reward is ever lost.
void dropGem(GameState& state, Vec2 pos, int xp);

// Called for each death. Elites always drop a pickup, others on a kill counter.
void onEnemyKilled(GameState& state, Vec2 pos, bool elite);

// Combines nearby gems into one, keeping total XP the same.
void mergeGems(GameState& state);

}  // namespace ls::pickups
