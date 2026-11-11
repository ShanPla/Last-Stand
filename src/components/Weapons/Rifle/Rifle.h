#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Fires bullets at the nearest zombie, fanned out when there is more than one.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireRifle(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
