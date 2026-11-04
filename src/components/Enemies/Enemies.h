#pragma once

#include "core/GameState.h"

namespace ls::enemies {

// Returns nullptr when the pool is full.
Enemy* spawn(GameState& state, EnemyType type, Vec2 pos, bool elite);

// Moves every enemy, applies contact damage, and rebuilds the spatial hash.
void update(GameState& state);

// Moves enemy bullets and checks them against the player.
void updateBullets(GameState& state);

// Fires one enemy bullet. Does nothing if the pool is full.
void fireBullet(GameState& state, Vec2 pos, Vec2 velocity, float damage);

// Lowers an enemy's HP. It stays in the pool until removeDead runs, so indices
// stay valid for the rest of the step. source says which weapon gets the credit.
void hit(GameState& state, int enemyIndex, float amount, WeaponId source, Vec2 knockback);

// Removes dead enemies, and handles their drops, splitting and kill counts.
void removeDead(GameState& state);

// Index of the nearest living enemy within range, or -1.
int nearest(const GameState& state, Vec2 pos, float range);

}  // namespace ls::enemies
