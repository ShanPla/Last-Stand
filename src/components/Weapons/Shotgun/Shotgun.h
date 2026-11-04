#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Fires a cone of short-range pellets at the nearest zombie in reach.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireShotgun(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
