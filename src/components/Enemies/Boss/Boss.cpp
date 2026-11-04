#include "components/Enemies/Boss/Boss.h"

#include "components/Enemies/Enemies.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>

namespace ls::enemies {

namespace {

// Phase follows the boss's HP. It only ever goes up, and moves one step at a
// time, so a big hit that skips a phase still sends every phase event.
void updatePhase(GameState& state, const Enemy& boss) {
    const float fraction = std::max(0.0f, boss.hp / boss.maxHp);
    state.run.bossHpFraction = fraction;
    uint8_t wanted = 0;
    if (fraction <= balance::kBossPhase3HpFraction) wanted = 2;
    else if (fraction <= balance::kBossPhase2HpFraction) wanted = 1;
    while (state.run.bossPhase < wanted) {
        state.run.bossPhase++;
        pushEvent(state, EventType::BossPhaseChanged, boss.pos, static_cast<float>(state.run.bossPhase),
                  state.run.bossPhase);
    }
}

Vec2 rotated(Vec2 v, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Hostile zones only warn at first. The weapons component counts down their life
// and applies the damage at the end, so this only has to create them.
void addZone(GameState& state, ZoneKind kind, Vec2 a, Vec2 b, float radius, float damage) {
    Zone* zone = state.zones.add();
    if (!zone) return;
    zone->kind = kind;
    zone->a = a;
    zone->b = b;
    zone->radius = radius;
    zone->prevRadius = radius;
    zone->maxRadius = radius;
    zone->life = balance::kBossTelegraphSeconds;
    zone->maxLife = balance::kBossTelegraphSeconds;
    zone->damage = damage;
    zone->hostile = true;
}

void castCircles(GameState& state) {
    const int count = balance::kBossCircleCount[state.run.bossPhase];
    for (int i = 0; i < count; ++i) {
        // The first circle lands exactly on the player, so standing still is punished.
        Vec2 pos = state.player.pos;
        if (i > 0) {
            pos += fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(0.0f, balance::kBossCircleScatter);
        }
        addZone(state, ZoneKind::BossCircle, pos, pos, balance::kBossCircleRadius, balance::kBossCircleDamage);
    }
}

void castLines(GameState& state, const Enemy& boss) {
    const Vec2 dir = normalizedOr(state.player.pos - boss.pos, Vec2{1.0f, 0.0f});
    const auto addLine = [&](Vec2 lineDir) {
        addZone(state, ZoneKind::BossLine, boss.pos, boss.pos + lineDir * balance::kBossLineLength,
                balance::kBossLineHalfWidth, balance::kBossLineDamage);
    };
    addLine(dir);
    if (state.run.bossPhase >= 2) {
        addLine(rotated(dir, balance::kBossSecondLineRadians));
        addLine(rotated(dir, -balance::kBossSecondLineRadians));
    }
}

void fireRing(GameState& state, const Enemy& boss) {
    const int count = balance::kBossBulletRing[state.run.bossPhase];
    // A random start angle keeps the gaps from sitting in the same place each time.
    const float start = state.rng.range(0.0f, kTau / static_cast<float>(count));
    for (int i = 0; i < count; ++i) {
        const float angle = start + kTau * static_cast<float>(i) / static_cast<float>(count);
        fireBullet(state, boss.pos, fromAngle(angle) * balance::kBossBulletSpeed, balance::kBossBulletDamage);
    }
}

// The attack that follows another. The dash is only in the cycle from phase 1.
uint8_t nextAfter(uint8_t current, uint8_t phase) {
    switch (current) {
        case kBossNextCircles: return kBossNextLine;
        case kBossNextLine: return kBossNextRing;
        case kBossNextRing: return phase >= 1 ? kBossNextDash : kBossNextCircles;
        default: return kBossNextCircles;
    }
}

// Back to walking, with the attack after the one just finished.
void finishAttack(GameState& state, Enemy& boss, uint8_t attack) {
    boss.state = nextAfter(attack, state.run.bossPhase);
    boss.timer = balance::kBossAttackInterval[state.run.bossPhase];
}

}  // namespace

Vec2 bossVelocity(GameState& state, Enemy& enemy) {
    updatePhase(state, enemy);
    const Vec2 toPlayer = state.player.pos - enemy.pos;
    const uint8_t phase = state.run.bossPhase;

    switch (enemy.state) {
        case kBossRingCharge:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                fireRing(state, enemy);
                finishAttack(state, enemy, kBossNextRing);
            }
            return Vec2{};
        case kBossDashCharge:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                enemy.state = kBossDash;
                enemy.timer = balance::kBossDashSeconds;
            }
            return Vec2{};
        case kBossDash:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) finishAttack(state, enemy, kBossNextDash);
            return enemy.dir * balance::kBossDashSpeed;
        default:
            break;
    }

    // Walking. A phase change shortens the wait so the new tempo shows up at once.
    enemy.timer = std::min(enemy.timer, balance::kBossAttackInterval[phase]) - kDt;
    if (enemy.timer <= 0.0f) {
        switch (enemy.state) {
            case kBossNextCircles:
                castCircles(state);
                finishAttack(state, enemy, kBossNextCircles);
                break;
            case kBossNextLine:
                castLines(state, enemy);
                finishAttack(state, enemy, kBossNextLine);
                break;
            case kBossNextRing:
                enemy.state = kBossRingCharge;
                enemy.timer = balance::kBossTelegraphSeconds;
                break;
            default:  // kBossNextDash
                enemy.state = kBossDashCharge;
                enemy.timer = balance::kBossTelegraphSeconds;
                enemy.dir = normalizedOr(toPlayer, Vec2{1.0f, 0.0f});
                break;
        }
        // The attack that just started may stop the boss, so skip this step's walk.
        if (enemy.state >= kBossRingCharge) return Vec2{};
    }
    // The player must not be able to win or stall by running out of weapon range.
    const float speed = lengthSq(toPlayer) > balance::kBossLeashDistance * balance::kBossLeashDistance
                            ? balance::kBossCatchUpSpeed
                            : enemy.speed;
    return normalizedOr(toPlayer, Vec2{}) * speed;
}

}  // namespace ls::enemies
