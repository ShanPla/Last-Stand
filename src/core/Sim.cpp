#include "core/Sim.h"

#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Pickups/Pickups.h"
#include "components/Player/Player.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"

#include <cstring>

namespace ls::sim {

namespace {

// Debug only: keeps a known number of objects alive for performance measurement.
void applyStressTargets(GameState& state) {
    const Vec2 center = state.player.pos;
    while (state.enemies.count < state.run.debugEnemyTarget) {
        const Vec2 pos = center + fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(120.0f, 650.0f);
        if (!enemies::spawn(state, EnemyType::Walker, pos, false)) break;
    }
    while (state.projectiles.count < state.run.debugProjectileTarget) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        const Vec2 pos = center + fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(0.0f, 600.0f);
        shot->pos = pos;
        shot->prevPos = pos;
        shot->vel = fromAngle(state.rng.range(0.0f, kTau)) * 200.0f;
        shot->damage = 1.0f;
        shot->radius = balance::kBulletRadius;
        shot->life = 1.5f;
        shot->pierce = 2;
    }
    // The stress test must keep running, so the player can't die or level up.
    if (state.run.debugEnemyTarget > 0) {
        state.player.hp = state.player.maxHp;
        state.run.pendingLevels = 0;
    }
}

}  // namespace

void newRun(GameState& state, uint64_t seed) {
    state.rng = Rng{};
    state.rng.seed(seed);
    state.run = RunState{};
    state.run.seed = seed;
    state.run.rerollsLeft = balance::kStartingRerolls;
    state.run.nextPickupDropKills = balance::kKillsPerPickupDrop;
    state.enemies.clear();
    state.projectiles.clear();
    state.enemyBullets.clear();
    state.zones.clear();
    state.gems.clear();
    state.pickups.clear();
    state.events.clear();
    player::reset(state);
    upgrades::recomputeStats(state);
    state.player.hp = state.player.maxHp;
    weapons::grant(state, WeaponId::Rifle);
}

void step(GameState& state, const InputFrame& input) {
    RunState& run = state.run;
    if (run.phase == RunPhase::Won || run.phase == RunPhase::Dead) return;

    if (run.phase == RunPhase::LevelUp) {
        if (input.reroll) upgrades::reroll(state);
        if (input.chooseCard >= 0) upgrades::choose(state, input.chooseCard);
        return;
    }

    run.tick++;
    applyStressTargets(state);
    player::update(state, input.moveX, input.moveY);
    waves::update(state);
    enemies::update(state);
    weapons::update(state);
    enemies::updateBullets(state);
    pickups::update(state);
    enemies::removeDead(state);
    if (run.debugEnemyTarget > 0) run.pendingLevels = 0;
    upgrades::update(state);

    if (run.phase == RunPhase::Playing && state.player.hp <= 0.0f) {
        run.phase = RunPhase::Dead;
        pushEvent(state, EventType::PlayerDied, state.player.pos);
    }
}

uint64_t stateHash(const GameState& state) {
    // FNV-1a over the values that matter. Hashing fields one at a time avoids
    // reading uninitialized padding bytes inside the structs.
    uint64_t hash = 14695981039346656037ULL;
    const auto mixBytes = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    };
    const auto mixFloat = [&](float value) { mixBytes(&value, sizeof(value)); };
    const auto mixInt = [&](int value) { mixBytes(&value, sizeof(value)); };

    mixBytes(&state.rng.state, sizeof(state.rng.state));
    mixBytes(&state.run.tick, sizeof(state.run.tick));
    mixInt(state.run.kills);
    mixInt(static_cast<int>(state.run.phase));
    mixFloat(state.player.pos.x);
    mixFloat(state.player.pos.y);
    mixFloat(state.player.hp);
    mixInt(state.player.level);
    mixInt(state.player.xp);
    mixInt(state.enemies.count);
    for (const Enemy& enemy : state.enemies) {
        mixFloat(enemy.pos.x);
        mixFloat(enemy.pos.y);
        mixFloat(enemy.hp);
        mixBytes(&enemy.id, sizeof(enemy.id));
    }
    mixInt(state.projectiles.count);
    for (const Projectile& shot : state.projectiles) {
        mixFloat(shot.pos.x);
        mixFloat(shot.pos.y);
    }
    mixInt(state.enemyBullets.count);
    for (const EnemyBullet& bullet : state.enemyBullets) {
        mixFloat(bullet.pos.x);
        mixFloat(bullet.pos.y);
    }
    mixInt(state.zones.count);
    mixInt(state.gems.count);
    for (const Gem& gem : state.gems) {
        mixFloat(gem.pos.x);
        mixFloat(gem.pos.y);
        mixInt(gem.xp);
    }
    mixInt(state.pickups.count);
    return hash;
}

void setStressTargets(GameState& state, int enemies, int projectiles) {
    state.run.debugEnemyTarget = enemies;
    state.run.debugProjectileTarget = projectiles;
}

}  // namespace ls::sim
