#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Launches a fan of rockets. They home in on zombies while they fly.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireRockets(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
