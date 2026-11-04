#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Fires bullets at the nearest zombie, fanned out when there is more than one.
void fireRifle(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
