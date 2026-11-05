#pragma once

#include "core/GameState.h"

namespace ls::player {

void reset(GameState& state);
void update(GameState& state, float moveX, float moveY, bool dashPressed);

// Applies armor and the short invulnerability after a hit.
void damage(GameState& state, float amount);
void heal(GameState& state, float amount);

}  // namespace ls::player
