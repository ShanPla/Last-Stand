#include "components/Enemies/Boss/Boss.h"

#include "components/Enemies/Enemies.h"

#include <algorithm>
#include <cmath>

namespace ls::enemies {

namespace {

using balance::BossAttack;

// Story uses the run's difficulty. An Endless return may use a harder set. See balance::bossMoveSet.
const balance::BossMoves& movesOf(const GameState& state) { return balance::bossMovesFor(state.run); }

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

Vec2 aimAtPlayer(const GameState& state, const Enemy& boss) {
    return normalizedOr(state.player.pos - boss.pos, Vec2{1.0f, 0.0f});
}

// Only the new attacks have a sound of their own. Sound reads this; the simulation never does.
void announce(GameState& state, const Enemy& boss, BossAttack attack) {
    pushEvent(state, EventType::BossAttack, boss.pos, 0.0f, static_cast<uint8_t>(attack));
}

// Hostile zones only warn at first. The weapons component counts down their life
// and applies the damage at the end, so this only has to create them.
void addZone(GameState& state, ZoneKind kind, Vec2 a, Vec2 b, float radius, float damage) {
    Zone* zone = state.zones.add();
    if (!zone) return;
    const float telegraph = movesOf(state).telegraphSeconds;
    zone->kind = kind;
    zone->a = a;
    zone->b = b;
    zone->radius = radius;
    zone->prevRadius = radius;
    zone->maxRadius = radius;
    zone->life = telegraph;
    zone->maxLife = telegraph;
    zone->damage = damage;
    zone->hostile = true;
}

// --- One function per attack ---------------------------------------------------

void castCircles(GameState& state) {
    const balance::BossMoves& moves = movesOf(state);
    const int count = moves.circleCount[state.run.bossPhase];
    for (int i = 0; i < count; ++i) {
        // The first circle lands exactly on the player, so standing still is punished.
        Vec2 pos = state.player.pos;
        if (i > 0) {
            pos += fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(0.0f, balance::kBossCircleScatter);
        }
        addZone(state, ZoneKind::BossCircle, pos, pos, moves.circleRadius, moves.circleDamage);
    }
}

void castLines(GameState& state, const Enemy& boss) {
    const balance::BossMoves& moves = movesOf(state);
    const Vec2 dir = aimAtPlayer(state, boss);
    const auto addLine = [&](Vec2 lineDir) {
        addZone(state, ZoneKind::BossLine, boss.pos, boss.pos + lineDir * balance::kBossLineLength,
                balance::kBossLineHalfWidth, moves.lineDamage);
    };
    addLine(dir);
    // Extra lines come in pairs, one each side, so the line at the player stays in the middle.
    const int pairs = moves.lineCount[state.run.bossPhase] / 2;
    for (int i = 1; i <= pairs; ++i) {
        const float turn = balance::kBossSecondLineRadians * static_cast<float>(i);
        addLine(rotated(dir, turn));
        addLine(rotated(dir, -turn));
    }
}

// Arms reach out from the player's feet. They are turned half a gap from the boss's line
// of sight, so walking toward, away from or sideways to the boss is always a way out.
void castCross(GameState& state, const Enemy& boss) {
    const balance::BossMoves& moves = movesOf(state);
    const float gap = kTau / static_cast<float>(balance::kBossCrossArms);
    const Vec2 dir = aimAtPlayer(state, boss);
    const Vec2 center = state.player.pos;
    for (int arm = 0; arm < balance::kBossCrossArms; ++arm) {
        const Vec2 armDir = rotated(dir, gap * 0.5f + gap * static_cast<float>(arm));
        addZone(state, ZoneKind::BossLine, center, center + armDir * balance::kBossCrossArmLength,
                balance::kBossLineHalfWidth, moves.crossDamage);
    }
    announce(state, boss, BossAttack::Cross);
}

void dropTrailBurst(GameState& state) {
    const balance::BossMoves& moves = movesOf(state);
    const Vec2 pos = state.player.pos;
    addZone(state, ZoneKind::BossCircle, pos, pos, moves.trailRadius, moves.trailDamage);
}

void fireRing(GameState& state, const Enemy& boss) {
    const balance::BossMoves& moves = movesOf(state);
    const int count = moves.ringBullets[state.run.bossPhase];
    // A random start angle keeps the gaps from sitting in the same place each time.
    const float start = state.rng.range(0.0f, kTau / static_cast<float>(count));
    for (int i = 0; i < count; ++i) {
        const float angle = start + kTau * static_cast<float>(i) / static_cast<float>(count);
        fireBullet(state, boss.pos, fromAngle(angle) * moves.bulletSpeed, moves.bulletDamage);
    }
}

// Each volley is turned a little further than the last, which draws the pinwheel. The
// arms are far apart, so the gaps between them are the way through.
void fireSpiralVolley(GameState& state, const Enemy& boss) {
    const balance::BossMoves& moves = movesOf(state);
    const int arms = moves.spiralArms[state.run.bossPhase];
    const int volley = moves.spiralVolleys - state.run.bossRepeat;
    const float turn = balance::spiralTurnRadians(moves, state.run.bossPhase) * static_cast<float>(volley);
    for (int i = 0; i < arms; ++i) {
        const Vec2 dir = rotated(boss.dir, turn + kTau * static_cast<float>(i) / static_cast<float>(arms));
        fireBullet(state, boss.pos, dir * moves.bulletSpeed, moves.bulletDamage);
    }
    state.run.bossRepeat--;
}

// Back to walking, with the next attack of the cycle.
void finishAttack(GameState& state, Enemy& boss) {
    const balance::BossMoves& moves = movesOf(state);
    const uint8_t phase = state.run.bossPhase;
    const int count = moves.cycles[phase].count;
    state.run.bossStep = static_cast<uint8_t>((state.run.bossStep % count + 1) % count);
    state.run.bossRepeat = 0;
    boss.state = kBossWalk;
    boss.timer = moves.attackInterval[phase];
}

void beginDashCharge(GameState& state, Enemy& boss) {
    boss.state = kBossDashCharge;
    boss.timer = movesOf(state).telegraphSeconds;
    boss.dir = aimAtPlayer(state, boss);
}

// The player must not be able to win or stall by running out of weapon range.
Vec2 walk(const GameState& state, const Enemy& boss) {
    const Vec2 toPlayer = state.player.pos - boss.pos;
    const float speed = lengthSq(toPlayer) > balance::kBossLeashDistance * balance::kBossLeashDistance
                            ? balance::kBossCatchUpSpeed
                            : boss.speed;
    return normalizedOr(toPlayer, Vec2{}) * speed;
}

}  // namespace

void onBossKilled(GameState& state) {
    RunState& run = state.run;
    run.bossHpFraction = 0.0f;
    if (run.mode != RunMode::Endless) {
        // Killing the boss wins a Story run, unless the player also died in this step (see sim::step).
        run.phase = RunPhase::Won;
        return;
    }
    // Endless goes on. The waves start their loop again from now, and the next boss is
    // one step stronger. See waves::update for when it comes.
    run.bossSpawned = false;
    run.bossPhase = 0;
    run.bossStep = 0;
    run.bossRepeat = 0;
    run.bossesKilled++;
    run.lastBossKillTick = run.tick;
    run.nextEliteEvent = balance::firstLoopElite();
}

balance::BossAttack nextBossAttack(const GameState& state) {
    const balance::BossCycle& cycle = movesOf(state).cycles[state.run.bossPhase];
    return cycle.attacks[state.run.bossStep % cycle.count];
}

void startBossAttack(GameState& state, Enemy& boss, BossAttack attack) {
    const balance::BossMoves& moves = movesOf(state);
    const uint8_t phase = state.run.bossPhase;
    switch (attack) {
        case BossAttack::Circles:
            castCircles(state);
            finishAttack(state, boss);
            break;
        case BossAttack::Line:
            castLines(state, boss);
            finishAttack(state, boss);
            break;
        case BossAttack::Cross:
            castCross(state, boss);
            finishAttack(state, boss);
            break;
        case BossAttack::Ring:
            boss.state = kBossRingCharge;
            boss.timer = moves.telegraphSeconds;
            break;
        case BossAttack::Dash:
            state.run.bossRepeat = static_cast<uint8_t>(moves.dashCount[phase] - 1);
            beginDashCharge(state, boss);
            break;
        case BossAttack::Trail:
            // The first burst lands now, the rest follow while the boss keeps walking.
            dropTrailBurst(state);
            state.run.bossRepeat = static_cast<uint8_t>(moves.trailBursts[phase] - 1);
            boss.state = kBossTrail;
            boss.timer = moves.trailInterval;
            announce(state, boss, attack);
            if (state.run.bossRepeat == 0) finishAttack(state, boss);
            break;
        case BossAttack::Spiral:
            boss.state = kBossSpiralCharge;
            boss.timer = moves.telegraphSeconds;
            boss.dir = aimAtPlayer(state, boss);
            announce(state, boss, attack);
            break;
        case BossAttack::Count:
            break;
    }
}

Vec2 bossVelocity(GameState& state, Enemy& enemy) {
    updatePhase(state, enemy);
    const balance::BossMoves& moves = movesOf(state);

    switch (enemy.state) {
        case kBossRingCharge:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                fireRing(state, enemy);
                finishAttack(state, enemy);
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
            if (enemy.timer <= 0.0f) {
                // Each extra dash aims again and shows its full warning again.
                if (state.run.bossRepeat > 0) {
                    state.run.bossRepeat--;
                    beginDashCharge(state, enemy);
                    return Vec2{};
                }
                finishAttack(state, enemy);
            }
            return enemy.dir * balance::kBossDashSpeed;
        case kBossSpiralCharge:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                state.run.bossRepeat = static_cast<uint8_t>(moves.spiralVolleys);
                fireSpiralVolley(state, enemy);
                enemy.state = kBossSpiral;
                enemy.timer = moves.spiralVolleySeconds;
                if (state.run.bossRepeat == 0) finishAttack(state, enemy);
            }
            return Vec2{};
        case kBossSpiral:
            // It walks while firing, so a long spiral can't let a fleeing player get away.
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                fireSpiralVolley(state, enemy);
                enemy.timer += moves.spiralVolleySeconds;
                if (state.run.bossRepeat == 0) finishAttack(state, enemy);
            }
            return walk(state, enemy);
        case kBossTrail:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                dropTrailBurst(state);
                state.run.bossRepeat--;
                enemy.timer += moves.trailInterval;
                if (state.run.bossRepeat == 0) finishAttack(state, enemy);
            }
            return walk(state, enemy);
        default:
            break;
    }

    // Walking. A phase change shortens the wait so the new tempo shows up at once.
    const uint8_t phase = state.run.bossPhase;
    enemy.timer = std::min(enemy.timer, moves.attackInterval[phase]) - kDt;
    if (enemy.timer <= 0.0f) {
        startBossAttack(state, enemy, nextBossAttack(state));
        // The attack that just started may stop the boss, so skip this step's walk.
        if (bossCharging(enemy)) return Vec2{};
    }
    return walk(state, enemy);
}

}  // namespace ls::enemies
