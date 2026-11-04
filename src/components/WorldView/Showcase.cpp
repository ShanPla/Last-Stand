#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"
#include "components/WorldView/Characters.h"
#include "components/WorldView/WorldView.h"
#include "data/Balance.h"

#include "raylib.h"

#include <cmath>

// Debug only. The real weapons and zombies aren't needed to see every shape: this
// writes them straight into the pools, then holds the simulation still. Nothing
// here runs unless the page was opened with the debug flag.
namespace ls::worldview {

namespace {

constexpr uint32_t kGunChangeTicks = 80;
constexpr WeaponId kGuns[] = {WeaponId::Rifle, WeaponId::Shotgun, WeaponId::Smg, WeaponId::Sniper,
                              WeaponId::Flamethrower, WeaponId::GrenadeLauncher, WeaponId::Rockets, WeaponId::Tesla};
constexpr int kGunCount = static_cast<int>(sizeof(kGuns) / sizeof(kGuns[0]));

// Positions that fill rows from a start corner.
struct Rows {
    float startX, startY, spacingX, spacingY;
    int columns;
    int used = 0;
    Vec2 next() {
        const int column = used % columns;
        const int row = used / columns;
        used++;
        return {startX + spacingX * static_cast<float>(column), startY + spacingY * static_cast<float>(row)};
    }
};

// Where everything goes, so that the same list of objects fits both screen shapes.
struct Layout {
    Rows zombies;
    Vec2 boss;
    Vec2 chargeDir;
    Rows shots;
    Vec2 blastA, blastB;
    Vec2 beamFrom, beamTo;
    Vec2 teslaA, teslaB, teslaC;
    Vec2 circle;
    Vec2 lineFrom, lineTo;
    Rows gems;
    Rows drops;
};

Layout wideLayout() {
    return Layout{
        Rows{-540.0f, -190.0f, 135.0f, -130.0f, 9},
        Vec2{-450.0f, 230.0f},
        Vec2{1.0f, 0.05f},
        Rows{-190.0f, 170.0f, 75.0f, 70.0f, 8},
        Vec2{500.0f, -30.0f}, Vec2{500.0f, 110.0f},
        Vec2{30.0f, 310.0f}, Vec2{390.0f, 310.0f},
        Vec2{30.0f, 390.0f}, Vec2{200.0f, 360.0f}, Vec2{390.0f, 390.0f},
        Vec2{-100.0f, 385.0f},
        Vec2{430.0f, 240.0f}, Vec2{620.0f, 450.0f},
        Rows{190.0f, -70.0f, 50.0f, 0.0f, 5},
        Rows{190.0f, 10.0f, 66.0f, 0.0f, 4},
    };
}

Layout tallLayout() {
    return Layout{
        Rows{-236.0f, -180.0f, 118.0f, -130.0f, 5},
        Vec2{0.0f, -585.0f},
        Vec2{-1.0f, -0.2f},
        Rows{-240.0f, 290.0f, 80.0f, 70.0f, 7},
        Vec2{-190.0f, 550.0f}, Vec2{-20.0f, 550.0f},
        Vec2{-270.0f, 670.0f}, Vec2{270.0f, 670.0f},
        Vec2{-270.0f, 720.0f}, Vec2{0.0f, 700.0f}, Vec2{270.0f, 720.0f},
        Vec2{170.0f, 570.0f},
        Vec2{-280.0f, 780.0f}, Vec2{280.0f, 830.0f},
        Rows{-120.0f, 155.0f, 60.0f, 0.0f, 5},
        Rows{-100.0f, 225.0f, 66.0f, 0.0f, 4},
    };
}

Enemy* placeEnemy(GameState& state, EnemyType type, Vec2 pos, bool elite) {
    Enemy* enemy = enemies::spawn(state, type, pos, elite);
    if (!enemy) return nullptr;
    // A small fake step, so the renderer treats it as walking and plays its gait.
    const float toward = pos.x < state.player.pos.x ? 0.2f : -0.2f;
    enemy->prevPos = pos - Vec2{toward, 0.0f};
    enemy->dir = normalizedOr(state.player.pos - pos, Vec2{1.0f, 0.0f});
    return enemy;
}

void placeShot(GameState& state, ProjectileKind kind, Vec2 pos, Vec2 dir, float speed, float radius, float life, WeaponId source, bool evolved) {
    Projectile* shot = state.projectiles.add();
    if (!shot) return;
    shot->kind = kind;
    shot->pos = pos;
    shot->prevPos = pos;
    shot->vel = dir * speed;
    shot->radius = radius;
    shot->life = life;
    shot->source = source;
    shot->evolved = evolved;
}

void placeBullet(GameState& state, Vec2 pos) {
    EnemyBullet* bullet = state.enemyBullets.add();
    if (!bullet) return;
    bullet->pos = pos;
    bullet->prevPos = pos;
    bullet->vel = Vec2{0.0f, 200.0f};
    bullet->radius = balance::kEnemyBulletRadius;
    bullet->life = balance::kEnemyBulletLife;
}

void placeZone(GameState& state, ZoneKind kind, Vec2 a, Vec2 b, float radius, float maxRadius, float life, float maxLife, bool hostile, bool evolved) {
    Zone* zone = state.zones.add();
    if (!zone) return;
    zone->kind = kind;
    zone->a = a;
    zone->b = b;
    zone->radius = radius;
    zone->prevRadius = radius;
    zone->maxRadius = maxRadius;
    zone->life = life;
    zone->maxLife = maxLife;
    zone->hostile = hostile;
    zone->evolved = evolved;
}

void addMixedCrowd(GameState& state, int count) {
    // A spiral with the golden angle spreads points evenly with no random numbers.
    constexpr float kGoldenAngle = 2.39996f;
    constexpr int kCrowdTypes = static_cast<int>(EnemyType::Boss);
    for (int i = 0; i < count; ++i) {
        const float distanceOut = 110.0f + 14.0f * std::sqrt(static_cast<float>(i));
        const Vec2 pos = fromAngle(kGoldenAngle * static_cast<float>(i)) * distanceOut;
        placeEnemy(state, static_cast<EnemyType>(i % kCrowdTypes), pos, false);
    }
}

void placeZombies(GameState& state, Layout& layout) {
    Rows& row = layout.zombies;
    for (int i = 0; i < static_cast<int>(EnemyType::Boss); ++i) {
        placeEnemy(state, static_cast<EnemyType>(i), row.next(), false);
    }
    placeEnemy(state, EnemyType::Walker, row.next(), true);
    placeEnemy(state, EnemyType::Brute, row.next(), true);
    if (Enemy* charger = placeEnemy(state, EnemyType::Charger, row.next(), false)) charger->state = enemies::kChargerDash;
    if (Enemy* charger = placeEnemy(state, EnemyType::Charger, row.next(), false)) charger->state = enemies::kChargerRecover;
    // A timer near the full interval means it has only just spat.
    if (Enemy* spitter = placeEnemy(state, EnemyType::Spitter, row.next(), false)) spitter->timer = balance::kSpitterFireInterval - 0.05f;
    if (Enemy* hurt = placeEnemy(state, EnemyType::Walker, row.next(), false)) hurt->flash = 100.0f;

    // A bullet right on top of a zombie, to test that it stays readable.
    const Vec2 busy = row.next();
    placeEnemy(state, EnemyType::Runner, busy, false);
    placeBullet(state, busy);

    if (Enemy* charger = placeEnemy(state, EnemyType::Charger, row.next(), false)) {
        charger->state = enemies::kChargerTelegraph;
        charger->dir = normalizedOr(layout.chargeDir, Vec2{1.0f, 0.0f});
    }
    placeEnemy(state, EnemyType::Boss, layout.boss, false);
}

void placeShots(GameState& state, Layout& layout) {
    Rows& shots = layout.shots;
    const Vec2 right{1.0f, 0.0f};
    const Vec2 slant = normalizedOr(Vec2{1.0f, -0.5f}, right);
    const float fuse = balance::kWeaponLevels[static_cast<int>(WeaponId::GrenadeLauncher)][0].duration;
    const float flameLife = balance::kWeaponLevels[static_cast<int>(WeaponId::Flamethrower)][0].duration;
    placeShot(state, ProjectileKind::Bullet, shots.next(), right, 700.0f, balance::kBulletRadius, 1.0f, WeaponId::Rifle, false);
    placeShot(state, ProjectileKind::Bullet, shots.next(), slant, 700.0f, balance::kBulletRadius, 1.0f, WeaponId::Rifle, true);
    placeShot(state, ProjectileKind::Bullet, shots.next(), slant, 560.0f, balance::kPelletRadius, 0.4f, WeaponId::Shotgun, false);
    placeShot(state, ProjectileKind::Flame, shots.next(), right, 340.0f, balance::kFlameRadius, flameLife, WeaponId::Flamethrower, false);
    placeShot(state, ProjectileKind::Flame, shots.next(), right, 340.0f, balance::kFlameRadius, flameLife * 0.4f, WeaponId::Flamethrower, false);
    placeShot(state, ProjectileKind::Flame, shots.next(), right, 340.0f, balance::kFlameRadius, flameLife * 0.7f, WeaponId::Flamethrower, true);
    placeShot(state, ProjectileKind::Grenade, shots.next(), right, 340.0f, balance::kGrenadeRadius, fuse * 0.5f, WeaponId::GrenadeLauncher, false);
    placeShot(state, ProjectileKind::Rocket, shots.next(), slant, 330.0f, balance::kRocketRadius, 3.0f, WeaponId::Rockets, false);
    placeShot(state, ProjectileKind::Rocket, shots.next(), Vec2{-0.7f, 0.7f}, 330.0f, balance::kRocketRadius, 3.0f, WeaponId::Rockets, true);
    placeShot(state, ProjectileKind::Mine, shots.next(), Vec2{}, 0.0f, balance::kMineTriggerRadius, 7.0f, WeaponId::Landmines, false);
    placeShot(state, ProjectileKind::Mine, shots.next(), Vec2{}, 0.0f, balance::kMineTriggerRadius, 7.0f, WeaponId::Landmines, true);
    for (int i = 0; i < 3; ++i) placeBullet(state, shots.next());
}

void placeZones(GameState& state, const Layout& layout) {
    const float blastLife = balance::kExplosionSeconds;
    const float telegraph = balance::kBossTelegraphSeconds;
    placeZone(state, ZoneKind::Explosion, layout.blastA, Vec2{}, 58.0f, 90.0f, blastLife * 0.6f, blastLife, false, false);
    placeZone(state, ZoneKind::Explosion, layout.blastB, Vec2{}, 58.0f, 90.0f, blastLife * 0.6f, blastLife, false, true);
    placeZone(state, ZoneKind::SniperBeam, layout.beamFrom, layout.beamTo, 7.0f, 7.0f, 0.1f, 0.14f, false, false);
    placeZone(state, ZoneKind::TeslaLink, layout.teslaA, layout.teslaB, 0.0f, 0.0f, 0.16f, 0.18f, false, false);
    placeZone(state, ZoneKind::TeslaLink, layout.teslaB, layout.teslaC, 0.0f, 0.0f, 0.16f, 0.18f, false, true);
    const float circleRadius = balance::kBossCircleRadius * 0.6f;
    placeZone(state, ZoneKind::BossCircle, layout.circle, layout.circle, circleRadius, circleRadius, telegraph * 0.5f, telegraph, true, false);
    placeZone(state, ZoneKind::BossLine, layout.lineFrom, layout.lineTo, balance::kBossLineHalfWidth, balance::kBossLineHalfWidth,
              telegraph * 0.5f, telegraph, true, false);
}

void placeRewards(GameState& state, Layout& layout) {
    const int gemValues[] = {1, 3, 5, 12, 40};
    for (const int xp : gemValues) {
        if (Gem* gem = state.gems.add()) {
            gem->pos = layout.gems.next();
            gem->prevPos = gem->pos;
            gem->xp = xp;
        }
    }
    for (int i = 0; i < static_cast<int>(PickupType::Count); ++i) {
        if (Pickup* pickup = state.pickups.add()) {
            pickup->pos = layout.drops.next();
            pickup->type = static_cast<PickupType>(i);
        }
    }
}

}  // namespace

void setupShowcase(GameState& state, int extraZombies) {
    state.enemies.clear();
    state.projectiles.clear();
    state.enemyBullets.clear();
    state.zones.clear();
    state.gems.clear();
    state.pickups.clear();
    state.run.phase = RunPhase::Playing;
    state.run.pendingLevels = 0;
    state.run.debugEnemyTarget = 0;
    state.run.debugProjectileTarget = 0;
    state.run.bossPhase = 0;

    Player& player = state.player;
    player.pos = Vec2{};
    player.prevPos = Vec2{};
    player.hp = player.maxHp;
    player.invulnerable = 0.0f;
    player.hitFlash = 0.0f;
    player.aim = normalizedOr(Vec2{1.0f, 0.3f}, Vec2{1.0f, 0.0f});
    // Blades go first, since only a few weapons can be held. The timer in
    // tickShowcase shows the rest of the guns in the soldier's hands.
    player.weaponCount = 0;
    weapons::grant(state, WeaponId::Blades);
    weapons::grant(state, WeaponId::Rifle);
    weapons::grant(state, WeaponId::Shotgun);
    weapons::grant(state, WeaponId::Sniper);
    weapons::grant(state, WeaponId::Rockets);
    weapons::grant(state, WeaponId::Tesla);
    player.weapons[0].level = 3;

    if (extraZombies > 0) {
        addMixedCrowd(state, extraZombies);
        return;
    }
    Layout layout = GetScreenWidth() >= GetScreenHeight() ? wideLayout() : tallLayout();
    placeZombies(state, layout);
    placeShots(state, layout);
    placeZones(state, layout);
    placeRewards(state, layout);
}

void tickShowcase(GameState& state) {
    RunState& run = state.run;
    run.tick++;
    run.bladeAngle += balance::kWeaponLevels[static_cast<int>(WeaponId::Blades)][0].speed * kDt;

    // Loops every timed object, so the fading and filling can be seen.
    for (Zone& zone : state.zones) {
        zone.life -= kDt * 0.4f;
        if (zone.life <= 0.0f) zone.life = zone.maxLife;
        if (zone.kind == ZoneKind::Explosion) zone.radius = zone.maxRadius * (1.0f - zone.life / zone.maxLife * 0.8f);
    }
    for (Projectile& shot : state.projectiles) {
        if (shot.kind != ProjectileKind::Grenade) continue;
        shot.life -= kDt * 0.5f;
        if (shot.life <= 0.0f) shot.life = balance::kWeaponLevels[static_cast<int>(WeaponId::GrenadeLauncher)][0].duration;
    }

    // Steps through each gun, turning the aim and reloading now and then, so that
    // screenshots taken at different times show all of the soldier's looks.
    const uint32_t beat = run.tick / kGunChangeTicks;
    const WeaponId gun = kGuns[beat % static_cast<uint32_t>(kGunCount)];
    const float aimAngle = kTau * static_cast<float>(beat % 8u) / 8.0f + 0.3f;
    state.player.aim = fromAngle(aimAngle);
    if (run.tick % 8u == 0u) pushEvent(state, EventType::WeaponFired, state.player.pos, 0.0f, static_cast<uint8_t>(gun));
    for (int i = 0; i < state.player.weaponCount; ++i) {
        WeaponSlot& slot = state.player.weapons[i];
        slot.reloadLeft = (beat % 3u == 2u) ? 1.0f : 0.0f;
        slot.evolved = slot.id == WeaponId::Blades && beat % 2u == 1u;
    }

    // Each phase of the boss gets its turn, and a charge-up in between.
    run.bossPhase = static_cast<uint8_t>((beat / 3u) % 3u);
    for (Enemy& enemy : state.enemies) {
        if (enemy.type == EnemyType::Boss) enemy.state = beat % 4u == 3u ? enemies::kBossRingCharge : enemies::kBossNextCircles;
    }
}

}  // namespace ls::worldview
