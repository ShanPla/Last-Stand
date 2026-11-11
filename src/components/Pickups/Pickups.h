#pragma once

#include "core/GameState.h"

namespace ls::pickups {

// Moves gems, collects them and pickups, and merges gems when too many exist.
void update(GameState& state);

// Adds a gem. When the pool is full, the XP goes straight to the nearest player, so no
// reward is ever lost.
void dropGem(GameState& state, Vec2 pos, int xp);

// Called for each death. Elites always drop a pickup, others on a kill counter.
void onEnemyKilled(GameState& state, Vec2 pos, bool elite);

// Adds a pickup. Pickups never expire, so a full pool replaces the oldest one, which
// means an uncollected pile can never block a needed ammo crate.
void spawnPickup(GameState& state, Vec2 pos, PickupType type);

// Combines nearby gems into one, keeping total XP the same.
void mergeGems(GameState& state);

}  // namespace ls::pickups
