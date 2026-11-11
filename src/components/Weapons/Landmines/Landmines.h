#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Drops mines behind the player, but only while some zombie is in range.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireLandmines(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
