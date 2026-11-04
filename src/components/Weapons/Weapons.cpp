#include "components/Weapons/Weapons.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Blades/Blades.h"
#include "components/Weapons/Flamethrower/Flamethrower.h"
#include "components/Weapons/GrenadeLauncher/GrenadeLauncher.h"
#include "components/Weapons/Landmines/Landmines.h"
#include "components/Weapons/Rifle/Rifle.h"
#include "components/Weapons/Rockets/Rockets.h"
#include "components/Weapons/Shotgun/Shotgun.h"
#include "components/Weapons/Smg/Smg.h"
#include "components/Weapons/Sniper/Sniper.h"
#include "components/Weapons/Tesla/Tesla.h"

#include <cmath>

namespace ls::weapons {

namespace {

const balance::Evolution* evolutionFor(WeaponId id) {
    for (const balance::Evolution& evolution : balance::kEvolutions) {
        if (evolution.weapon == id) return &evolution;
    }
    return nullptr;
}

void fire(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    switch (slot.id) {
        case WeaponId::Rifle: fireRifle(state, slot, stats); break;
        case WeaponId::Shotgun: fireShotgun(state, slot, stats); break;
        case WeaponId::Smg: fireSmg(state, slot, stats); break;
        case WeaponId::Sniper: fireSniper(state, slot, stats); break;
        case WeaponId::Flamethrower: fireFlamethrower(state, slot, stats); break;
        case WeaponId::GrenadeLauncher: fireGrenadeLauncher(state, slot, stats); break;
        case WeaponId::Rockets: fireRockets(state, slot, stats); break;
        case WeaponId::Landmines: fireLandmines(state, slot, stats); break;
        case WeaponId::Tesla: fireTesla(state, slot, stats); break;
        case WeaponId::Blades: fireBlades(state, slot, stats); break;
        case WeaponId::Count: break;
    }
}

// Moves one shot and checks it against nearby enemies.
// Returns true when the shot is used up.
bool updateShot(GameState& state, Projectile& shot) {
    shot.prevPos = shot.pos;
    shot.pos += shot.vel * kDt;
    shot.life -= kDt;
    if (shot.life <= 0.0f) return true;

    bool usedUp = false;
    const float largestEnemyRadius = 87.0f;
    state.enemyGrid.query(shot.pos, shot.radius + largestEnemyRadius, [&](int index) {
        const Enemy& enemy = state.enemies[index];
        if (enemy.hp <= 0.0f || enemy.id == shot.lastHitId) return false;
        const float touch = enemy.radius + shot.radius;
        if (distanceSq(enemy.pos, shot.pos) >= touch * touch) return false;
        const Vec2 knockback = normalizedOr(shot.vel, Vec2{}) * balance::kShotKnockback;
        enemies::hit(state, index, shot.damage, shot.source, knockback);
        shot.lastHitId = enemy.id;
        if (--shot.pierce < 0) {
            usedUp = true;
            return true;
        }
        return false;
    });
    return usedUp;
}

}  // namespace

void grant(GameState& state, WeaponId id) {
    Player& player = state.player;
    if (player.weaponCount >= kMaxHeldWeapons) return;
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == id) return;
    }
    WeaponSlot& slot = player.weapons[player.weaponCount++];
    slot = WeaponSlot{};
    slot.id = id;
}

void refill(GameState&, WeaponSlot&) {}

void addAmmoFraction(GameState&, float) {}

balance::WeaponLevel effectiveStats(const GameState& state, const WeaponSlot& slot) {
    balance::WeaponLevel stats = balance::kWeaponLevels[static_cast<int>(slot.id)][slot.level - 1];
    if (slot.evolved) {
        if (const balance::Evolution* evolution = evolutionFor(slot.id)) {
            stats.damage *= evolution->damageMult;
            stats.cooldown *= evolution->cooldownMult;
            stats.count += evolution->countBonus;
            stats.area *= evolution->areaMult;
            stats.pierce += evolution->pierceBonus;
        }
    }
    const PlayerStats& player = state.player.stats;
    stats.damage *= player.damageMult;
    stats.cooldown *= player.cooldownMult;
    stats.area *= player.areaMult;
    return stats;
}

void update(GameState& state) {
    Player& player = state.player;
    for (int i = 0; i < player.weaponCount; ++i) {
        WeaponSlot& slot = player.weapons[i];
        slot.cooldown -= kDt;
        if (slot.cooldown <= 0.0f) {
            const balance::WeaponLevel stats = effectiveStats(state, slot);
            fire(state, slot, stats);
            slot.cooldown += stats.cooldown;
        }
    }

    for (int i = state.projectiles.count - 1; i >= 0; --i) {
        if (updateShot(state, state.projectiles[i])) state.projectiles.removeAt(i);
    }
}

}  // namespace ls::weapons
