#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

void fireRockets(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
