#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Sprays piercing flames in a narrow cone at the nearest zombie in reach.
// Returns true if it really fired. With no target it returns false and does nothing.
bool fireFlamethrower(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
