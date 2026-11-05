#include "components/Enemies/Spitter/Spitter.h"

#include "components/Enemies/Enemies.h"
#include "data/Balance.h"

#include <algorithm>

namespace ls::enemies {

Vec2 spitterVelocity(GameState& state, Enemy& enemy) {
    const Vec2 toPlayer = state.player.pos - enemy.pos;
    const float dist = length(toPlayer);
    const Vec2 dir = normalizedOr(toPlayer, Vec2{1.0f, 0.0f});

    // Fire timer. On its first step (state 0) each spitter starts at a different
    // point in the cycle, taken from its id, so a group does not fire in step.
    if (enemy.state == 0) {
        enemy.state = 1;
        const uint32_t phase = (enemy.id * 7u) % balance::kSpitterFirePhases;
        enemy.timer = balance::kSpitterFireInterval * static_cast<float>(phase) / static_cast<float>(balance::kSpitterFirePhases);
    }
    // While out of range it may bank at most one step of readiness, so it can't fire a
    // burst of catch-up shots when it comes back into range. Guns work the same way.
    enemy.timer = std::max(enemy.timer - kDt, -kDt);

    // A bullet can only reach so far, so shooting from beyond that wastes it.
    const float bulletReach = balance::kSpitterBulletSpeed * balance::kEnemyBulletLife;
    if (enemy.timer <= 0.0f && dist < bulletReach) {
        enemy.timer += balance::kSpitterFireInterval;
        const float damage = balance::kSpitterBulletDamage * balance::enemyDamageScale(state.timeSeconds()) *
                             (enemy.elite ? balance::kElite_DamageMult : 1.0f);
        fireBullet(state, enemy.pos, dir * balance::kSpitterBulletSpeed, damage);
    }

    // Close in until the keep distance, back off if the player comes nearer, and
    // otherwise circle around. The band stops it flickering between the three.
    if (dist > balance::kSpitterKeepDistance + balance::kSpitterDistanceBand) return dir * enemy.speed;
    if (dist < balance::kSpitterKeepDistance - balance::kSpitterDistanceBand) return dir * (-enemy.speed);
    // Odd and even ids circle in opposite directions, so a group spreads out.
    const float sign = (enemy.id & 1u) ? 1.0f : -1.0f;
    return Vec2{-dir.y, dir.x} * (sign * enemy.speed * balance::kSpitterStrafeSpeedFraction);
}

}  // namespace ls::enemies
