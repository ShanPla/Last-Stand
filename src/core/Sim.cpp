#include "core/Sim.h"

#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Pickups/Pickups.h"
#include "components/Player/Player.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"

#include <cstring>
#include <type_traits>

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

void newRun(GameState& state, uint64_t seed, Difficulty difficulty) {
    state.rng = Rng{};
    state.rng.seed(seed);
    state.run = RunState{};
    state.run.seed = seed;
    state.run.difficulty = difficulty;
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
    player::update(state, input.moveX, input.moveY, input.dash);
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
    // One entry point for every number, flag and enum. Each is widened to a fixed
    // size first, so the hash does not depend on how a struct is laid out.
    const auto mix = [&](auto value) {
        using T = decltype(value);
        if constexpr (std::is_floating_point_v<T>) {
            const float narrowed = static_cast<float>(value);
            mixBytes(&narrowed, sizeof(narrowed));
        } else {
            const int64_t wide = static_cast<int64_t>(value);  // Integers, bools and enums.
            mixBytes(&wide, sizeof(wide));
        }
    };
    const auto mixVec = [&](Vec2 v) { mix(v.x); mix(v.y); };

    // Everything below can change what a later step does. What is left out is only
    // for drawing: previous positions (derived from the last step), hit flashes,
    // the event list (cleared by its reader) and the enemy grid (rebuilt each step).
    // A new field that a step reads must be added here, or the determinism tests
    // cannot see it.
    mixBytes(&state.rng.state, sizeof(state.rng.state));
    mixBytes(&state.rng.stream, sizeof(state.rng.stream));

    const RunState& run = state.run;
    mix(run.phase);
    mix(run.difficulty);
    mixBytes(&run.seed, sizeof(run.seed));
    mix(run.tick);
    mix(run.nextId);
    mix(run.kills);
    mix(run.rerollsLeft);
    mix(run.pendingLevels);
    mix(run.offerCount);
    for (const Card& card : run.offers) {
        mix(card.kind);
        mix(card.weapon);
        mix(card.passive);
        mix(card.newLevel);
    }
    for (float damage : run.damageByWeapon) mix(damage);
    for (float owed : run.waveAccumulators) mix(owed);
    mix(run.nextEliteEvent);
    mix(run.nextPickupDropKills);
    mix(run.bladeAngle);
    mix(run.bossSpawned);
    mix(run.bossPhase);
    mix(run.bossHpFraction);
    mix(run.debugEnemyTarget);
    mix(run.debugProjectileTarget);

    const Player& player = state.player;
    mixVec(player.pos);
    mixVec(player.facing);
    mixVec(player.aim);
    mix(player.hp);
    mix(player.maxHp);
    mix(player.invulnerable);
    mix(player.dashLeft);
    mix(player.dashCooldown);
    mix(player.dashInvulnerable);
    mixVec(player.dashDir);
    mix(player.level);
    mix(player.xp);
    mix(player.xpToNext);
    mix(player.stats.damageMult);
    mix(player.stats.cooldownMult);
    mix(player.stats.areaMult);
    mix(player.stats.moveSpeed);
    mix(player.stats.pickupRange);
    mix(player.stats.armor);
    mix(player.weaponCount);
    for (int i = 0; i < player.weaponCount; ++i) {
        const WeaponSlot& slot = player.weapons[i];
        mix(slot.id);
        mix(slot.level);
        mix(slot.evolved);
        mix(slot.cooldown);
        mix(slot.magazine);
        mix(slot.reserve);
        mix(slot.reloadLeft);
        mix(slot.dryReported);
    }
    mix(player.passiveCount);
    for (int i = 0; i < player.passiveCount; ++i) {
        mix(player.passives[i].id);
        mix(player.passives[i].level);
    }

    mix(state.enemies.count);
    for (const Enemy& enemy : state.enemies) {
        mixVec(enemy.pos);
        mixVec(enemy.knock);
        mixVec(enemy.dir);
        mix(enemy.hp);
        mix(enemy.maxHp);
        mix(enemy.radius);
        mix(enemy.speed);
        mix(enemy.contactDamage);
        mix(enemy.timer);
        mix(enemy.bladeCooldown);
        mix(enemy.id);
        mix(enemy.type);
        mix(enemy.state);
        mix(enemy.elite);
    }
    mix(state.projectiles.count);
    for (const Projectile& shot : state.projectiles) {
        mixVec(shot.pos);
        mixVec(shot.vel);
        mix(shot.damage);
        mix(shot.radius);
        mix(shot.life);
        mix(shot.area);
        mix(shot.lastHitId);
        mix(shot.pierce);
        mix(shot.kind);
        mix(shot.source);
        mix(shot.evolved);
    }
    mix(state.enemyBullets.count);
    for (const EnemyBullet& bullet : state.enemyBullets) {
        mixVec(bullet.pos);
        mixVec(bullet.vel);
        mix(bullet.damage);
        mix(bullet.radius);
        mix(bullet.life);
    }
    mix(state.zones.count);
    for (const Zone& zone : state.zones) {
        mixVec(zone.a);
        mixVec(zone.b);
        mix(zone.radius);
        mix(zone.prevRadius);  // Read by ring zones to find who the ring just crossed.
        mix(zone.maxRadius);
        mix(zone.life);
        mix(zone.maxLife);
        mix(zone.damage);
        mix(zone.kind);
        mix(zone.source);
        mix(zone.evolved);
        mix(zone.hostile);
        mix(zone.applied);
    }
    mix(state.gems.count);
    for (const Gem& gem : state.gems) {
        mixVec(gem.pos);
        mix(gem.xp);
        mix(gem.attracted);
    }
    mix(state.pickups.count);
    for (const Pickup& pickup : state.pickups) {
        mixVec(pickup.pos);
        mix(pickup.type);
    }
    return hash;
}

void setStressTargets(GameState& state, int enemies, int projectiles) {
    state.run.debugEnemyTarget = enemies;
    state.run.debugProjectileTarget = projectiles;
}

}  // namespace ls::sim
