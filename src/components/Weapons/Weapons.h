#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Adds a weapon at level 1. Does nothing if the build is full or already has it.
void grant(GameState& state, WeaponId id);

// Counts down cooldowns, fires weapons, and moves projectiles and zones.
void update(GameState& state);

// Called when a held weapon gains a level: fills its magazine and reserve.
void refill(GameState& state, WeaponSlot& slot);

// Ammo crate: adds this fraction of the maximum reserve to every gun that uses ammo.
void addAmmoFraction(GameState& state, float fraction);

// A weapon's numbers after passives and its evolution are applied.
balance::WeaponLevel effectiveStats(const GameState& state, const WeaponSlot& slot);

}  // namespace ls::weapons
