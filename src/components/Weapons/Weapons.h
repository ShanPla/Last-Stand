#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::weapons {

// Adds a weapon at level 1 with a full magazine and reserve. Does nothing if the
// build is full or already has it.
void grant(GameState& state, WeaponId id);

// Counts down cooldowns, handles ammo and reloads, fires weapons, and moves
// projectiles and zones.
void update(GameState& state);

// Called when a held weapon gains a level: fills its magazine and reserve.
void refill(GameState& state, WeaponSlot& slot);

// Ammo crate: adds this fraction of the maximum reserve to every gun that uses ammo.
void addAmmoFraction(GameState& state, float fraction);

// About how much damage per second this build can put on one big target. On-paper
// numbers from the weapon tables, not a measurement, so it's the same for any seed.
float estimatedBossDps(const GameState& state);

// A weapon's numbers after passives and its evolution are applied.
balance::WeaponLevel effectiveStats(const GameState& state, const WeaponSlot& slot);

// Helpers shared by the weapon folders. ------------------------------------------

// Records a trigger pull: points the gun along aim and sends a WeaponFired event.
void markFired(GameState& state, const WeaponSlot& slot, Vec2 aim);

// Nearest living enemy within range that is not in the excluded index list, or -1.
int nearestEnemyExcluding(const GameState& state, Vec2 pos, float range, const int* excluded, int excludedCount);

// Squared distance from a point to the segment a-b.
float distanceToSegmentSq(Vec2 point, Vec2 a, Vec2 b);

}  // namespace ls::weapons
