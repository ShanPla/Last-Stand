#include "components/Weapons/Weapons.h"

#include "components/Enemies/Enemies.h"
#include "components/Player/Player.h"
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

#include <algorithm>
#include <cmath>

namespace ls::weapons {

namespace {

const balance::Evolution* evolutionFor(WeaponId id) {
    for (const balance::Evolution& evolution : balance::kEvolutions) {
        if (evolution.weapon == id) return &evolution;
    }
    return nullptr;
}

// Returns true when the weapon really fired. A weapon with no target returns
// false, so it keeps its ammo and its cooldown and simply waits.
bool fire(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    switch (slot.id) {
        case WeaponId::Rifle: return fireRifle(state, owner, slot, stats);
        case WeaponId::Shotgun: return fireShotgun(state, owner, slot, stats);
        case WeaponId::Smg: return fireSmg(state, owner, slot, stats);
        case WeaponId::Sniper: return fireSniper(state, owner, slot, stats);
        case WeaponId::Flamethrower: return fireFlamethrower(state, owner, slot, stats);
        case WeaponId::GrenadeLauncher: return fireGrenadeLauncher(state, owner, slot, stats);
        case WeaponId::Rockets: return fireRockets(state, owner, slot, stats);
        case WeaponId::Landmines: return fireLandmines(state, owner, slot, stats);
        case WeaponId::Tesla: return fireTesla(state, owner, slot, stats);
        case WeaponId::Blades:
        case WeaponId::Count: break;
    }
    return false;
}

// One blast from a grenade, rocket or mine: a visible ring, plus damage to every zombie
// inside, once, right now. The damage is applied at creation so the zone only has to be
// drawn afterwards. The shot's owner gets the credit.
void explode(GameState& state, const Projectile& shot) {
    const Vec2 center = shot.pos;
    const float radius = shot.area;
    state.enemyGrid.query(center, radius + balance::kLargestEnemyRadius, [&](int index) {
        const Enemy& enemy = state.enemies[index];
        if (enemy.hp <= 0.0f) return false;
        const float reach = radius + enemy.radius;
        if (distanceSq(enemy.pos, center) >= reach * reach) return false;
        const Vec2 outward = normalizedOr(enemy.pos - center, Vec2{1.0f, 0.0f});
        enemies::hit(state, index, shot.damage, shot.source, shot.owner, outward * balance::kAreaKnockback);
        return false;
    });
    const WeaponId source = shot.source;
    // Sent even when the blast hits nothing, so that a lone mine still makes a sound.
    pushEvent(state, EventType::Explosion, center, radius, static_cast<uint8_t>(source));
    if (Zone* zone = state.zones.add()) {
        zone->kind = ZoneKind::Explosion;
        zone->a = center;
        zone->b = center;
        zone->radius = radius;
        zone->prevRadius = radius;
        zone->maxRadius = radius;
        zone->life = balance::kExplosionSeconds;
        zone->maxLife = balance::kExplosionSeconds;
        zone->damage = shot.damage;
        zone->source = source;
        zone->evolved = shot.evolved;
        zone->owner = shot.owner;
        zone->applied = true;
    }
}

// Moves a bullet or flame and checks it against nearby enemies.
// Returns true when the shot is used up.
bool updateShot(GameState& state, Projectile& shot) {
    shot.prevPos = shot.pos;
    shot.pos += shot.vel * kDt;
    shot.life -= kDt;
    if (shot.life <= 0.0f) return true;

    bool usedUp = false;
    state.enemyGrid.query(shot.pos, shot.radius + balance::kLargestEnemyRadius, [&](int index) {
        const Enemy& enemy = state.enemies[index];
        if (enemy.hp <= 0.0f || enemy.id == shot.lastHitId) return false;
        const float touch = enemy.radius + shot.radius;
        if (distanceSq(enemy.pos, shot.pos) >= touch * touch) return false;
        const Vec2 knockback = normalizedOr(shot.vel, Vec2{}) * balance::kShotKnockback;
        enemies::hit(state, index, shot.damage, shot.source, shot.owner, knockback);
        shot.lastHitId = enemy.id;
        if (--shot.pierce < 0) {
            usedUp = true;
            return true;
        }
        return false;
    });
    return usedUp;
}

// Grenades fly over everything and only explode when the fuse runs out.
bool updateGrenade(GameState& state, Projectile& shot) {
    shot.prevPos = shot.pos;
    shot.pos += shot.vel * kDt;
    shot.life -= kDt;
    if (shot.life > 0.0f) return false;
    explode(state, shot);
    return true;
}

// Rockets steer toward the nearest zombie, limited by a turn rate so they swing
// in wide curves. They explode on contact, or when their life ends.
bool updateRocket(GameState& state, Projectile& shot) {
    const float speed = length(shot.vel);
    const int target = enemies::nearest(state, shot.pos, balance::kWeaponRange);
    if (target >= 0 && speed > 0.0f) {
        const Vec2 wanted = state.enemies[target].pos - shot.pos;
        const float cross = shot.vel.x * wanted.y - shot.vel.y * wanted.x;
        const float angleToTarget = std::atan2(cross, dot(shot.vel, wanted));
        const float maxTurn = balance::kRocketTurnRate * kDt;
        const float turn = clampf(angleToTarget, -maxTurn, maxTurn);
        const float c = std::cos(turn);
        const float s = std::sin(turn);
        shot.vel = Vec2{shot.vel.x * c - shot.vel.y * s, shot.vel.x * s + shot.vel.y * c};
    }
    shot.prevPos = shot.pos;
    shot.pos += shot.vel * kDt;
    shot.life -= kDt;

    bool touched = false;
    state.enemyGrid.query(shot.pos, shot.radius + balance::kLargestEnemyRadius, [&](int index) {
        const Enemy& enemy = state.enemies[index];
        if (enemy.hp <= 0.0f) return false;
        const float touch = enemy.radius + shot.radius;
        if (distanceSq(enemy.pos, shot.pos) >= touch * touch) return false;
        touched = true;
        return true;
    });
    if (!touched && shot.life > 0.0f) return false;
    explode(state, shot);
    return true;
}

// Mines sit still. They go off when a zombie steps close, or vanish when old.
bool updateMine(GameState& state, Projectile& shot) {
    shot.life -= kDt;
    if (shot.life <= 0.0f) return true;
    bool triggered = false;
    state.enemyGrid.query(shot.pos, shot.radius + balance::kLargestEnemyRadius, [&](int index) {
        const Enemy& enemy = state.enemies[index];
        if (enemy.hp <= 0.0f) return false;
        const float reach = balance::kMineTriggerRadius + enemy.radius;
        if (distanceSq(enemy.pos, shot.pos) >= reach * reach) return false;
        triggered = true;
        return true;
    });
    if (!triggered) return false;
    explode(state, shot);
    return true;
}

bool updateProjectile(GameState& state, Projectile& shot) {
    switch (shot.kind) {
        case ProjectileKind::Bullet:
        case ProjectileKind::Flame: return updateShot(state, shot);
        case ProjectileKind::Grenade: return updateGrenade(state, shot);
        case ProjectileKind::Rocket: return updateRocket(state, shot);
        case ProjectileKind::Mine: return updateMine(state, shot);
    }
    return true;
}

// A boss attack zone hits once, when its telegraph ends, and hurts every player inside it.
// A downed player can't be hurt, so it doesn't matter that they lie inside.
void applyHostileZone(GameState& state, const Zone& zone) {
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        const Vec2 playerPos = state.players[slot].pos;
        bool inside = false;
        if (zone.kind == ZoneKind::BossCircle) {
            inside = distanceSq(playerPos, zone.a) <= zone.radius * zone.radius;
        } else {
            inside = distanceToSegmentSq(playerPos, zone.a, zone.b) <= zone.radius * zone.radius;
        }
        if (inside) player::damage(state, slot, zone.damage);
    }
}

void updateZones(GameState& state) {
    for (int i = state.zones.count - 1; i >= 0; --i) {
        Zone& zone = state.zones[i];
        zone.life -= kDt;
        if (zone.life > 0.0f) continue;
        const bool bossAttack = zone.hostile && (zone.kind == ZoneKind::BossCircle || zone.kind == ZoneKind::BossLine);
        if (bossAttack) applyHostileZone(state, zone);
        state.zones.removeAt(i);
    }
}

void startReload(GameState& state, int owner, WeaponSlot& slot, const balance::WeaponLevel& stats) {
    slot.reloadLeft = stats.reload;
    pushPlayerEvent(state, EventType::ReloadStarted, owner, state.players[owner].pos, stats.reload, static_cast<uint8_t>(slot.id));
}

// Moves rounds from the reserve into the magazine.
void finishReload(WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const int moved = std::min(stats.magazine - slot.magazine, slot.reserve);
    slot.magazine += moved;
    slot.reserve -= moved;
    slot.reloadLeft = 0.0f;
    slot.dryReported = false;
}

void updateSlot(GameState& state, int owner, WeaponSlot& slot) {
    balance::WeaponLevel stats = effectiveStats(state.players[owner], slot);

    // Combat Blades are always spinning, so they skip the cooldown and ammo logic.
    if (slot.id == WeaponId::Blades) {
        updateBlades(state, owner, slot, stats);
        return;
    }

    // A waiting gun may bank at most one step of readiness, so it cannot fire
    // a burst of catch-up shots once a target appears.
    slot.cooldown = std::max(slot.cooldown - kDt, -kDt);

    const bool usesAmmo = stats.magazine > 0;
    bool freeShot = false;
    if (usesAmmo) {
        if (slot.reloadLeft > 0.0f) {
            slot.reloadLeft -= kDt;
            if (slot.reloadLeft > 0.0f) return;
            finishReload(slot, stats);
        }
        if (slot.magazine <= 0) {
            if (slot.reserve > 0) {
                startReload(state, owner, slot, stats);
                return;
            }
            if (!slot.dryReported) {
                slot.dryReported = true;
                pushPlayerEvent(state, EventType::WeaponDry, owner, state.players[owner].pos, 0.0f, static_cast<uint8_t>(slot.id));
            }
            // Only the Rifle keeps firing, slowly and weakly, so the player can always fight back.
            if (slot.id != WeaponId::Rifle) return;
            freeShot = true;
            stats.damage *= balance::kRifleDryDamageMult;
            stats.cooldown *= balance::kRifleDryCooldownMult;
            stats.count = 1;
        }
    }

    if (slot.cooldown > 0.0f) return;
    if (!fire(state, owner, slot, stats)) return;
    if (usesAmmo && !freeShot) slot.magazine--;
    slot.cooldown += stats.cooldown;
}

}  // namespace

void markFired(GameState& state, int owner, const WeaponSlot& slot, Vec2 aim) {
    Player& player = state.players[owner];
    player.aim = aim;
    pushPlayerEvent(state, EventType::WeaponFired, owner, player.pos, 0.0f, static_cast<uint8_t>(slot.id));
}

int nearestEnemyExcluding(const GameState& state, Vec2 pos, float range, const int* excluded, int excludedCount) {
    int best = -1;
    float bestDistSq = range * range;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        if (enemy.hp <= 0.0f) continue;
        bool skip = false;
        for (int e = 0; e < excludedCount; ++e) {
            if (excluded[e] == i) skip = true;
        }
        if (skip) continue;
        const float distSq = distanceSq(enemy.pos, pos);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = i;
        }
    }
    return best;
}

float distanceToSegmentSq(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 along = b - a;
    const float lenSq = lengthSq(along);
    if (lenSq < 1e-8f) return distanceSq(point, a);
    const float t = clampf(dot(point - a, along) / lenSq, 0.0f, 1.0f);
    return distanceSq(point, a + along * t);
}

void grant(GameState& state, int owner, WeaponId id) {
    Player& player = state.players[owner];
    if (player.weaponCount >= kMaxHeldWeapons) return;
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == id) return;
    }
    WeaponSlot& slot = player.weapons[player.weaponCount++];
    slot = WeaponSlot{};
    slot.id = id;
    refill(player, slot);
}

void refill(const Player& owner, WeaponSlot& slot) {
    const balance::WeaponLevel stats = effectiveStats(owner, slot);
    slot.magazine = stats.magazine;
    slot.reserve = stats.maxReserve;
    slot.reloadLeft = 0.0f;
    slot.dryReported = false;
}

void addAmmoFraction(GameState& state, int owner, float fraction) {
    Player& player = state.players[owner];
    for (int i = 0; i < player.weaponCount; ++i) {
        WeaponSlot& slot = player.weapons[i];
        const balance::WeaponLevel stats = effectiveStats(player, slot);
        if (stats.magazine <= 0) continue;
        const int added = static_cast<int>(std::lround(fraction * static_cast<float>(stats.maxReserve)));
        slot.reserve = std::min(stats.maxReserve, slot.reserve + added);
        slot.dryReported = false;
    }
}

float estimatedBossDps(const Player& player) {
    float total = 0.0f;
    for (int i = 0; i < player.weaponCount; ++i) {
        const WeaponSlot& slot = player.weapons[i];
        const balance::WeaponLevel stats = effectiveStats(player, slot);
        // Lightning jumps away and only a blade pass counts, so these hit one target once
        // per cycle. For every other weapon, each bullet in a trigger pull can land.
        const bool singleHit = slot.id == WeaponId::Tesla || slot.id == WeaponId::Blades;
        const float hits = singleHit ? 1.0f : static_cast<float>(stats.count);
        total += stats.damage * hits / stats.cooldown * balance::kBossDamageShare[static_cast<int>(slot.id)];
    }
    return total;
}

balance::WeaponLevel effectiveStats(const Player& owner, const WeaponSlot& slot) {
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
    const PlayerStats& passives = owner.stats;
    stats.damage *= passives.damageMult;
    stats.cooldown *= passives.cooldownMult;
    stats.area *= passives.areaMult;
    return stats;
}

void update(GameState& state) {
    for (int owner = 0; owner < state.playerCount(); ++owner) {
        Player& player = state.players[owner];
        // A downed player can't fight, and in co-op a player choosing a card holds fire.
        if (player.downed || player.cardsOpen) continue;
        for (int i = 0; i < player.weaponCount; ++i) updateSlot(state, owner, player.weapons[i]);
    }

    for (int i = state.projectiles.count - 1; i >= 0; --i) {
        if (updateProjectile(state, state.projectiles[i])) state.projectiles.removeAt(i);
    }
    updateZones(state);
}

}  // namespace ls::weapons
