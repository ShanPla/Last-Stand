#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Instant beam at the toughest zombie in range, hitting everything along the line.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireSniper(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
