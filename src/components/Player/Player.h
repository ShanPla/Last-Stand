#pragma once

#include "core/GameState.h"

// Every function here works on one player, named by their slot in state.players.
namespace ls::player {

void reset(GameState& state, int slot);
void update(GameState& state, int slot, float moveX, float moveY, bool dashPressed);

// Applies armor and the short invulnerability after a hit.
void damage(GameState& state, int slot, float amount);
void heal(GameState& state, int slot, float amount);

// Co-op, at the end of a step: puts players at 0 HP down, and gets downed players back up
// once a teammate has stood close long enough. Returns true when the whole team is down,
// which ends the run.
bool downAndRevive(GameState& state);

}  // namespace ls::player
