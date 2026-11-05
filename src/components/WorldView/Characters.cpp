#include "components/WorldView/Characters.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Effects/Effects.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

// How fast each type steps through its walk frames: 256 means one frame per tick.
constexpr int kWalkRate[static_cast<int>(EnemyType::Count)] = {
    /* Walker */ 28, /* Runner */ 64, /* Brute */ 20, /* Spitter */ 30, /* Bloater */ 24, /* Crawler */ 40, /* Charger */ 34, /* Boss */ 22,
};

// Art pixels the figure takes up, for culling.
constexpr float kHeights[static_cast<int>(EnemyType::Count)] = {34, 26, 44, 44, 40, 18, 40, 150};
constexpr float kHalfWidths[static_cast<int>(EnemyType::Count)] = {16, 16, 24, 14, 22, 24, 20, 80};

constexpr float kEliteSizeBoost = 1.36f;  // Elites are bigger than their radius alone.

constexpr float kChargerDashLength = balance::kChargerDashSeconds * balance::kChargerDashSpeed;
constexpr float kBossDashLength = balance::kBossDashSpeed * balance::kBossDashSeconds;

// How a long sun shadow leans, for each figure height of shadow length.
constexpr float kShadowLeanX = 0.72f;
constexpr float kShadowLeanY = 0.26f;

// How far a warning line's arrows and edges stick out beside the line, in art pixels.
constexpr float kLineSideRoom = 12.0f;

Figure figureOf(const GameState& state, const Enemy& enemy) {
    if (enemy.type == EnemyType::Boss) return static_cast<Figure>(static_cast<int>(Figure::Boss1) + std::min<int>(state.run.bossPhase, 2));
    return static_cast<Figure>(static_cast<int>(enemy.type));
}

// The nearest pre-painted size. Comparing with midpoints keeps this free of any search.
int stepFor(float scale, int first, int last) {
    int step = first;
    while (step < last && scale > (kStepScale[step] + kStepScale[step + 1]) * 0.5f) ++step;
    return step;
}

int stepForEnemy(const Enemy& enemy, float scale) {
    if (enemy.type == EnemyType::Boss) return stepFor(scale, kStepBossFirst, kStepBossLast);
    if (enemy.elite) return stepFor(scale * kEliteSizeBoost, kStepEliteFirst, kScaleSteps - 1);
    return stepFor(scale, 0, kStepNormalLast);
}

// World directions pick one of three views. Faces toward the camera are the most
// readable, so a figure only turns its back when it is clearly below the target.
struct Heading {
    int facing;
    bool mirror;
};

Heading headingFrom(Vec2 dir) {
    const float across = std::abs(dir.x);
    const float along = std::abs(dir.y) * kTiltSquash;
    Heading heading;
    if (along > across * 0.9f) heading.facing = dir.y > 0.0f ? kFacingFront : kFacingBack;
    else heading.facing = kFacingSide;
    heading.mirror = dir.x < 0.0f;
    return heading;
}

struct ZombieLook {
    const SpriteRef* sprite;
    bool mirror;
    uint8_t row;
    uint8_t grade;
};

uint8_t outfitRow(uint32_t id) {
    return static_cast<uint8_t>(kRowOutfits + scramble(id * 2654435761u) % kOutfitCount);
}

ZombieLook lookOf(const GameState& state, const Enemy& enemy, const ScreenPoint& feet) {
    const uint32_t tick = state.run.tick;
    const Figure figure = figureOf(state, enemy);
    const int step = stepForEnemy(enemy, feet.scale);

    Vec2 toward = state.player.pos - enemy.pos;
    bool flashing = false;
    int pose;
    const int frames = enemy.type == EnemyType::Boss ? kBossWalkFrames : kWalkFrames;
    const int rate = kWalkRate[static_cast<int>(enemy.type)];
    pose = static_cast<int>(((static_cast<uint32_t>(rate) * tick + enemy.id * 4099u) >> 8) % static_cast<uint32_t>(frames));

    if (enemy.type == EnemyType::Spitter) {
        const float sinceFired = balance::kSpitterFireInterval - enemy.timer;
        if (sinceFired < 0.25f) pose = kPoseSpitRecoil;
        else if (enemy.timer < 0.4f) pose = kPoseSpitInflate;
    } else if (enemy.type == EnemyType::Charger) {
        if (enemy.state == enemies::kChargerTelegraph) {
            pose = (tick >> 2) & 1u ? kPoseBraceA : kPoseBraceB;
            toward = enemy.dir;
            // Where flicker is off, the warning color just stays on.
            flashing = !effects::flickerAllowed() || ((tick >> 2) & 1u) != 0u;
        } else if (enemy.state == enemies::kChargerDash) {
            pose = (tick >> 2) & 1u ? kPoseDashA : kPoseDashB;
            toward = enemy.dir;
        } else if (enemy.state == enemies::kChargerRecover) {
            pose = kPoseRecover;
            toward = enemy.dir;
        }
    } else if (enemy.type == EnemyType::Boss) {
        if (enemy.state == enemies::kBossRingCharge || enemy.state == enemies::kBossDashCharge) {
            pose = (tick >> 3) & 1u ? kPoseBraceA : kPoseBraceB;
            if (enemy.state == enemies::kBossDashCharge) toward = enemy.dir;
            flashing = !effects::flickerAllowed() || ((tick >> 2) & 1u) != 0u;
        } else if (enemy.state == enemies::kBossDash) {
            pose = kPoseDashA;
            toward = enemy.dir;
        }
    }

    const Heading heading = headingFrom(toward);
    ZombieLook look;
    look.sprite = &figureSprite(figure, step, heading.facing, pose);
    look.mirror = heading.mirror;
    look.grade = 255;

    if (enemy.type == EnemyType::Boss) {
        look.row = static_cast<uint8_t>(kRowBoss + std::min<int>(state.run.bossPhase, 2));
        if (flashing) look.row = kRowBossCharge;
    } else if (enemy.elite) {
        look.row = static_cast<uint8_t>(kRowElite + static_cast<int>(enemy.type));
    } else {
        look.row = outfitRow(enemy.id);
    }
    if (flashing && enemy.type != EnemyType::Boss) look.row = kRowHot;
    return look;
}

Item shadowFor(const Enemy& enemy) {
    switch (enemy.type) {
        case EnemyType::Runner: return Item::ShadowSmall;
        case EnemyType::Crawler: return Item::ShadowMedium;
        case EnemyType::Brute:
        case EnemyType::Bloater: return Item::ShadowLarge;
        case EnemyType::Boss: return Item::ShadowBoss;
        case EnemyType::Charger: return Item::ShadowMedium;
        default: return Item::ShadowMedium;
    }
}

}  // namespace

float figureHeight(const Enemy& enemy) {
    return kHeights[static_cast<int>(enemy.type)] * (enemy.elite ? 1.7f : 1.3f);
}

float figureHalfWidth(const Enemy& enemy) {
    return kHalfWidths[static_cast<int>(enemy.type)] * (enemy.elite ? 1.7f : 1.3f);
}

bool figureTouchesTarget(const View& view, const GameState& state, const Enemy& enemy, const ScreenPoint& feet) {
    const ZombieLook zombie = lookOf(state, enemy, feet);
    const int x = roundToInt(feet.x);
    const int y = roundToInt(feet.y);
    const int left = zombie.mirror ? x - (zombie.sprite->w - zombie.sprite->footX) : x - zombie.sprite->footX;
    const int top = y - zombie.sprite->footY;
    return left < view.width && left + zombie.sprite->w > 0 && top < view.height && top + zombie.sprite->h > 0;
}

void drawZombieShadow(const Enemy& enemy, const ScreenPoint& feet) {
    const SpriteRef& shadow = itemSprite(enemy.elite ? Item::ShadowHuge : shadowFor(enemy));
    spriteQuad(shadow, roundToInt(feet.x), roundToInt(feet.y), false, kRowShadow, 0);
}

void drawZombieLongShadow(const GameState& state, const Enemy& enemy, const ScreenPoint& feet, const DayLook& look) {
    const ZombieLook zombie = lookOf(state, enemy, feet);
    const float height = static_cast<float>(zombie.sprite->h);
    shadowQuad(*zombie.sprite, roundToInt(feet.x), roundToInt(feet.y), zombie.mirror, height * look.shadowLength * 0.72f, -height * look.shadowLength * 0.26f);
}

void drawZombie(const GameState& state, const Enemy& enemy, const ScreenPoint& feet) {
    const ZombieLook zombie = lookOf(state, enemy, feet);
    // The clock stops on menus and at the end of a run, so a flash would stay stuck.
    uint8_t flash = 0;
    if (enemy.flash > 0.0f && state.run.phase == RunPhase::Playing) {
        flash = effects::zombieFlash(enemy.flash, enemy.id, enemy.elite || enemy.type == EnemyType::Boss, enemy.type == EnemyType::Boss);
    }
    spriteQuad(*zombie.sprite, roundToInt(feet.x), roundToInt(feet.y), zombie.mirror, zombie.row, zombie.grade, 255, flash);
}

void drawEliteBar(const GameState& state, const Enemy& enemy, const ScreenPoint& feet) {
    // A small bar on whole pixels, just above the figure.
    const SpriteRef& sprite = figureSprite(figureOf(state, enemy), stepForEnemy(enemy, feet.scale), kFacingSide, 0);
    const float width = std::max(14.0f, static_cast<float>(sprite.w));
    const float left = static_cast<float>(roundToInt(feet.x - width * 0.5f));
    const float top = static_cast<float>(roundToInt(feet.y) - sprite.footY - 5);
    const float fraction = clampf(enemy.hp / std::max(1.0f, enemy.maxHp), 0.0f, 1.0f);
    const auto rect = [](float x, float y, float w, float h, Color color) {
        quad(Vec2{x, y}, Vec2{x, y + h}, Vec2{x + w, y + h}, Vec2{x + w, y}, color);
    };
    rect(left - 1.0f, top - 1.0f, width + 2.0f, 4.0f, theme::kEliteBarBack);
    rect(left, top, static_cast<float>(roundToInt(width * fraction)), 2.0f, theme::kEliteBarFill);
}

bool isTelegraphing(const Enemy& enemy) {
    if (enemy.type == EnemyType::Charger) return enemy.state == enemies::kChargerTelegraph;
    if (enemy.type == EnemyType::Boss) return enemy.state == enemies::kBossRingCharge || enemy.state == enemies::kBossDashCharge;
    return false;
}

namespace {

// A line can cross the picture even when both of its ends are outside, so this tests the
// box around the whole line. A diagonal that only clips the box corner draws for nothing,
// which is harmless, as the GPU cuts it away.
bool lineCouldShow(const View& view, const ScreenPoint& a, const ScreenPoint& b) {
    return std::max(a.x, b.x) > -kLineSideRoom && std::min(a.x, b.x) < static_cast<float>(view.width) + kLineSideRoom &&
           std::max(a.y, b.y) > -kLineSideRoom && std::min(a.y, b.y) < static_cast<float>(view.height) + kLineSideRoom;
}

}  // namespace

bool drawTelegraph(const View& view, const GameState& state, const Enemy& enemy, Vec2 worldFeet) {
    const uint32_t tick = state.run.tick;
    bool shown = false;

    if (enemy.type == EnemyType::Charger && enemy.state == enemies::kChargerTelegraph) {
        // A warning line along the locked direction, as far as the dash will reach.
        const Vec2 start = worldFeet;
        const Vec2 end = worldFeet + enemy.dir * kChargerDashLength;
        const ScreenPoint a = project(view, start);
        const ScreenPoint b = project(view, end);
        if (!lineCouldShow(view, a, b)) return false;
        shown = true;
        const bool flash = !effects::flickerAllowed() || ((tick >> 2) & 1u) != 0u;
        const Vec2 from{a.x, a.y};
        const Vec2 to{b.x, b.y};
        thickLine(from, to, 5.0f, fade(theme::kHostileDark, 0.75f));
        thickLine(from, to, 3.0f, fade(theme::kChargerLine, flash ? 1.0f : 0.65f));
        // Moving arrows along it, so the direction can't be misread.
        const Vec2 side = perpendicular(normalizedOr(to - from, Vec2{1.0f, 0.0f}));
        const Vec2 ahead = normalizedOr(to - from, Vec2{1.0f, 0.0f});
        constexpr int kArrows = 4;
        for (int i = 0; i < kArrows; ++i) {
            const float t = (static_cast<float>(i) + static_cast<float>(tick % 24u) / 24.0f) / static_cast<float>(kArrows);
            const Vec2 at = lerp(from, to, t);
            thickLine(at - ahead * 5.0f + side * 5.0f, at, 2.0f, theme::kHostileFront);
            thickLine(at - ahead * 5.0f - side * 5.0f, at, 2.0f, theme::kHostileFront);
        }
    }

    if (enemy.type == EnemyType::Boss && (enemy.state == enemies::kBossRingCharge || enemy.state == enemies::kBossDashCharge)) {
        // The ring tightens as the timer runs out, so the moment of attack can be read.
        const float t = clampf(enemy.timer / balance::kBossTelegraphSeconds, 0.0f, 1.0f);
        const float radius = enemy.radius * (1.3f + 1.2f * t);
        const ScreenPoint a = project(view, worldFeet);
        // Generous, because perspective swells the near half of a ring.
        const float ringReach = (radius + 10.0f) * kArtPerWorld * a.scale * 1.5f;
        if (onTarget(view, a, ringReach, ringReach, ringReach)) {
            shown = true;
            groundRing(view, worldFeet, radius, 10.0f, 3, fade(theme::kBossChargeRing, 1.0f - 0.4f * t));
        }
        if (enemy.state == enemies::kBossDashCharge) {
            const ScreenPoint b = project(view, worldFeet + enemy.dir * kBossDashLength);
            if (lineCouldShow(view, a, b)) {
                shown = true;
                thickLine(Vec2{a.x, a.y}, Vec2{b.x, b.y}, 7.0f, fade(theme::kHostileDark, 0.75f));
                const bool flash = !effects::flickerAllowed() || ((tick >> 2) & 1u) != 0u;
                thickLine(Vec2{a.x, a.y}, Vec2{b.x, b.y}, 5.0f, fade(theme::kChargerLine, flash ? 1.0f : 0.65f));
            }
        }
    }
    return shown;
}

// --- Soldier ------------------------------------------------------------------

namespace {

struct SoldierState {
    float stride = 0.0f;       // World units walked, which drives the walk cycle.
    Vec2 lastFeet;
    bool moving = false;
    float sinceFired = 10.0f;  // Seconds. Starts high, so no flash shows at first.
    bool firedThisFrame = false;  // A shot's first drawn frame must show it, however slow.
    WeaponId gun = WeaponId::Rifle;
    bool seenAny = false;
};

SoldierState g_soldierState;
int g_muzzleFrames = 0;  // Only read by the browser tests.

constexpr float kStrideLength = 62.0f;   // World units for one full walk cycle.
constexpr float kAimHoldSeconds = 0.8f;  // He faces his target this long after a shot.
constexpr float kRecoilSeconds = 0.08f;
constexpr float kRecoilPixels = 2.5f;
constexpr float kFlashSeconds = 0.05f;
constexpr float kShoulderHeight = 17.5f;
constexpr int kTrailGhosts = 4;          // Copies in a dash afterimage.
constexpr int kTrailGhostsSimple = 2;    // With reduced motion or flashes off.
constexpr float kTrailSpacing = 28.0f;   // World units between copies.
constexpr float kTrailAlpha = 140.0f;    // Strength of the nearest copy, out of 255.
constexpr uint8_t kTrailWash = 110;      // How far a copy mixes toward the pale flash color.

bool isHeldGun(WeaponId weapon) { return weapon != WeaponId::Blades && weapon != WeaponId::Landmines; }

WeaponId heldGun(const GameState& state) {
    if (g_soldierState.seenAny) return g_soldierState.gun;
    for (int i = 0; i < state.player.weaponCount; ++i) {
        if (isHeldGun(state.player.weapons[i].id)) return state.player.weapons[i].id;
    }
    return WeaponId::Rifle;
}

bool gunEvolved(const GameState& state, WeaponId gun) {
    for (int i = 0; i < state.player.weaponCount; ++i) {
        if (state.player.weapons[i].id == gun) return state.player.weapons[i].evolved;
    }
    return false;
}

// True while the gun in his hands is reloading.
bool reloading(const GameState& state, WeaponId gun) {
    for (int i = 0; i < state.player.weaponCount; ++i) {
        if (state.player.weapons[i].id == gun) return state.player.weapons[i].reloadLeft > 0.0f;
    }
    return false;
}

Heading soldierHeading(const GameState& state) {
    if (g_soldierState.sinceFired < kAimHoldSeconds) return headingFrom(state.player.aim);
    return headingFrom(g_soldierState.moving ? state.player.facing : state.player.aim);
}

// Index 0 aims straight up the screen, and the last straight down. Left is mirrored.
int gunAngleIndex(Vec2 aim) {
    const float angle = std::atan2(-aim.y * kTiltSquash, std::abs(aim.x));
    const float position = (kPi * 0.5f - angle) / kPi * static_cast<float>(kGunAngles - 1);
    return std::clamp(static_cast<int>(position + 0.5f), 0, kGunAngles - 1);
}

int bodyPose(uint32_t tick) {
    if (!g_soldierState.moving) return kSoldierIdlePose + static_cast<int>((tick / 40u) & 1u);
    const float cycle = g_soldierState.stride / kStrideLength;
    const float wrapped = cycle - std::floor(cycle);
    return std::min(kSoldierWalkFrames - 1, static_cast<int>(wrapped * static_cast<float>(kSoldierWalkFrames)));
}

}  // namespace

void noteWeaponFired(WeaponId weapon) {
    // Mines are dropped and blades circle by themselves, so neither moves the gun in his
    // hands. Treating them as shots would flash and kick a gun that never fired.
    if (!isHeldGun(weapon)) return;
    g_soldierState.sinceFired = 0.0f;
    g_soldierState.firedThisFrame = true;
    g_soldierState.gun = weapon;
    g_soldierState.seenAny = true;
}

void resetSoldierAnimation() { g_soldierState = SoldierState{}; }

SoldierDebug soldierDebug(const GameState& state) { return {static_cast<int>(heldGun(state)), g_muzzleFrames}; }

void updateSoldierAnimation(Vec2 feetWorld, float realDt) {
    SoldierState& s = g_soldierState;
    const float moved = distance(feetWorld, s.lastFeet);
    s.lastFeet = feetWorld;
    // A big jump means a new run or a debug move, not walking.
    if (moved < 40.0f) s.stride += moved;
    s.moving = moved > 0.05f;
    // A shot's clock starts after its first drawn frame. On a slow device one frame can be
    // longer than the whole flash, and counting it first would mean no flash is ever seen.
    if (s.firedThisFrame) s.firedThisFrame = false;
    else s.sinceFired += realDt;
}

void drawSoldierRing(const View& view, Vec2 worldFeet) {
    // A cool ring on the ground: he can be found at a glance in any crowd.
    groundRing(view, worldFeet, balance::kPlayerRadius * 1.9f, 5.0f, 3, theme::kPlayerRing);
    groundRing(view, worldFeet, balance::kPlayerRadius * 2.6f, 3.0f, 3, theme::kPlayerRingFaint);
}

void drawSoldierRingOverCrowd(const View& view, Vec2 worldFeet) {
    groundRing(view, worldFeet, balance::kPlayerRadius * 1.9f, 4.0f, 3, fade(theme::kPlayerRing, 0.7f));
}

void drawSoldierShadow(const GameState& state, const ScreenPoint& feet, const DayLook& look) {
    const int x = roundToInt(feet.x);
    const int y = roundToInt(feet.y);
    spriteQuad(itemSprite(Item::ShadowMedium), x, y, false, kRowShadow, 0);
    if (look.shadowLength > 0.02f) {
        const Heading heading = soldierHeading(state);
        const SpriteRef& body = soldierBody(heading.facing, bodyPose(state.run.tick));
        const float height = static_cast<float>(body.h);
        shadowQuad(body, x, y, heading.mirror, height * look.shadowLength * 0.72f, -height * look.shadowLength * 0.26f);
    }
}

void drawSoldier(const GameState& state, const ScreenPoint& feet, uint8_t strength) {
    const Player& player = state.player;
    const uint32_t tick = state.run.tick;
    const int x = roundToInt(feet.x);
    const int y = roundToInt(feet.y);
    const Heading heading = soldierHeading(state);
    if (state.run.phase == RunPhase::Dead) {
        spriteQuad(soldierBody(kFacingSide, kSoldierDeadPose), x, y, heading.mirror, kRowBase, 150, strength);
        return;
    }
    const bool playing = state.run.phase == RunPhase::Playing;
    const bool safe = playing && player.invulnerable > 0.0f;
    // A blink while hits can't land, so that safe moment can be seen. Where flicker is
    // off, he goes a little see-through for that moment instead.
    if (safe && effects::flickerAllowed() && ((tick >> 2) & 1u) != 0u) return;
    if (safe && !effects::flickerAllowed()) strength = static_cast<uint8_t>(strength / 2 + strength / 4);

    const uint8_t flash = playing && player.hitFlash > 0.0f ? effects::soldierFlash(player.hitFlash) : 0;
    const uint8_t row = kRowBase;
    const uint8_t grade = 150;
    const SpriteRef& body = soldierBody(heading.facing, bodyPose(tick));

    const WeaponId gun = heldGun(state);
    const uint8_t gunRow = gunEvolved(state, gun) ? kRowEvolved : kRowBase;
    const bool isReloading = reloading(state, gun);

    const Vec2 aim = player.aim;
    const float aimX = aim.x;
    const float aimY = aim.y * kTiltSquash;
    const float aimLength = std::max(0.001f, std::sqrt(aimX * aimX + aimY * aimY));
    const float t = clampf(1.0f - g_soldierState.sinceFired / kRecoilSeconds, 0.0f, 1.0f);
    const float recoil = isReloading ? 0.0f : t * kRecoilPixels;
    const int gunX = x - roundToInt(aimX / aimLength * recoil);
    const int gunY = y - roundToInt(kShoulderHeight) - roundToInt(aimY / aimLength * recoil);

    const int angleIndex = gunAngleIndex(aim);
    const SpriteRef& gunSprite = isReloading ? soldierReload(gun, static_cast<int>((tick >> 4) & 1u)) : soldierGun(gun, angleIndex);
    const bool gunMirror = isReloading ? heading.mirror : aim.x < 0.0f;
    // Aiming up the screen, the gun is on the far side of his body.
    const bool gunBehind = !isReloading && angleIndex <= 2;
    if (gunBehind) spriteQuad(gunSprite, gunX, gunY, gunMirror, gunRow, grade, strength, flash);
    spriteQuad(body, x, y, heading.mirror, row, grade, strength, flash);
    if (!gunBehind) spriteQuad(gunSprite, gunX, gunY, gunMirror, gunRow, grade, strength, flash);
}

// The afterimage of a dash: a few pale copies of his body strung out behind him. Where they
// stand comes straight from the dash state, so the renderer keeps no list of old positions.
// Reduced motion or no flicker keeps only two, steady and without the fade-out blink.
void drawDashTrail(const View& view, const GameState& state, Vec2 playerWorld) {
    const Player& player = state.player;
    if (player.dashLeft <= 0.0f || state.run.phase != RunPhase::Playing) return;
    const bool simple = !effects::flickerAllowed();
    const int ghosts = simple ? kTrailGhostsSimple : kTrailGhosts;
    const float travelled = (balance::kDashSeconds - player.dashLeft) * balance::kDashSpeed;
    const Heading heading = soldierHeading(state);
    const SpriteRef& body = soldierBody(heading.facing, bodyPose(state.run.tick));
    // Far ghosts first, so nearer ones draw over them.
    for (int i = ghosts; i >= 1; --i) {
        const float back = static_cast<float>(i) * kTrailSpacing;
        if (back > travelled) continue;
        const ScreenPoint at = project(view, playerWorld - player.dashDir * back);
        const float strength = 1.0f - static_cast<float>(i - 1) / static_cast<float>(ghosts);
        spriteQuad(body, roundToInt(at.x), roundToInt(at.y), heading.mirror, kRowBase, 150, static_cast<uint8_t>(kTrailAlpha * strength), kTrailWash);
    }
}

void drawMuzzleFlash(const GameState& state, const ScreenPoint& feet) {
    if (g_soldierState.sinceFired > kFlashSeconds || state.run.phase == RunPhase::Dead) return;
    // With rapid fire this is a strobe, however small, so it is the first thing to go.
    const float light = effects::flashScale();
    if (effects::reducedMotion() || light <= 0.0f) return;
    const WeaponId gun = heldGun(state);
    if (gun == WeaponId::Flamethrower || gun == WeaponId::Tesla) return;
    if (reloading(state, gun)) return;
    const Vec2 aim = state.player.aim;
    const float along = 1.0f - 0.28f * std::abs(aim.y);
    const float length = gunLength(gun) * along + 1.0f;
    const float aimY = aim.y * kTiltSquash;
    const float aimLength = std::max(0.001f, std::sqrt(aim.x * aim.x + aimY * aimY));
    const int x = roundToInt(feet.x + aim.x / aimLength * length);
    const int y = roundToInt(feet.y - kShoulderHeight + aimY / aimLength * length);
    const bool big = gun == WeaponId::Shotgun || gun == WeaponId::Sniper || gun == WeaponId::Rockets || gun == WeaponId::GrenadeLauncher;
    const bool evolved = gunEvolved(state, gun);
    g_muzzleFrames++;
    spriteQuad(itemSprite(big ? Item::MuzzleBig : Item::MuzzleSmall), x, y, false, evolved ? kRowEvolved : kRowBase, 0, static_cast<uint8_t>(255.0f * light));
    effects::queueGlowSpot(static_cast<float>(x), static_cast<float>(y), big ? 34.0f : 22.0f, evolved ? theme::kFxGlowEvolved : theme::kFxGlowMuzzle, (big ? 0.8f : 0.6f) * light);
}

// --- Corpses ------------------------------------------------------------------

namespace {

struct Corpse {
    Vec2 world;
    uint32_t bornTick = 0;
    EnemyType type = EnemyType::Walker;
    bool used = false;
    bool mirror = false;
    uint8_t row = 0;
};

constexpr int kMaxCorpses = 240;
constexpr uint32_t kFallTicks = 18;
constexpr uint32_t kLieTicks = 200;
constexpr uint32_t kFadeTicks = 60;

Corpse g_corpses[kMaxCorpses];
int g_nextCorpse = 0;

}  // namespace

void addCorpse(EnemyType type, Vec2 pos, uint32_t tick) {
    // A full pool just reuses the oldest slot, so a big fight can't use more memory.
    Corpse& corpse = g_corpses[g_nextCorpse];
    g_nextCorpse = (g_nextCorpse + 1) % kMaxCorpses;
    const uint32_t hash = hashPosition(pos);
    corpse.world = pos;
    corpse.type = type;
    corpse.used = true;
    corpse.bornTick = tick;
    corpse.mirror = (hash & 1u) != 0u;
    corpse.row = type == EnemyType::Boss ? kRowBoss : static_cast<uint8_t>(kRowOutfits + (hash >> 4) % kOutfitCount);
}

void drawCorpses(const View& view, uint32_t tick) {
    for (Corpse& corpse : g_corpses) {
        if (!corpse.used) continue;
        if (tick < corpse.bornTick) {  // A new run has started.
            corpse.used = false;
            continue;
        }
        const uint32_t age = tick - corpse.bornTick;
        if (age >= kFallTicks + kLieTicks + kFadeTicks) {
            corpse.used = false;
            continue;
        }
        const ScreenPoint feet = project(view, corpse.world);
        if (!onTarget(view, feet, 50.0f, 40.0f, 30.0f)) continue;
        const int frame = age >= kFallTicks ? kCorpseFrames - 1 : static_cast<int>(age * kCorpseFrames / kFallTicks);
        const Figure figure = corpse.type == EnemyType::Boss ? Figure::Boss1 : static_cast<Figure>(static_cast<int>(corpse.type));
        const int step = corpse.type == EnemyType::Boss ? stepFor(feet.scale, kStepBossFirst, kStepBossLast) : stepFor(feet.scale, 0, kStepNormalLast);
        const SpriteRef& sprite = corpseSprite(figure, step, frame);
        float alpha = 1.0f;
        if (age > kFallTicks + kLieTicks) alpha = 1.0f - static_cast<float>(age - kFallTicks - kLieTicks) / static_cast<float>(kFadeTicks);
        spriteQuad(sprite, roundToInt(feet.x), roundToInt(feet.y), corpse.mirror, corpse.row, 255, static_cast<uint8_t>(alpha * 255.0f));
    }
}

}  // namespace ls::worldview
