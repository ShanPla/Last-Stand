#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Fires randomly scattered bullets at the nearest zombie.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireSmg(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
