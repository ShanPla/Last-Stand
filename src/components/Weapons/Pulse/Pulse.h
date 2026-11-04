#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Fires shots at the nearest enemy, fanned out when there is more than one.
void firePulse(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
