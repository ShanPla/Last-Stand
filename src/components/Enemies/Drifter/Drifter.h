#pragma once

#include "core/GameState.h"

namespace ls::enemies {

// Slow chaser: walks straight at the player.
Vec2 drifterVelocity(const GameState& state, const Enemy& enemy);

}  // namespace ls::enemies
