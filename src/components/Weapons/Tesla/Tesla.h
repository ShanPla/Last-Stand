#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Lightning that starts at the nearest zombie and jumps to the next nearest ones.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireTesla(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
