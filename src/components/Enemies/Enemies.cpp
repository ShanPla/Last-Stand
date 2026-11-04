#include "components/Enemies/Enemies.h"

#include "components/Enemies/Bloater/Bloater.h"
#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Brute/Brute.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Crawler/Crawler.h"
#include "components/Enemies/Runner/Runner.h"
#include "components/Enemies/Spitter/Spitter.h"
#include "components/Enemies/Walker/Walker.h"
#include "components/Pickups/Pickups.h"
#include "components/Player/Player.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>

namespace ls::enemies {

namespace {

// Each enemy's wanted velocity for this step. Every type has its own folder.
Vec2 behaviourVelocity(GameState& state, Enemy& enemy) {
    switch (enemy.type) {
        case EnemyType::Walker: return walkerVelocity(state, enemy);
        case EnemyType::Runner: return runnerVelocity(state, enemy);
        case EnemyType::Brute: return bruteVelocity(state, enemy);
        case EnemyType::Spitter: return spitterVelocity(state, enemy);
        case EnemyType::Bloater: return bloaterVelocity(state, enemy);
        case EnemyType::Crawler: return crawlerVelocity(state, enemy);
        case EnemyType::Charger: return chargerVelocity(state, enemy);
        case EnemyType::Boss: return bossVelocity(state, enemy);
        case EnemyType::Count: break;
    }
    return Vec2{};
}

// Pushes overlapping enemies apart so crowds spread out instead of stacking.
Vec2 separation(const GameState& state, const Enemy& enemy, int selfIndex) {
    Vec2 push{};
    int checked = 0;
    const float reach = enemy.radius + 16.0f;
    state.enemyGrid.query(enemy.pos, reach, [&](int other) {
        if (other == selfIndex || other >= state.enemies.count) return false;
        const Enemy& neighbour = state.enemies[other];
        const Vec2 delta = enemy.pos - neighbour.pos;
        const float minDistance = enemy.radius + neighbour.radius;
        const float distSq = lengthSq(delta);
        if (distSq < minDistance * minDistance && distSq > 1e-4f) {
            push += delta * ((minDistance - std::sqrt(distSq)) / minDistance);
        }
        // A cap keeps huge crowds cheap. The push only needs to look plausible.
        return ++checked >= 8;
    });
    return push * balance::kSeparationStrength;
}

// Enemies left far behind move to the opposite side of the player, so the
// pressure stays on instead of wasting them off screen.
void recycleIfFar(const GameState& state, Enemy& enemy) {
    if (enemy.type == EnemyType::Boss) return;
    const Vec2 offset = enemy.pos - state.player.pos;
    if (lengthSq(offset) > balance::kRecycleRadius * balance::kRecycleRadius) {
        enemy.pos = state.player.pos - normalizedOr(offset, Vec2{1.0f, 0.0f}) * balance::kSpawnRadius;
        enemy.prevPos = enemy.pos;
    }
}

}  // namespace

Enemy* spawn(GameState& state, EnemyType type, Vec2 pos, bool elite) {
    Enemy* enemy = state.enemies.add();
    if (!enemy) return nullptr;
    const balance::EnemyStats& stats = balance::kEnemyStats[static_cast<int>(type)];
    const float seconds = state.timeSeconds();
    const bool isBoss = type == EnemyType::Boss;
    const float hpScale = isBoss ? 1.0f : balance::enemyHpScale(seconds);
    const float damageScale = isBoss ? 1.0f : balance::enemyDamageScale(seconds);
    enemy->pos = pos;
    enemy->prevPos = pos;
    enemy->type = type;
    enemy->elite = elite;
    enemy->id = state.run.nextId++;
    enemy->maxHp = stats.hp * hpScale * (elite ? balance::kElite_HpMult : 1.0f);
    enemy->hp = enemy->maxHp;
    enemy->radius = stats.radius * (elite ? balance::kElite_RadiusMult : 1.0f);
    enemy->speed = stats.speed;
    enemy->contactDamage = stats.contactDamage * damageScale * (elite ? balance::kElite_DamageMult : 1.0f);
    return enemy;
}

void update(GameState& state) {
    state.enemyGrid.build(state.enemies.items, state.enemies.count);
    const float knockDecay = std::max(0.0f, 1.0f - balance::kKnockbackDecayPerSecond * kDt);
    const Vec2 playerPos = state.player.pos;

    for (int i = 0; i < state.enemies.count; ++i) {
        Enemy& enemy = state.enemies[i];
        enemy.prevPos = enemy.pos;
        enemy.flash = std::max(0.0f, enemy.flash - kDt);
        enemy.bladeCooldown = std::max(0.0f, enemy.bladeCooldown - kDt);

        const Vec2 velocity = behaviourVelocity(state, enemy) + enemy.knock + separation(state, enemy, i);
        enemy.pos += velocity * kDt;
        enemy.knock *= knockDecay;
        recycleIfFar(state, enemy);

        const float touch = enemy.radius + balance::kPlayerRadius;
        if (distanceSq(enemy.pos, playerPos) < touch * touch) {
            player::damage(state, enemy.contactDamage);
        }
    }
    // Rebuilt after moving, so weapons this step test against current positions.
    state.enemyGrid.build(state.enemies.items, state.enemies.count);
}

void updateBullets(GameState& state) {
    const Vec2 playerPos = state.player.pos;
    for (int i = state.enemyBullets.count - 1; i >= 0; --i) {
        EnemyBullet& bullet = state.enemyBullets[i];
        bullet.prevPos = bullet.pos;
        bullet.pos += bullet.vel * kDt;
        bullet.life -= kDt;
        const float touch = bullet.radius + balance::kPlayerRadius;
        if (distanceSq(bullet.pos, playerPos) < touch * touch) {
            player::damage(state, bullet.damage);
            state.enemyBullets.removeAt(i);
        } else if (bullet.life <= 0.0f) {
            state.enemyBullets.removeAt(i);
        }
    }
}

void fireBullet(GameState& state, Vec2 pos, Vec2 velocity, float damage) {
    EnemyBullet* bullet = state.enemyBullets.add();
    if (!bullet) return;
    bullet->pos = pos;
    bullet->prevPos = pos;
    bullet->vel = velocity;
    bullet->damage = damage;
    bullet->radius = balance::kEnemyBulletRadius;
    bullet->life = balance::kEnemyBulletLife;
}

void hit(GameState& state, int enemyIndex, float amount, WeaponId source, Vec2 knockback) {
    Enemy& enemy = state.enemies[enemyIndex];
    if (enemy.hp <= 0.0f) return;
    const float dealt = std::min(enemy.hp, amount);
    enemy.hp -= amount;
    enemy.flash = balance::kHitFlashSeconds;
    enemy.knock += knockback * balance::kEnemyStats[static_cast<int>(enemy.type)].knockbackTaken;
    state.run.damageByWeapon[static_cast<int>(source)] += dealt;
    pushEvent(state, EventType::EnemyHit, enemy.pos, dealt, static_cast<uint8_t>(source), enemy.elite);
}

void removeDead(GameState& state) {
    // Backward, so a removed item's replacement has already been checked. Enemies
    // added during this loop (Bloater crawlers) sit past the index and are not visited.
    for (int i = state.enemies.count - 1; i >= 0; --i) {
        if (state.enemies[i].hp > 0.0f) continue;
        const Enemy dead = state.enemies[i];
        state.enemies.removeAt(i);
        state.run.kills++;
        const balance::EnemyStats& stats = balance::kEnemyStats[static_cast<int>(dead.type)];
        pickups::dropGem(state, dead.pos, stats.xp * (dead.elite ? balance::kElite_XpMult : 1));
        pickups::onEnemyKilled(state, dead.pos, dead.elite);
        pushEvent(state, dead.elite ? EventType::EliteKilled : EventType::EnemyKilled, dead.pos, dead.radius,
                  static_cast<uint8_t>(dead.type));
    }
}

int nearest(const GameState& state, Vec2 pos, float range) {
    int best = -1;
    float bestDistSq = range * range;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        if (enemy.hp <= 0.0f) continue;
        const float distSq = distanceSq(enemy.pos, pos);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = i;
        }
    }
    return best;
}

}  // namespace ls::enemies
