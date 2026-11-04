#pragma once

#include "core/GameState.h"

namespace ls::upgrades {

// Adds XP and counts any level-ups earned. The cards appear in update().
void addXp(GameState& state, int amount);

// Opens the level-up screen when a level is waiting.
void update(GameState& state);

// Picks offered card number index, and applies it.
void choose(GameState& state, int index);

// Replaces the current offers, if the player has rerolls left.
void reroll(GameState& state);

// Rebuilds player stats from the held passives.
void recomputeStats(GameState& state);

}  // namespace ls::upgrades
