#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Spins the blades around the player and hurts zombies they touch. Called every
// step, because blades have no cooldown between shots.
void updateBlades(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
