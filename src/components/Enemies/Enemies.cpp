#include "components/Enemies/Enemies.h"

#include "components/Enemies/Bloater/Bloater.h"
#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Brute/Brute.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Crawler/Crawler.h"
#include "components/Enemies/Runner/Runner.h"
#include "components/Enemies/Spitter/Spitter.h"
#include "components/Enemies/Walker/Walker.h"
#include "components/Obstacles/Obstacles.h"
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

// Enemies left far behind every player move to the opposite side of the nearest one, so the
// pressure stays on instead of wasting them off screen.
void recycleIfFar(const GameState& state, Enemy& enemy) {
    if (enemy.type == EnemyType::Boss) return;
    const int nearest = nearestStanding(state, enemy.pos);
    if (nearest < 0) return;
    const Vec2 center = state.players[nearest].pos;
    const Vec2 offset = enemy.pos - center;
    if (lengthSq(offset) > balance::kRecycleRadius * balance::kRecycleRadius) {
        enemy.pos = center - normalizedOr(offset, Vec2{1.0f, 0.0f}) * balance::kSpawnRadius;
        enemy.prevPos = enemy.pos;
    }
}

// Co-op: each zombie chases the nearest standing player. Looking again every step would let
// a zombie halfway between two players flip back and forth, so it looks once every
// kZombieRetargetSteps, at a step picked by its id so the work is spread out. It looks at
// once when its target goes down. With one player the target is always slot 0.
void chooseTarget(const GameState& state, Enemy& enemy) {
    if (!state.isCoop()) return;
    const bool due = (state.run.tick + enemy.id) % balance::kZombieRetargetSteps == 0;
    if (!due && standing(state, enemy.target)) return;
    const int nearest = nearestStanding(state, enemy.pos);
    if (nearest < 0 || nearest == enemy.target) return;
    enemy.target = static_cast<uint8_t>(nearest);
    // Progress toward the old target says nothing about the new one, so the stuck watch starts over.
    enemy.stuck = 0;
    enemy.stuckQueued = 0;
    enemy.stuckFrom = enemy.pos;
}

// The boss walks straight through cars, because it is wider than a car is deep and its attacks
// assume straight paths, and it flattens each one it steps on. A dashing Charger keeps its
// locked direction on purpose: that is how it ends up crashing.
bool ignoresCars(const Enemy& enemy) { return enemy.type == EnemyType::Boss; }
bool goesAroundCars(const Enemy& enemy) { return !ignoresCars(enemy) && !(enemy.type == EnemyType::Charger && enemy.state == kChargerDash); }

float carRadius(const Enemy& enemy) { return enemy.radius * balance::kZombieCarRadiusShare; }

// True when a zombie nearer this one's target is touching it on the side it is trying to walk
// toward. Waiting in a queue isn't being stuck: the queue moves once those in front get there.
// It has to be nearer, because two zombies heading for opposite corners of a car can block
// each other head on. Then the nearer one has nobody to wait for, so it still gets rescued.
bool heldByCrowd(const GameState& state, const Enemy& enemy, Vec2 heading) {
    const Vec2 dir = normalizedOr(heading, Vec2{});
    const Vec2 targetPos = targetOf(state, enemy).pos;
    const float ownGapSq = distanceSq(enemy.pos, targetPos);
    bool held = false;
    state.enemyGrid.query(enemy.pos, enemy.radius + balance::largestPushedEnemyRadius(), [&](int other) {
        if (other >= state.enemies.count) return false;
        const Enemy& neighbour = state.enemies[other];
        if (&neighbour == &enemy || distanceSq(neighbour.pos, targetPos) >= ownGapSq) return false;
        const Vec2 delta = neighbour.pos - enemy.pos;
        const float touch = enemy.radius + neighbour.radius;
        if (lengthSq(delta) >= touch * touch) return false;
        // Within 60 degrees either side of where it is going.
        held = dot(delta, dir) > 0.5f * length(delta);
        return held;
    });
    return held;
}

// Counts how long a zombie has gone without getting closer to its target while cars are in
// its way. Steering can't tell us this, because a zombie pinned among wrecks can be steered
// on some steps and not on others, or not at all. What counts is the result, and it is
// measured from one spot held still until the zombie beats it, so walking back and forth
// along a car doesn't reset anything. Both distances use where the target is now, so a
// player running away doesn't look like a zombie falling behind. The count runs every step,
// but the sums only run every kZombieStuckCheckSteps, which keeps 2,000 zombies cheap.
void watchProgress(const GameState& state, Enemy& enemy, Vec2 wanted, Vec2 steered) {
    enemy.stuck++;
    if (enemy.stuck % balance::kZombieStuckCheckSteps != 0) return;
    const Vec2 playerPos = targetOf(state, enemy).pos;
    const Vec2 toPlayer = playerPos - enemy.pos;
    const float gap = length(toPlayer);
    const bool closer = distance(enemy.stuckFrom, playerPos) - gap >= balance::kZombieProgressMargin;
    const bool touching = gap <= enemy.radius + balance::kPlayerRadius + balance::kZombieTouchSlack;
    // A Spitter keeping its distance or a Charger winding up isn't trying to get closer.
    const bool chasing = dot(wanted, toPlayer) > 0.5f * length(wanted) * gap;
    if (closer || touching || !chasing) {
        enemy.stuck = 0;
        enemy.stuckQueued = 0;
        enemy.stuckFrom = enemy.pos;
        return;
    }
    // With no car on the line ahead it is free to walk on, and that will show as progress at a
    // later check. So this stretch of time doesn't count.
    const Vec2 ahead = enemy.pos + toPlayer * (std::min(gap, balance::kZombieStuckLookAhead) / gap);
    bool waiting = !obstacles::lineBlocked(state, enemy.pos, ahead, carRadius(enemy));
    // Nor does waiting in a queue, up to a point: zombies pressed together among cars can take
    // turns holding each other back forever, and then the queue is never going to move.
    if (!waiting && enemy.stuckQueued < balance::kZombieQueueChecks && heldByCrowd(state, enemy, steered)) {
        enemy.stuckQueued++;
        waiting = true;
    }
    if (waiting) enemy.stuck = static_cast<uint16_t>(enemy.stuck - balance::kZombieStuckCheckSteps);
}

// The velocity that goes around a car.
Vec2 aroundCars(const GameState& state, Enemy& enemy, Vec2 wanted) {
    if (!goesAroundCars(enemy)) return wanted;
    // Heading for its target, the way around is judged by the target's position. A Spitter
    // backing off or circling is heading somewhere else, so it is judged by its own heading.
    const Vec2 targetPos = targetOf(state, enemy).pos;
    const Vec2 toPlayer = targetPos - enemy.pos;
    const Vec2 target = dot(wanted, toPlayer) > 0.0f ? targetPos : enemy.pos + wanted;
    const Vec2 steered = obstacles::steerAround(state, enemy.pos, carRadius(enemy), wanted, target, enemy.id);
    watchProgress(state, enemy, wanted, steered);
    return steered;
}

// Runs after everything that moves zombies in a step, so nothing can leave one inside a car.
void settleAgainstCars(GameState& state) {
    for (int i = 0; i < state.enemies.count; ++i) {
        Enemy& enemy = state.enemies[i];
        if (ignoresCars(enemy)) {
            obstacles::crushUnder(state, enemy.pos, enemy.radius - balance::kBossCrushOverlap);
            continue;
        }
        const obstacles::Push push = obstacles::resolveCircle(state, enemy.pos, carRadius(enemy));
        if (push.hit && enemy.type == EnemyType::Charger && enemy.state == kChargerDash) {
            // A crash: it bounces off and is stunned for a moment, which is the player's reward.
            enemy.state = kChargerRecover;
            enemy.timer = balance::kChargerCrashSeconds;
            enemy.knock = enemy.dir * -balance::kChargerCrashKnock;
        }
        // No pathfinding means a zombie can get caught in the pocket between two cars. If it
        // has gone far too long without getting closer it is moved past them, because a zombie
        // that never arrives would make hiding behind a car safe.
        if (enemy.stuck < balance::kZombieStuckSteps) continue;
        enemy.stuck = 0;
        enemy.stuckQueued = 0;
        enemy.stuckFrom = enemy.pos;
        Vec2 spot;
        const Vec2 targetPos = targetOf(state, enemy).pos;
        if (!obstacles::rescueSpot(state, enemy.pos, carRadius(enemy), targetPos, enemy.radius + balance::kPlayerRadius, &spot)) continue;
        enemy.pos = spot;
        enemy.prevPos = spot;
        enemy.stuckFrom = spot;
        pushEvent(state, EventType::ZombieUnstuck, spot);
    }
}

}  // namespace

// How far a point is from the nearest standing player, squared. With one player, from them.
static float teamGapSq(const GameState& state, Vec2 pos) {
    const int nearest = nearestStanding(state, pos);
    return distanceSq(pos, state.players[nearest < 0 ? 0 : nearest].pos);
}

// When the pool is full, an elite or the boss still has to arrive: an elite carries a
// guaranteed reward and the boss ends the run. So the ordinary zombie farthest from every
// player is removed to make room. It is the one least likely to matter, and its removal is
// not a kill, so it gives no drop and no count. Equal distances go to the lower id, so the
// choice never depends on where the pool happens to have put anyone. This is one pass over
// the pool and runs only when the pool is full and one of these spawns, which is rare.
static bool makeRoomFor(GameState& state) {
    int victim = -1;
    float victimDistSq = -1.0f;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& candidate = state.enemies[i];
        if (candidate.elite || candidate.type == EnemyType::Boss) continue;
        const float distSq = teamGapSq(state, candidate.pos);
        if (distSq > victimDistSq || (distSq == victimDistSq && candidate.id < state.enemies[victim].id)) {
            victim = i;
            victimDistSq = distSq;
        }
    }
    if (victim < 0) return false;
    state.enemies.removeAt(victim);
    return true;
}

Enemy* spawn(GameState& state, EnemyType type, Vec2 pos, bool elite) {
    Enemy* enemy = state.enemies.add();
    if (!enemy) {
        // Ordinary spawns are dropped when full. That acts as a soft ceiling on the crowd.
        if (!elite && type != EnemyType::Boss) return nullptr;
        if (!makeRoomFor(state)) return nullptr;
        enemy = state.enemies.add();
    }
    const balance::EnemyStats& stats = balance::kEnemyStats[static_cast<int>(type)];
    const float seconds = state.timeSeconds();
    const bool isBoss = type == EnemyType::Boss;
    // The boss gets its difficulty share in waves::bossHpForBuild.
    const float hpScale = isBoss ? 1.0f : balance::enemyHpScale(seconds) * balance::difficultyTuning(state.run.difficulty).enemyHp;
    const float damageScale = isBoss ? 1.0f : balance::enemyDamageScale(seconds);
    enemy->pos = pos;
    enemy->type = type;
    enemy->elite = elite;
    enemy->id = state.run.nextId++;
    enemy->maxHp = stats.hp * hpScale * (elite ? balance::kElite_HpMult : 1.0f);
    enemy->hp = enemy->maxHp;
    enemy->radius = stats.radius * (elite ? balance::kElite_RadiusMult : 1.0f);
    enemy->speed = stats.speed;
    enemy->contactDamage = stats.contactDamage * damageScale * (elite ? balance::kElite_DamageMult : 1.0f);
    // Co-op: tougher zombies for a bigger team, if the balance table asks for it. The boss gets
    // its share in waves::bossHpForBuild. One player is never scaled, not even by 1.
    if (state.isCoop() && !isBoss) {
        enemy->maxHp *= balance::coopZombieHp(state.playerCount());
        enemy->hp = enemy->maxHp;
    }
    const int nearest = nearestStanding(state, pos);
    enemy->target = static_cast<uint8_t>(nearest < 0 ? 0 : nearest);
    // The boss waits one full interval before its first attack, so it can walk in.
    // Its attack cycle starts from the top, with the move set of the run's difficulty.
    if (isBoss) {
        enemy->timer = balance::bossMovesFor(state.run).attackInterval[0];
        state.run.bossStep = 0;
        state.run.bossRepeat = 0;
    }
    // Nothing spawns inside a car. The boss doesn't care, and arrives through them.
    if (!isBoss) obstacles::resolveCircle(state, enemy->pos, carRadius(*enemy));
    enemy->prevPos = enemy->pos;
    enemy->stuckFrom = enemy->pos;
    return enemy;
}

void update(GameState& state) {
    state.enemyGrid.build(state.enemies.items, state.enemies.count);
    const float knockDecay = std::max(0.0f, 1.0f - balance::kKnockbackDecayPerSecond * kDt);
    const int players = state.playerCount();

    for (int i = 0; i < state.enemies.count; ++i) {
        Enemy& enemy = state.enemies[i];
        enemy.prevPos = enemy.pos;
        enemy.flash = std::max(0.0f, enemy.flash - kDt);
        enemy.bladeCooldown = std::max(0.0f, enemy.bladeCooldown - kDt);
        chooseTarget(state, enemy);

        // The crowd moves around the boss. It must not be held back by the zombies it walks through.
        const Vec2 push = enemy.type == EnemyType::Boss ? Vec2{} : separation(state, enemy, i);
        const Vec2 velocity = aroundCars(state, enemy, behaviourVelocity(state, enemy)) + enemy.knock + push;
        enemy.pos += velocity * kDt;
        enemy.knock *= knockDecay;
        recycleIfFar(state, enemy);

        // A zombie hurts every player it touches, not only the one it chases. A downed player
        // is ignored: player::damage can't hurt someone at 0 HP.
        const float touch = enemy.radius + balance::kPlayerRadius;
        for (int slot = 0; slot < players; ++slot) {
            if (distanceSq(enemy.pos, state.players[slot].pos) < touch * touch) player::damage(state, slot, enemy.contactDamage);
        }
    }
    pushCrowdOutOfWideEnemies(state);
    // After the crowd pushes, or they would put zombies back into cars.
    settleAgainstCars(state);
    // Rebuilt after moving, so weapons this step test against current positions.
    state.enemyGrid.build(state.enemies.items, state.enemies.count);
}

void updateBullets(GameState& state) {
    const int players = state.playerCount();
    for (int i = state.enemyBullets.count - 1; i >= 0; --i) {
        EnemyBullet& bullet = state.enemyBullets[i];
        bullet.prevPos = bullet.pos;
        bullet.pos += bullet.vel * kDt;
        bullet.life -= kDt;
        // The first standing player it touches takes it, in slot order. It flies over a downed one.
        const float touch = bullet.radius + balance::kPlayerRadius;
        int struck = -1;
        for (int slot = 0; slot < players && struck < 0; ++slot) {
            if (standing(state, slot) && distanceSq(bullet.pos, state.players[slot].pos) < touch * touch) struck = slot;
        }
        if (struck >= 0) {
            player::damage(state, struck, bullet.damage);
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

// Shared by hit() and damageNoCredit(). A credited hit adds to the weapon's damage total,
// for the team and for the player who owns the weapon.
void applyDamage(GameState& state, int enemyIndex, float amount, bool credited, WeaponId source, int owner, Vec2 knockback) {
    Enemy& enemy = state.enemies[enemyIndex];
    if (enemy.hp <= 0.0f) return;
    const float dealt = std::min(enemy.hp, amount);
    enemy.hp -= amount;
    enemy.flash = balance::kHitFlashSeconds;
    enemy.knock += knockback * balance::kEnemyStats[static_cast<int>(enemy.type)].knockbackTaken;
    if (credited) {
        state.run.damageByWeapon[static_cast<int>(source)] += dealt;
        state.players[owner].damageByWeapon[static_cast<int>(source)] += dealt;
    }
    // WeaponId::Count in the event means no weapon was behind the hit.
    pushEvent(state, EventType::EnemyHit, enemy.pos, dealt, static_cast<uint8_t>(credited ? source : WeaponId::Count),
              enemy.elite);
}

// Crawlers burst out of a dead Bloater, spread evenly around it from a random start angle.
void spawnCrawlers(GameState& state, const Enemy& bloater) {
    const int count = balance::kCrawlersPerBloater * (bloater.elite ? balance::kElite_CrawlerMult : 1);
    const float start = state.rng.range(0.0f, kTau);
    for (int i = 0; i < count; ++i) {
        const float angle = start + kTau * static_cast<float>(i) / static_cast<float>(count);
        spawn(state, EnemyType::Crawler, bloater.pos + fromAngle(angle) * balance::kBloaterCrawlerSpread, false);
    }
}

}  // namespace

void hit(GameState& state, int enemyIndex, float amount, WeaponId source, int owner, Vec2 knockback) {
    applyDamage(state, enemyIndex, amount, true, source, owner, knockback);
}

void damageNoCredit(GameState& state, int enemyIndex, float amount) {
    applyDamage(state, enemyIndex, amount, false, WeaponId::Rifle, 0, Vec2{});
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
            // A win in Story, the run goes on in Endless. It drops no pickup either way.
            onBossKilled(state);
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
