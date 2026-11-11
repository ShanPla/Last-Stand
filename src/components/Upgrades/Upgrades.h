#pragma once

#include "core/GameState.h"

// Each player levels up on their own, so every function here takes that player's slot.
namespace ls::upgrades {

// Adds XP and counts any level-ups earned. The cards appear in update().
void addXp(GameState& state, int slot, int amount);

// Offers cards to players with a level waiting. In single-player this opens the level-up
// screen and pauses the run. In co-op the run goes on, and the player's cards open instead.
void update(GameState& state);

// True when this player has cards on screen to choose from.
bool choosing(const GameState& state, int slot);

// Picks offered card number index, and applies it.
void choose(GameState& state, int slot, int index);

// Replaces the current offers, if the player has rerolls left.
void reroll(GameState& state, int slot);

// Rebuilds a player's stats from the passives they hold.
void recomputeStats(GameState& state, int slot);

}  // namespace ls::upgrades
