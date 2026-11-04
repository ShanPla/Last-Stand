#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// How far a blade sits from the player. Even blades use the outer circle, and odd
// ones the inner circle. The renderer draws them from this too, so what you see is
// where they cut.
inline float bladeOrbitRadius(int bladeIndex, const balance::WeaponLevel& stats) {
    return bladeIndex % 2 == 0 ? stats.area : stats.area * balance::kBladeInnerRadiusFraction;
}

// Spins the blades around the player and hurts zombies they touch. Called every
// step, because blades have no cooldown between shots.
void updateBlades(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats);

}  // namespace ls::weapons
