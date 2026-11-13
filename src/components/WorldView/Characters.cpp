#include "components/WorldView/Characters.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Effects/Effects.h"
#include "components/WorldView/DashPath.h"
#include "components/WorldView/ShieldBubble.h"
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

// The spiral warning's arms, in world units past the charge ring, and how many straight
// pieces draw each bend.
constexpr float kSpiralArmLength = 560.0f;
constexpr int kSpiralArmPoints = 8;

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

    Vec2 toward = state.players[enemy.target < state.playerCount() ? enemy.target : 0].pos - enemy.pos;
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
        if (enemies::bossCharging(enemy)) {
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

float longShadowReach(const Enemy& enemy, float shadowLength) { return figureHeight(enemy) * shadowLength * kShadowLeanX; }

bool figureTouchesTarget(const View& view, const GameState& state, const Enemy& enemy, const ScreenPoint& feet) {
    const ZombieLook zombie = lookOf(state, enemy, feet);
    const int x = roundToInt(feet.x);
    const int y = roundToInt(feet.y);
    const int left = zombie.mirror ? x - (zombie.sprite->w - zombie.sprite->footX) : x - zombie.sprite->footX;
    const int top = y - zombie.sprite->footY;
    return left < view.width && left + zombie.sprite->w > 0 && top < view.height && top + zombie.sprite->h > 0;
}

void drawZombieShadow(const GameState& state, const Enemy& enemy, const ScreenPoint& feet) {
    const SpriteRef& shadow = itemSprite(enemy.elite ? Item::ShadowHuge : shadowFor(enemy));
    // Harder levels draw the boss bigger, so its shadow grows by the same amount. Otherwise
    // the Hell boss looks like it floats over a shadow made for a smaller body.
    if (enemy.type == EnemyType::Boss) {
        spriteQuadScaled(shadow, roundToInt(feet.x), roundToInt(feet.y), bossArtScale(state.run.difficulty), kRowShadow, 0);
        return;
    }
    spriteQuad(shadow, roundToInt(feet.x), roundToInt(feet.y), false, kRowShadow, 0);
}

void drawZombieLongShadow(const GameState& state, const Enemy& enemy, const ScreenPoint& feet, const DayLook& look) {
    const ZombieLook zombie = lookOf(state, enemy, feet);
    const float height = static_cast<float>(zombie.sprite->h);
    shadowQuad(*zombie.sprite, roundToInt(feet.x), roundToInt(feet.y), zombie.mirror, height * look.shadowLength * kShadowLeanX, -height * look.shadowLength * kShadowLeanY);
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

void drawZombieSilhouette(const GameState& state, const Enemy& enemy, const ScreenPoint& feet, uint8_t alpha, float left, float top, float right, float bottom) {
    const ZombieLook zombie = lookOf(state, enemy, feet);
    spriteQuadCropped(*zombie.sprite, roundToInt(feet.x), roundToInt(feet.y), zombie.mirror, zombie.row, zombie.grade, alpha, 0, left, top, right, bottom);
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
    if (enemy.type == EnemyType::Boss) return enemies::bossCharging(enemy);
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

// The spiral's warning: one bent arm for each bullet of a volley, shaped like the pinwheel
// the bullets will make. The outer tip is where the first volley flies, and the inner end
// is turned as far as the last volley will be. The arms grow out as the charge runs out,
// so the moment it fires can be read. Each arm is tested on its own, so the warning
// shows even when the boss stands outside the picture.
bool drawSpiralArms(const View& view, const GameState& state, const Enemy& boss, Vec2 worldFeet, float timeLeft) {
    const balance::BossMoves& moves = balance::bossMovesFor(state.run);
    const int phase = std::min<int>(state.run.bossPhase, 2);
    const int arms = moves.spiralArms[phase];
    const float reach = boss.radius * 1.4f + kSpiralArmLength * (1.0f - 0.4f * timeLeft);
    // The curl is how far the whole spiral will turn, so the arm shows where the pattern goes.
    const float curl = balance::spiralTurnRadians(moves, phase) * static_cast<float>(moves.spiralVolleys - 1);
    const bool flash = !effects::flickerAllowed() || ((state.run.tick >> 2) & 1u) != 0u;
    bool shown = false;
    for (int arm = 0; arm < arms; ++arm) {
        const float start = std::atan2(boss.dir.y, boss.dir.x) + kTau * static_cast<float>(arm) / static_cast<float>(arms);
        Vec2 points[kSpiralArmPoints + 1];
        bool visible = false;
        for (int i = 0; i <= kSpiralArmPoints; ++i) {
            const float along = static_cast<float>(i) / static_cast<float>(kSpiralArmPoints);
            const Vec2 world = worldFeet + fromAngle(start + curl * (1.0f - along)) * (boss.radius * 1.4f + (reach - boss.radius * 1.4f) * along);
            const ScreenPoint p = project(view, world);
            points[i] = Vec2{p.x, p.y};
            if (i > 0) {
                const ScreenPoint from{points[i - 1].x, points[i - 1].y, p.scale};
                if (lineCouldShow(view, from, p)) visible = true;
            }
        }
        if (!visible) continue;
        shown = true;
        for (int i = 0; i < kSpiralArmPoints; ++i) thickLine(points[i], points[i + 1], 7.0f, fade(theme::kHostileDark, 0.75f));
        for (int i = 0; i < kSpiralArmPoints; ++i) thickLine(points[i], points[i + 1], 4.0f, fade(theme::kBossSpiralArm, flash ? 1.0f : 0.65f));
        // A sharp arrowhead, so the turn can't be misread, even with no color at all.
        const Vec2 tip = points[kSpiralArmPoints];
        const Vec2 ahead = normalizedOr(tip - points[kSpiralArmPoints - 1], Vec2{1.0f, 0.0f});
        const Vec2 side = perpendicular(ahead);
        thickLine(tip - ahead * 9.0f + side * 7.0f, tip, 3.0f, theme::kHostileFront);
        thickLine(tip - ahead * 9.0f - side * 7.0f, tip, 3.0f, theme::kHostileFront);
    }
    return shown;
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

    if (enemy.type == EnemyType::Boss && enemies::bossCharging(enemy)) {
        // The ring tightens as the timer runs out, so the moment of attack can be read.
        const float t = clampf(enemy.timer / balance::bossMovesFor(state.run).telegraphSeconds, 0.0f, 1.0f);
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
        if (enemy.state == enemies::kBossSpiralCharge && drawSpiralArms(view, state, enemy, worldFeet, t)) shown = true;
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

// One per player slot. In single-player only the first is used.
SoldierState g_soldierStates[kMaxPlayers];
// Each co-op player's color, as an index into the team rows of the palette.
uint8_t g_teamColor[kMaxPlayers] = {0, 1, 2, 3};
DashPath g_dashPath;          // The dash trail is only drawn for this screen's own soldier.
uint32_t g_dashPathTick = 0;  // The simulation step the path was last added to.
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

WeaponId heldGun(const GameState& state, int slot) {
    if (g_soldierStates[slot].seenAny) return g_soldierStates[slot].gun;
    const Player& player = state.players[slot];
    for (int i = 0; i < player.weaponCount; ++i) {
        if (isHeldGun(player.weapons[i].id)) return player.weapons[i].id;
    }
    return WeaponId::Rifle;
}

bool gunEvolved(const GameState& state, int slot, WeaponId gun) {
    const Player& player = state.players[slot];
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == gun) return player.weapons[i].evolved;
    }
    return false;
}

// True while the gun in his hands is reloading.
bool reloading(const GameState& state, int slot, WeaponId gun) {
    const Player& player = state.players[slot];
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == gun) return player.weapons[i].reloadLeft > 0.0f;
    }
    return false;
}

Heading soldierHeading(const GameState& state, int slot) {
    const Player& player = state.players[slot];
    const SoldierState& s = g_soldierStates[slot];
    if (s.sinceFired < kAimHoldSeconds) return headingFrom(player.aim);
    return headingFrom(s.moving ? player.facing : player.aim);
}

// Index 0 aims straight up the screen, and the last straight down. Left is mirrored.
int gunAngleIndex(Vec2 aim) {
    const float angle = std::atan2(-aim.y * kTiltSquash, std::abs(aim.x));
    const float position = (kPi * 0.5f - angle) / kPi * static_cast<float>(kGunAngles - 1);
    return std::clamp(static_cast<int>(position + 0.5f), 0, kGunAngles - 1);
}

int bodyPose(int slot, uint32_t tick) {
    const SoldierState& s = g_soldierStates[slot];
    if (!s.moving) return kSoldierIdlePose + static_cast<int>((tick / 40u) & 1u);
    const float cycle = s.stride / kStrideLength;
    const float wrapped = cycle - std::floor(cycle);
    return std::min(kSoldierWalkFrames - 1, static_cast<int>(wrapped * static_cast<float>(kSoldierWalkFrames)));
}

// A single-player soldier keeps his own colors. In co-op each one wears his player's color.
uint8_t soldierRow(const GameState& state, int slot) {
    if (!state.isCoop()) return kRowBase;
    return static_cast<uint8_t>(kRowTeam + g_teamColor[slot] % kTeamRows);
}

// Lying on the ground: dead at the end of a run, or a co-op player who is down.
bool lyingDown(const GameState& state, int slot) { return state.run.phase == RunPhase::Dead || state.players[slot].downed; }

}  // namespace

void setSoldierColors(const int colors[kMaxPlayers]) {
    for (int slot = 0; slot < kMaxPlayers; ++slot) g_teamColor[slot] = static_cast<uint8_t>((colors[slot] < 0 ? slot : colors[slot]) % kTeamRows);
}

void noteWeaponFired(int slot, WeaponId weapon) {
    // Mines are dropped and blades circle by themselves, so neither moves the gun in his
    // hands. Treating them as shots would flash and kick a gun that never fired.
    if (!isHeldGun(weapon) || slot < 0 || slot >= kMaxPlayers) return;
    SoldierState& s = g_soldierStates[slot];
    s.sinceFired = 0.0f;
    s.firedThisFrame = true;
    s.gun = weapon;
    s.seenAny = true;
}

void resetSoldierAnimation() {
    for (SoldierState& s : g_soldierStates) s = SoldierState{};
    g_dashPath.clear();
}

SoldierDebug soldierDebug(const GameState& state) { return {static_cast<int>(heldGun(state, state.localSlot)), g_muzzleFrames}; }

void updateSoldierAnimation(int slot, Vec2 feetWorld, float realDt) {
    SoldierState& s = g_soldierStates[slot];
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

void drawTeammateRing(const View& view, Vec2 worldFeet, int slot) {
    // A thinner ring than this screen's own soldier, in his player's color.
    groundRing(view, worldFeet, balance::kPlayerRadius * 1.9f, 3.0f, 3, fade(theme::kCoopSlotColors[g_teamColor[slot] % kTeamRows], 0.8f));
}

// A bubble of shield around a co-op soldier who is choosing a level-up card, or has just picked
// one and is still safe. It is a pixel
// ellipse: a one-pixel edge, a faint body, and two bright pixels on its shoulder. Every
// player sees it, so the team knows who cannot be hurt right now and for how much longer.
bool drawShieldBubble(const GameState& state, int slot, const ScreenPoint& feet) {
    if (!state.isCoop() || state.run.phase != RunPhase::Playing) return false;
    const Player& player = state.players[slot];
    if (player.cardShield <= 0.0f || player.downed || player.hp <= 0.0f) return false;
    const ShieldBubbleLook look = shieldBubbleLook(player.cardShield, state.run.tick, effects::flickerAllowed(), effects::flashScale());
    if (look.outline <= 0.0f) return false;

    const SpriteRef& body = soldierBody(kFacingSide, kSoldierIdlePose);
    const float height = static_cast<float>(body.h);
    const int centerX = roundToInt(feet.x);
    const int radiusX = roundToInt(height * 0.58f);
    const int radiusY = roundToInt(height * 0.68f);
    const int centerY = roundToInt(feet.y) - roundToInt(height * 0.46f);

    // Half the width of the ellipse at a row, in whole pixels. The extra half pixel gives the
    // top and bottom rows some width.
    const auto halfWidth = [&](int row) {
        const float t = static_cast<float>(row) / (static_cast<float>(radiusY) + 0.5f);
        const float inside = 1.0f - t * t;
        return inside > 0.0f ? roundToInt(static_cast<float>(radiusX) * std::sqrt(inside)) : 0;
    };
    const Color edge = fade(theme::kCoopShieldEdge, look.outline);
    const Color inside = fade(theme::kCoopShieldFill, look.fill);
    const auto span = [](int left, int right, int row, Color color) {
        if (right < left) return;
        quad(Vec2{static_cast<float>(left), static_cast<float>(row)}, Vec2{static_cast<float>(left), static_cast<float>(row + 1)},
             Vec2{static_cast<float>(right + 1), static_cast<float>(row + 1)}, Vec2{static_cast<float>(right + 1), static_cast<float>(row)}, color);
    };
    for (int row = -radiusY; row <= radiusY; ++row) {
        const int y = centerY + row;
        const int outer = halfWidth(row);
        // The edge runs from this row's width in to the next row's, so it has no gaps. The top and
        // bottom rows are one full span.
        const int inner = std::min(outer - 1, halfWidth(row < 0 ? row - 1 : row + 1));
        if (std::abs(row) == radiusY || inner <= 0) {
            span(centerX - outer, centerX + outer, y, edge);
        } else {
            span(centerX - outer, centerX - inner, y, edge);
            span(centerX + inner, centerX + outer, y, edge);
            span(centerX - inner + 1, centerX + inner - 1, y, inside);
        }
    }
    // Two bright pixel strokes on the upper left, like light on glass.
    const Color glint = fade(theme::kCoopShieldGlint, look.outline);
    const int glintX = centerX - roundToInt(static_cast<float>(radiusX) * 0.55f);
    const int glintY = centerY - roundToInt(static_cast<float>(radiusY) * 0.55f);
    span(glintX, glintX + 2, glintY, glint);
    span(glintX - 1, glintX - 1, glintY + 1, glint);
    span(glintX - 1, glintX - 1, glintY + 2, glint);
    return true;
}

void drawSoldierShadow(const GameState& state, int slot, const ScreenPoint& feet, const DayLook& look) {
    const int x = roundToInt(feet.x);
    const int y = roundToInt(feet.y);
    spriteQuad(itemSprite(Item::ShadowMedium), x, y, false, kRowShadow, 0);
    // A co-op player lying down casts no standing shadow.
    if (look.shadowLength > 0.02f && !state.players[slot].downed) {
        const Heading heading = soldierHeading(state, slot);
        const SpriteRef& body = soldierBody(heading.facing, bodyPose(slot, state.run.tick));
        const float height = static_cast<float>(body.h);
        shadowQuad(body, x, y, heading.mirror, height * look.shadowLength * kShadowLeanX, -height * look.shadowLength * kShadowLeanY);
    }
}

void drawSoldier(const GameState& state, int slot, const ScreenPoint& feet, uint8_t strength) {
    const Player& player = state.players[slot];
    const uint32_t tick = state.run.tick;
    const int x = roundToInt(feet.x);
    const int y = roundToInt(feet.y);
    const Heading heading = soldierHeading(state, slot);
    const uint8_t row = soldierRow(state, slot);
    if (lyingDown(state, slot)) {
        spriteQuad(soldierBody(kFacingSide, kSoldierDeadPose), x, y, heading.mirror, row, 150, strength);
        return;
    }
    const bool playing = state.run.phase == RunPhase::Playing;
    const bool safe = playing && player.invulnerable > 0.0f;
    // A blink while hits can't land, so that safe moment can be seen. Where flicker is
    // off, he goes a little see-through for that moment instead.
    if (safe && effects::flickerAllowed() && ((tick >> 2) & 1u) != 0u) return;
    if (safe && !effects::flickerAllowed()) strength = static_cast<uint8_t>(strength / 2 + strength / 4);

    const uint8_t flash = playing && player.hitFlash > 0.0f ? effects::soldierFlash(player.hitFlash) : 0;
    const uint8_t grade = 150;
    const SpriteRef& body = soldierBody(heading.facing, bodyPose(slot, tick));

    const WeaponId gun = heldGun(state, slot);
    const uint8_t gunRow = gunEvolved(state, slot, gun) ? kRowEvolved : kRowBase;
    const bool isReloading = reloading(state, slot, gun);

    const Vec2 aim = player.aim;
    const float aimX = aim.x;
    const float aimY = aim.y * kTiltSquash;
    const float aimLength = std::max(0.001f, std::sqrt(aimX * aimX + aimY * aimY));
    const float t = clampf(1.0f - g_soldierStates[slot].sinceFired / kRecoilSeconds, 0.0f, 1.0f);
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

// The afterimage of a dash: a few pale copies of his body strung out behind him, on the way he
// really went. A dash into a car goes nowhere, so it leaves no copies.
// Reduced motion or no flicker keeps only two, steady and without the fade-out blink.
void drawDashTrail(const View& view, const GameState& state, Vec2 playerWorld) {
    const int slot = state.localSlot;
    const Player& player = state.players[slot];
    if (player.dashLeft <= 0.0f) {
        g_dashPath.clear();
        return;
    }
    if (state.run.phase != RunPhase::Playing) return;
    if (g_dashPath.empty()) {
        g_dashPath.add(player.prevPos);
        g_dashPath.add(player.pos);
        g_dashPathTick = state.run.tick;
    } else if (state.run.tick != g_dashPathTick) {
        g_dashPath.add(player.pos);
        g_dashPathTick = state.run.tick;
    }
    const bool simple = !effects::flickerAllowed();
    const int ghosts = simple ? kTrailGhostsSimple : kTrailGhosts;
    const Heading heading = soldierHeading(state, slot);
    const SpriteRef& body = soldierBody(heading.facing, bodyPose(slot, state.run.tick));
    // Far ghosts first, so nearer ones draw over them.
    for (int i = ghosts; i >= 1; --i) {
        Vec2 ghost;
        if (!g_dashPath.pointBack(playerWorld, static_cast<float>(i) * kTrailSpacing, &ghost)) continue;
        const ScreenPoint at = project(view, ghost);
        const float strength = 1.0f - static_cast<float>(i - 1) / static_cast<float>(ghosts);
        spriteQuad(body, roundToInt(at.x), roundToInt(at.y), heading.mirror, soldierRow(state, slot), 150, static_cast<uint8_t>(kTrailAlpha * strength), kTrailWash);
    }
}

void drawMuzzleFlash(const GameState& state, int slot, const ScreenPoint& feet) {
    if (g_soldierStates[slot].sinceFired > kFlashSeconds || lyingDown(state, slot)) return;
    // With rapid fire this is a strobe, however small, so it is the first thing to go.
    const float light = effects::flashScale();
    if (effects::reducedMotion() || light <= 0.0f) return;
    const WeaponId gun = heldGun(state, slot);
    if (gun == WeaponId::Flamethrower || gun == WeaponId::Tesla) return;
    if (reloading(state, slot, gun)) return;
    const Vec2 aim = state.players[slot].aim;
    const float along = 1.0f - 0.28f * std::abs(aim.y);
    const float length = gunLength(gun) * along + 1.0f;
    const float aimY = aim.y * kTiltSquash;
    const float aimLength = std::max(0.001f, std::sqrt(aim.x * aim.x + aimY * aimY));
    const int x = roundToInt(feet.x + aim.x / aimLength * length);
    const int y = roundToInt(feet.y - kShoulderHeight + aimY / aimLength * length);
    const bool big = gun == WeaponId::Shotgun || gun == WeaponId::Sniper || gun == WeaponId::Rockets || gun == WeaponId::GrenadeLauncher;
    const bool evolved = gunEvolved(state, slot, gun);
    if (slot == state.localSlot) g_muzzleFrames++;
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
    // The boss always dies in its last phase, so it falls in that phase's colors.
    corpse.row = type == EnemyType::Boss ? static_cast<uint8_t>(kRowBoss + 2) : static_cast<uint8_t>(kRowOutfits + (hash >> 4) % kOutfitCount);
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
        setSpriteDepth(depthOfRow(view, feet.y));
        const int frame = age >= kFallTicks ? kCorpseFrames - 1 : static_cast<int>(age * kCorpseFrames / kFallTicks);
        const Figure figure = corpse.type == EnemyType::Boss ? Figure::Boss3 : static_cast<Figure>(static_cast<int>(corpse.type));
        const int step = corpse.type == EnemyType::Boss ? stepFor(feet.scale, kStepBossFirst, kStepBossLast) : stepFor(feet.scale, 0, kStepNormalLast);
        const SpriteRef& sprite = corpseSprite(figure, step, frame);
        float alpha = 1.0f;
        if (age > kFallTicks + kLieTicks) alpha = 1.0f - static_cast<float>(age - kFallTicks - kLieTicks) / static_cast<float>(kFadeTicks);
        spriteQuad(sprite, roundToInt(feet.x), roundToInt(feet.y), corpse.mirror, corpse.row, 255, static_cast<uint8_t>(alpha * 255.0f));
    }
}

}  // namespace ls::worldview
