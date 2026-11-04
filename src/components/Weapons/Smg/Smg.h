#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

void fireSmg(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
