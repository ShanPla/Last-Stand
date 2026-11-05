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

// The velocity that moves enemy out of neighbour, or zero if they don't overlap.
Vec2 overlapPush(const Enemy& enemy, const Enemy& neighbour) {
    const Vec2 delta = enemy.pos - neighbour.pos;
    const float minDistance = enemy.radius + neighbour.radius;
    const float distSq = lengthSq(delta);
    if (distSq >= minDistance * minDistance || distSq <= 1e-4f) return Vec2{};
    return delta * ((minDistance - std::sqrt(distSq)) / minDistance * balance::kSeparationStrength);
}

// Pushes overlapping enemies apart so crowds spread out instead of stacking. Each enemy
// looks only a short way around itself, which keeps 2,000 of them cheap. That look can
// miss the center of a wide neighbour, so pushCrowdOutOfWideEnemies handles those pairs.
Vec2 separation(const GameState& state, const Enemy& enemy, int selfIndex) {
    Vec2 push{};
    int checked = 0;
    const float reach = enemy.radius + balance::kSeparationReach;
    state.enemyGrid.query(enemy.pos, reach, [&](int other) {
        if (other == selfIndex || other >= state.enemies.count) return false;
        const Enemy& neighbour = state.enemies[other];
        if (neighbour.radius > balance::kSeparationReach) return false;
        push += overlapPush(enemy, neighbour);
        // The push only needs to look plausible, so a few neighbours are enough.
        return ++checked >= balance::kSeparationMaxChecks;
    });
    return push;
}

// Wide enemies are few, so they can afford a longer look, and they push out everyone who
// overlaps them. This way a push never depends on which grid cell a center falls in.
void pushCrowdOutOfWideEnemies(GameState& state) {
    const float widestNeighbour = balance::largestPushedEnemyRadius();
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& wide = state.enemies[i];
        if (wide.radius <= balance::kSeparationReach) continue;
        state.enemyGrid.query(wide.pos, wide.radius + widestNeighbour, [&](int other) {
            if (other == i || other >= state.enemies.count) return false;
            Enemy& neighbour = state.enemies[other];
            // The crowd moves around the boss. It is never pushed by the crowd.
            if (neighbour.type == EnemyType::Boss) return false;
            neighbour.pos += overlapPush(neighbour, wide) * kDt;
            return false;
        });
    }
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
    // The boss gets its difficulty share in waves::bossHpForBuild.
    const float hpScale = isBoss ? 1.0f : balance::enemyHpScale(seconds) * balance::difficultyTuning(state.run.difficulty).enemyHp;
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
    // The boss waits one full interval before its first attack, so it can walk in.
    if (isBoss) enemy->timer = balance::kBossAttackInterval[0];
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

        // The crowd moves around the boss. It must not be held back by the zombies it walks through.
        const Vec2 push = enemy.type == EnemyType::Boss ? Vec2{} : separation(state, enemy, i);
        const Vec2 velocity = behaviourVelocity(state, enemy) + enemy.knock + push;
        enemy.pos += velocity * kDt;
        enemy.knock *= knockDecay;
        recycleIfFar(state, enemy);

        const float touch = enemy.radius + balance::kPlayerRadius;
        if (distanceSq(enemy.pos, playerPos) < touch * touch) {
            player::damage(state, enemy.contactDamage);
        }
    }
    pushCrowdOutOfWideEnemies(state);
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

namespace {

// Shared by hit() and damageNoCredit(). A credited hit adds to the weapon's damage total.
void applyDamage(GameState& state, int enemyIndex, float amount, bool credited, WeaponId source, Vec2 knockback) {
    Enemy& enemy = state.enemies[enemyIndex];
    if (enemy.hp <= 0.0f) return;
    const float dealt = std::min(enemy.hp, amount);
    enemy.hp -= amount;
    enemy.flash = balance::kHitFlashSeconds;
    enemy.knock += knockback * balance::kEnemyStats[static_cast<int>(enemy.type)].knockbackTaken;
    if (credited) state.run.damageByWeapon[static_cast<int>(source)] += dealt;
    // WeaponId::Count in the event means no weapon was behind the hit.
    pushEvent(state, EventType::EnemyHit, enemy.pos, dealt, static_cast<uint8_t>(credited ? source : WeaponId::Count),
              enemy.elite);
}

// Crawlers burst out of a dead Bloater, spread evenly around it from a random start angle.
void spawnCrawlers(GameState& state, const Enemy& bloater) {
    const int count = balance::kCrawlersPerBloater * (bloater.elite ? 2 : 1);
    const float start = state.rng.range(0.0f, kTau);
    for (int i = 0; i < count; ++i) {
        const float angle = start + kTau * static_cast<float>(i) / static_cast<float>(count);
        spawn(state, EnemyType::Crawler, bloater.pos + fromAngle(angle) * balance::kBloaterCrawlerSpread, false);
    }
}

}  // namespace

void hit(GameState& state, int enemyIndex, float amount, WeaponId source, Vec2 knockback) {
    applyDamage(state, enemyIndex, amount, true, source, knockback);
}

void damageNoCredit(GameState& state, int enemyIndex, float amount) {
    applyDamage(state, enemyIndex, amount, false, WeaponId::Rifle, Vec2{});
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
        if (dead.type == EnemyType::Boss) {
            // Killing the boss wins the run, unless the player also died in this step
            // (see sim::step). It drops no pickup, because the run is over.
            state.run.bossHpFraction = 0.0f;
            state.run.phase = RunPhase::Won;
            pushEvent(state, EventType::BossKilled, dead.pos, dead.radius, static_cast<uint8_t>(dead.type));
            continue;
        }
        pickups::onEnemyKilled(state, dead.pos, dead.elite);
        if (dead.type == EnemyType::Bloater) spawnCrawlers(state, dead);
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
