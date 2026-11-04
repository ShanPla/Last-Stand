#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Lobs grenades that land on the nearest zombies just as their fuse ends.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireGrenadeLauncher(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
