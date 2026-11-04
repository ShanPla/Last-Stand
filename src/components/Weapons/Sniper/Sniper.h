#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

void fireSniper(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
