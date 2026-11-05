#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Enemies.h"
#include "components/Maps/Layout/Layout.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Weapons/Weapons.h"
#include "core/Math.h"
#include "components/WorldView/Showcase.h"
#include "components/WorldView/WorldView.h"
#include "components/WorldView/Draw.h"
#include "data/Balance.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

// Debug only. The real weapons and zombies aren't needed to see every shape: this
// writes them straight into the pools, then holds the simulation still. Nothing
// here runs unless the page was opened with the debug flag.
namespace ls::worldview {

namespace {

constexpr uint32_t kGunChangeTicks = 80;
bool g_clockStopped = false;  // For comparing pictures of the ground.
int g_sheetPage = -1;         // Which sprite sheet is showing, if any.
constexpr WeaponId kGuns[] = {WeaponId::Rifle, WeaponId::Shotgun, WeaponId::Smg, WeaponId::Sniper,
                              WeaponId::Flamethrower, WeaponId::GrenadeLauncher, WeaponId::Rockets, WeaponId::Tesla};
constexpr int kShownGunCount = static_cast<int>(sizeof(kGuns) / sizeof(kGuns[0]));

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
        Vec2{-150.0f, -470.0f}, Vec2{160.0f, -470.0f},
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
        Rows{-192.0f, -150.0f, 96.0f, -130.0f, 5},
        Vec2{0.0f, -585.0f},
        Vec2{-1.0f, -0.2f},
        Rows{-192.0f, 290.0f, 64.0f, 70.0f, 7},
        Vec2{-110.0f, 480.0f}, Vec2{70.0f, 480.0f},
        Vec2{-190.0f, 560.0f}, Vec2{190.0f, 560.0f},
        Vec2{-190.0f, 620.0f}, Vec2{0.0f, 600.0f}, Vec2{190.0f, 620.0f},
        Vec2{130.0f, 500.0f},
        Vec2{-190.0f, 660.0f}, Vec2{190.0f, 690.0f},
        Rows{-120.0f, 130.0f, 60.0f, 0.0f, 5},
        Rows{-100.0f, 200.0f, 66.0f, 0.0f, 4},
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

    // A stream of flames leaving the soldier, each older than the last.
    constexpr int kStream = 12;
    for (int i = 0; i < kStream; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kStream);
        const Vec2 dir = normalizedOr(Vec2{-1.0f, 0.25f}, right);
        const Vec2 pos = dir * (40.0f + 170.0f * t) + Vec2{static_cast<float>(i % 3) * 6.0f, static_cast<float>(i % 2) * 10.0f};
        placeShot(state, ProjectileKind::Flame, pos, dir, 340.0f, balance::kFlameRadius, flameLife * (1.0f - t), WeaponId::Flamethrower, i > 7);
    }
}

void placeZones(GameState& state, const Layout& layout) {
    const float blastLife = balance::kExplosionSeconds;
    const balance::BossMoves& moves = balance::bossMoves(state.run.difficulty);
    const float telegraph = moves.telegraphSeconds;
    placeZone(state, ZoneKind::Explosion, layout.blastA, Vec2{}, 58.0f, 90.0f, blastLife * 0.6f, blastLife, false, false);
    placeZone(state, ZoneKind::Explosion, layout.blastB, Vec2{}, 58.0f, 90.0f, blastLife * 0.6f, blastLife, false, true);
    placeZone(state, ZoneKind::SniperBeam, layout.beamFrom, layout.beamTo, 7.0f, 7.0f, 0.1f, 0.14f, false, false);
    placeZone(state, ZoneKind::TeslaLink, layout.teslaA, layout.teslaB, 0.0f, 0.0f, 0.16f, 0.18f, false, false);
    placeZone(state, ZoneKind::TeslaLink, layout.teslaB, layout.teslaC, 0.0f, 0.0f, 0.16f, 0.18f, false, true);
    const float circleRadius = moves.circleRadius * 0.6f;
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

// Stands a zombie this many art pixels past the left edge of the picture, level with the
// player. The zombie is outside by more than the renderer's culling margin, so anything
// drawn for it can only come from the telegraph pass.
Enemy* placeOffLeft(GameState& state, EnemyType type, float artPixels, Vec2 dir) {
    const View& view = currentView();
    const Vec2 world = unproject(view, -artPixels, view.anchorY) - Vec2{view.focusX, view.focusY};
    Enemy* enemy = placeEnemy(state, type, world, false);
    if (!enemy) return nullptr;
    enemy->prevPos = world;
    enemy->dir = dir;
    return enemy;
}

// The first car seen side-on in widening rings around the start, which is far from the player.
bool findSideCar(const GameState& state, maps::Obstacle* found) {
    maps::Obstacle list[maps::kMaxObstaclesPerTile];
    for (int ring = 1; ring < 12; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                const int count = maps::obstaclesInTile(state.run.map, state.run.seed, x, y, list, maps::kMaxObstaclesPerTile);
                for (int i = 0; i < count; ++i) {
                    if (list[i].sprite != maps::CarSprite::CitySide && list[i].sprite != maps::CarSprite::WreckSide) continue;
                    *found = list[i];
                    return true;
                }
            }
        }
    }
    return false;
}

// The first car in widening rings around the start that passes a test.
template <class Test>
bool findCar(const GameState& state, maps::Obstacle* found, Test&& wanted) {
    maps::Obstacle list[maps::kMaxObstaclesPerTile];
    for (int ring = 1; ring < 40; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                const int count = maps::obstaclesInTile(state.run.map, state.run.seed, x, y, list, maps::kMaxObstaclesPerTile);
                for (int i = 0; i < count; ++i) {
                    if (!wanted(list[i])) continue;
                    *found = list[i];
                    return true;
                }
            }
        }
    }
    return false;
}

void standBesideCar(GameState& state, const maps::Obstacle& car) {
    state.player.pos = car.center + Vec2{-car.half.x - balance::kPlayerRadius - 2.0f, 0.0f};
    state.player.prevPos = state.player.pos;
}

void placeCarScene(GameState& state, int scene) {
    maps::Obstacle car;
    if (!findSideCar(state, &car)) return;
    const float radius = balance::kEnemyStats[static_cast<int>(EnemyType::Walker)].radius;
    state.player.pos = car.center + Vec2{-car.half.x - balance::kPlayerRadius - 2.0f, 0.0f};
    state.player.prevPos = state.player.pos;
    // Fixed ids, because a zombie's clothes come from its id, and tests compare pictures.
    const auto place = [&](Vec2 pos, uint32_t id) {
        if (Enemy* enemy = placeEnemy(state, EnemyType::Walker, pos, false)) enemy->id = id;
    };
    if (scene == kSceneCarBehind || scene == kSceneCarBoth) place(car.center + Vec2{0.0f, -car.half.y - radius}, 1u);
    if (scene == kSceneCarFront || scene == kSceneCarBoth) place(car.center + Vec2{car.half.x * 0.5f, car.half.y + radius}, 2u);
}

void placeTestScene(GameState& state, int scene) {
    const Vec2 right{1.0f, 0.0f};
    maps::Obstacle chosen;
    if (scene <= kSceneCarOfSprite && scene > kSceneCarOfSprite - 8) {
        const auto sprite = static_cast<maps::CarSprite>(kSceneCarOfSprite - scene);
        if (findCar(state, &chosen, [&](const maps::Obstacle& car) { return car.sprite == sprite; })) standBesideCar(state, chosen);
        return;
    }
    if (scene == kSceneCarOverEdge) {
        const auto overEdge = [](const maps::Obstacle& car) {
            const maps::TexelRect rect = maps::spriteRect(car);
            return std::floor(static_cast<float>(rect.x0) / maps::kChunkWidth) != std::floor(static_cast<float>(rect.x1 - 1) / maps::kChunkWidth) ||
                   std::floor(static_cast<float>(rect.y0) / maps::kChunkHeight) != std::floor(static_cast<float>(rect.y1 - 1) / maps::kChunkHeight);
        };
        if (findCar(state, &chosen, overEdge)) standBesideCar(state, chosen);
        return;
    }
    if (scene <= kSceneCarEmpty && scene >= kSceneCarBoth) {
        placeCarScene(state, scene);
        return;
    }
    if (scene == kSceneChargerOffLeft || scene == kSceneChargerAway) {
        Enemy* charger = placeOffLeft(state, EnemyType::Charger, 120.0f, scene == kSceneChargerOffLeft ? right : Vec2{-1.0f, 0.0f});
        if (charger) charger->state = enemies::kChargerTelegraph;
    } else if (scene == kSceneBossOffLeft) {
        Enemy* boss = placeOffLeft(state, EnemyType::Boss, 130.0f, right);
        if (boss) {
            boss->state = enemies::kBossDashCharge;
            boss->timer = balance::bossMoves(state.run.difficulty).telegraphSeconds;
        }
    } else if (scene == kSceneBossSpiral || scene == kSceneBossCross || scene == kSceneBossTrail) {
        // The real attack code starts each one, so the test sees what play would show. Only
        // Hell's move set has numbers for every attack, so these scenes play Hell.
        state.run.difficulty = Difficulty::Hell;
        static constexpr balance::BossAttack kAttack[] = {balance::BossAttack::Trail, balance::BossAttack::Cross, balance::BossAttack::Spiral};
        Enemy* boss = placeOffLeft(state, EnemyType::Boss, 130.0f, right);
        if (boss) {
            state.run.bossSpawned = true;
            enemies::startBossAttack(state, *boss, kAttack[scene - kSceneBossTrail]);
        }
    } else if (scene == kSceneWalkerOffLeft) {
        placeOffLeft(state, EnemyType::Walker, 30.0f, right);
    } else if (scene == kSceneOnlyMines) {
        state.player.weaponCount = 0;
        weapons::grant(state, WeaponId::Landmines);
        placeEnemy(state, EnemyType::Walker, Vec2{balance::kWeaponRange * 0.5f, 0.0f}, false);
    }
}

}  // namespace

// Debug only: lays out frames of the atlas, to judge the pixels of every figure.
void drawSpriteSheet() {
    if (g_sheetPage < 0) return;
    constexpr uint8_t kOutfit[6] = {kRowOutfits + 1, kRowOutfits + 6, kRowOutfits + 11, kRowOutfits + 16, kRowOutfits + 21, kRowOutfits + 26};
    if (g_sheetPage == 0) {
        // Every zombie type: side walk, then three frames each from the front and back.
        constexpr int kRowSteps[7] = {44, 38, 56, 54, 50, 26, 46};
        int y = 2;
        for (int f = 0; f < 7; ++f) {
            y += kRowSteps[f];
            int x = 26;
            for (int facing = 0; facing < kFacingCount; ++facing) {
                const int count = facing == kFacingSide ? kWalkFrames : 3;
                for (int pose = 0; pose < count; ++pose) {
                    const int shown = facing == kFacingSide ? pose : pose * 2;
                    spriteQuad(figureSprite(static_cast<Figure>(f), 2, facing, shown), x, y, false, kOutfit[(pose + f) % 6]);
                    x += f == 2 || f == 4 ? 52 : 44;
                }
            }
        }
    } else if (g_sheetPage == 1) {
        // Special poses, elites and corpses.
        int x = 26;
        for (int pose = kPoseSpecial; pose < kPoseSpecial + 2; ++pose) {
            for (int facing = 0; facing < kFacingCount; ++facing) {
                spriteQuad(figureSprite(Figure::Spitter, 2, facing, pose), x, 50, false, kOutfit[2]);
                x += 32;
            }
        }
        x = 26;
        for (int pose = kPoseSpecial; pose < kPoseCount; ++pose) {
            spriteQuad(figureSprite(Figure::Charger, 2, kFacingSide, pose), x, 100, false, kOutfit[3]);
            spriteQuad(figureSprite(Figure::Charger, 2, kFacingFront, pose), x + 36, 100, false, kOutfit[3]);
            spriteQuad(figureSprite(Figure::Charger, 2, kFacingBack, pose), x + 72, 100, pose == kPoseBraceB, kRowHot);
            x += 108;
        }
        x = 26;
        for (int f : {0, 2, 3, 4, 6}) {
            spriteQuad(figureSprite(static_cast<Figure>(f), 5, kFacingSide, 1), x, 170, false, kRowElite + static_cast<uint8_t>(f));
            spriteQuad(figureSprite(static_cast<Figure>(f), 5, kFacingFront, 1), x + 56, 170, false, kRowElite + static_cast<uint8_t>(f));
            x += 112;
        }
        x = 26;
        for (int f = 0; f < 7; ++f) {
            for (int frame = 0; frame < kCorpseFrames; ++frame) {
                spriteQuad(corpseSprite(static_cast<Figure>(f), 2, frame), x, 214 + (f % 2) * 30, false, kOutfit[f % 6]);
                x += 30;
            }
        }
        x = 26;
        for (int step = 0; step <= kStepNormalLast; ++step) {
            spriteQuad(figureSprite(Figure::Walker, step, kFacingFront, 0), x, 330, false, kOutfit[0]);
            spriteQuad(figureSprite(Figure::Walker, step, kFacingSide, 0), x + 30, 330, false, kOutfit[1]);
            spriteQuad(figureSprite(Figure::Brute, step, kFacingFront, 0), x + 76, 330, false, kOutfit[2]);
            x += 124;
        }
    } else if (g_sheetPage == 2) {
        // The boss: each phase from each side, then its special poses.
        for (int phase = 0; phase < 3; ++phase) {
            const Figure figure = static_cast<Figure>(static_cast<int>(Figure::Boss1) + phase);
            const int y = 112 + phase * 118;
            const uint8_t row = static_cast<uint8_t>(kRowBoss + phase);
            spriteQuad(figureSprite(figure, 2, kFacingSide, 0), 62, y, false, row);
            spriteQuad(figureSprite(figure, 2, kFacingFront, 1), 186, y, false, row);
            spriteQuad(figureSprite(figure, 2, kFacingBack, 2), 316, y, false, row);
            spriteQuad(figureSprite(figure, 2, kFacingFront, kPoseBraceA), 446, y, false, kRowBossCharge);
            spriteQuad(figureSprite(figure, 2, kFacingSide, kPoseDashA), 576, y, false, row);
        }
    } else if (g_sheetPage == 3) {
        // The soldier: walk in all three views, then each gun at a few angles.
        for (int facing = 0; facing < kFacingCount; ++facing) {
            for (int pose = 0; pose < kSoldierPoses; ++pose) {
                spriteQuad(soldierBody(facing, pose), 22 + pose * 28, 44 + facing * 44, false, kRowBase, 0);
            }
        }
        for (int gun = 0; gun < kGunCount; ++gun) {
            const int y = 170 + (gun / 5) * 90;
            const int x = 30 + (gun % 5) * 124;
            spriteQuad(soldierBody(kFacingSide, 0), x, y, false, kRowBase, 0);
            spriteQuad(soldierGun(static_cast<WeaponId>(gun), 4), x, y - 18, false, kRowBase, 0);
            spriteQuad(soldierBody(kFacingFront, 0), x + 44, y, false, kRowBase, 0);
            spriteQuad(soldierGun(static_cast<WeaponId>(gun), 7), x + 44, y - 18, false, kRowBase, 0);
            spriteQuad(soldierReload(static_cast<WeaponId>(gun), 0), x + 76, y - 18, false, kRowBase, 0);
        }
    }
}

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

    g_clockStopped = extraZombies < 0;
    g_sheetPage = extraZombies <= -2 && extraZombies > kSceneChargerOffLeft ? -2 - extraZombies : -1;
    if (extraZombies <= kSceneChargerOffLeft) placeTestScene(state, extraZombies);
    if (extraZombies < 0) return;
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

void crushNearestCar(GameState& state) {
    obstacles::update(state);
    maps::Obstacle list[maps::kMaxObstaclesPerTile];
    const Vec2 at = state.player.pos;
    const int count = maps::obstaclesInTile(state.run.map, state.run.seed, maps::tileOf(at.x), maps::tileOf(at.y), list, maps::kMaxObstaclesPerTile);
    float nearest = 1.0e30f;
    Vec2 center;
    for (int i = 0; i < count; ++i) {
        if (maps::CrushedCars{state.run.crushed, state.run.crushedCount}.holds(list[i].texelX, list[i].texelY)) continue;
        const float gap = std::max(std::abs(at.x - list[i].center.x) - list[i].half.x, 0.0f) + std::max(std::abs(at.y - list[i].center.y) - list[i].half.y, 0.0f);
        if (gap < nearest) {
            nearest = gap;
            center = list[i].center;
        }
    }
    if (nearest < 1.0e30f) obstacles::crushUnder(state, center, 1.0f);
}

void placeBossBesideCar(GameState& state) {
    maps::Obstacle car;
    if (!findSideCar(state, &car)) return;
    state.enemies.clear();
    state.run.bossSpawned = true;
    const float radius = balance::kEnemyStats[static_cast<int>(EnemyType::Boss)].radius;
    state.player.pos = car.center + Vec2{car.half.x + 170.0f, 0.0f};
    state.player.prevPos = state.player.pos;
    obstacles::update(state);
    obstacles::resolveCircle(state, state.player.pos, balance::kPlayerRadius);
    state.player.prevPos = state.player.pos;
    // Far enough that the picture has settled by the time it arrives, so a measurement of the
    // crush doesn't catch the chunks painted for the jump to this place.
    enemies::spawn(state, EnemyType::Boss, car.center + Vec2{-car.half.x - radius - 110.0f, 0.0f}, false);
}

void tickShowcase(GameState& state) {
    if (g_clockStopped) return;
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
    const WeaponId gun = kGuns[beat % static_cast<uint32_t>(kShownGunCount)];
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
        if (enemy.type == EnemyType::Boss) enemy.state = beat % 4u == 3u ? enemies::kBossRingCharge : enemies::kBossWalk;
    }
}

}  // namespace ls::worldview
