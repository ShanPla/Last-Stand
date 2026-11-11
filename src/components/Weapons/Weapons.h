#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

// Every player holds their own weapons. A function that fires or changes one names the
// player who holds it by their slot in state.players, called the owner.
namespace ls::weapons {

// Adds a weapon at level 1 with a full magazine and reserve to one player. Does nothing if
// their build is full or already has it.
void grant(GameState& state, int owner, WeaponId id);

// For every standing player who isn't choosing a card: counts down cooldowns, handles ammo
// and reloads, and fires. Then moves every projectile and zone.
void update(GameState& state);

// Called when a held weapon gains a level: fills its magazine and reserve.
void refill(const Player& owner, WeaponSlot& slot);

// Ammo crate: adds this fraction of the maximum reserve to every gun this player holds that uses ammo.
void addAmmoFraction(GameState& state, int owner, float fraction);

// About how much damage per second this player's build can put on one big target. On-paper
// numbers from the weapon tables, not a measurement, so it's the same for any seed.
float estimatedBossDps(const Player& owner);

// A weapon's numbers after its owner's passives and its evolution are applied.
balance::WeaponLevel effectiveStats(const Player& owner, const WeaponSlot& slot);

// Helpers shared by the weapon folders. ------------------------------------------

// Records a trigger pull: points the owner's gun along aim and sends a WeaponFired event.
void markFired(GameState& state, int owner, const WeaponSlot& slot, Vec2 aim);

// Nearest living enemy within range that is not in the excluded index list, or -1.
int nearestEnemyExcluding(const GameState& state, Vec2 pos, float range, const int* excluded, int excludedCount);

// Squared distance from a point to the segment a-b.
float distanceToSegmentSq(Vec2 point, Vec2 a, Vec2 b);

}  // namespace ls::weapons
