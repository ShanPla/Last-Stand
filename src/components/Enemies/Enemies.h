#pragma once

#include "core/GameState.h"

namespace ls::enemies {

// Returns nullptr when the pool is full, except that an elite or the boss always gets a place:
// the ordinary zombie farthest from every standing player is removed to make room for it.
// A new zombie chases the standing player nearest to where it appears.
Enemy* spawn(GameState& state, EnemyType type, Vec2 pos, bool elite);

// The player a zombie is chasing. Every behaviour reads its target from here.
inline const Player& targetOf(const GameState& state, const Enemy& enemy) { return state.players[enemy.target]; }

// Moves every enemy, applies contact damage, and rebuilds the spatial hash.
void update(GameState& state);

// Moves enemy bullets and checks them against the players.
void updateBullets(GameState& state);

// Fires one enemy bullet. Does nothing if the pool is full.
void fireBullet(GameState& state, Vec2 pos, Vec2 velocity, float damage);

// Lowers an enemy's HP. It stays in the pool until removeDead runs, so indices stay valid
// for the rest of the step. source says which weapon gets the credit, and owner whose it is.
void hit(GameState& state, int enemyIndex, float amount, WeaponId source, int owner, Vec2 knockback);

// Same as hit, but no weapon gets the credit and there is no knockback. For the bomb.
void damageNoCredit(GameState& state, int enemyIndex, float amount);

// Removes dead enemies, and handles their drops, splitting and kill counts.
void removeDead(GameState& state);

// Index of the nearest living enemy within range, or -1.
int nearest(const GameState& state, Vec2 pos, float range);

}  // namespace ls::enemies
