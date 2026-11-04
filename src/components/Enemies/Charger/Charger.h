#pragma once

#include "core/GameState.h"

namespace ls::enemies {

// Stored in enemy.state. The renderer reads it to draw the telegraph.
enum ChargerState : uint8_t {
    kChargerChase = 0,
    kChargerTelegraph = 1,  // Standing still, aim locked in enemy.dir.
    kChargerDash = 2,
    kChargerRecover = 3,
};

// Chases, then telegraphs, dashes in a straight line, rests, and chases again.
Vec2 chargerVelocity(GameState& state, Enemy& enemy);

}  // namespace ls::enemies
