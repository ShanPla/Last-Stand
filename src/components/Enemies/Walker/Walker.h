#pragma once

#include "core/GameState.h"

namespace ls::enemies {

// Slow chaser: shambles straight at the player.
Vec2 walkerVelocity(GameState& state, Enemy& enemy);

}  // namespace ls::enemies
