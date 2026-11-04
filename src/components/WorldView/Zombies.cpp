#include "components/WorldView/Characters.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/WorldView/Figures.h"

#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>

namespace ls::worldview {

namespace {

constexpr int kTypeCount = static_cast<int>(EnemyType::Count);

// One gait cycle takes this many seconds, per enemy type. A 32 bit counter wraps
// once per cycle, so whole-number maths gives smooth, looping animation.
constexpr uint32_t gaitRate(float cycleSeconds) {
    return static_cast<uint32_t>(4294967296.0 / static_cast<double>(cycleSeconds * 60.0f));
}
constexpr uint32_t kGaitRate[kTypeCount] = {
    /* Walker  */ gaitRate(1.25f),
    /* Runner  */ gaitRate(0.36f),
    /* Brute   */ gaitRate(1.50f),
    /* Spitter */ gaitRate(1.05f),
    /* Bloater */ gaitRate(0.85f),
    /* Crawler */ gaitRate(0.42f),
    /* Charger */ gaitRate(0.80f),
    /* Boss    */ gaitRate(1.70f),
};
constexpr uint32_t kDashGaitRate = gaitRate(0.22f);

// Sizes in figure units, so multiples of the enemy's radius.
constexpr float kShadowWidth[kTypeCount] = {1.15f, 1.35f, 1.65f, 0.95f, 1.30f, 1.95f, 1.30f, 2.50f};
constexpr float kHeight[kTypeCount] = {3.1f, 2.4f, 3.2f, 4.4f, 3.1f, 1.7f, 3.2f, 4.5f};
constexpr float kHalfWidth[kTypeCount] = {2.3f, 2.5f, 2.2f, 1.6f, 1.9f, 2.3f, 2.5f, 2.8f};

constexpr float kShadowSquash = 0.42f;   // Shadows are flatter than the tilt alone, as the light is overhead.
constexpr float kBarWidth = 1.6f;        // Elite HP bar width, per unit of radius.
constexpr float kBarHeight = 4.0f;
constexpr float kRecoilSeconds = 0.28f;  // How long a Spitter rocks back after spitting.
constexpr float kRecoilLean = 0.34f;
constexpr int kFlashPeriodTicks = 8;     // Charger telegraph flash, in simulation steps.

constexpr Palette makePalette(Color outline, Color accent, Color hot) {
    return Palette{{outline, theme::kEnemyFill, accent, hot}};
}

constexpr Palette kPalettes[kTypeCount] = {
    makePalette(theme::kEnemyWalker, theme::kEnemyEye, theme::kEnemyEye),
    makePalette(theme::kEnemyRunner, theme::kEnemyEye, theme::kEnemyEye),
    makePalette(theme::kEnemyBrute, theme::kEnemyEye, theme::kEnemyEye),
    makePalette(theme::kEnemySpitter, theme::kEnemyGland, theme::kEnemyEye),
    makePalette(theme::kEnemyBloater, theme::kEnemyBloater, theme::kEnemyEye),
    makePalette(theme::kEnemyCrawler, theme::kEnemyEye, theme::kEnemyEye),
    makePalette(theme::kEnemyCharger, theme::kEnemyEye, theme::kEnemyHorn),
    makePalette(theme::kBossPhase1, theme::kBossCore, theme::kBossCore),
};

int bossPhaseOf(const GameState& state) { return std::min<int>(state.run.bossPhase, 2); }

FigureId figureFor(const GameState& state, const Enemy& enemy) {
    switch (enemy.type) {
        case EnemyType::Walker: return FigureId::Walker;
        case EnemyType::Runner: return FigureId::Runner;
        case EnemyType::Brute: return FigureId::Brute;
        case EnemyType::Spitter: return FigureId::Spitter;
        case EnemyType::Bloater: return FigureId::Bloater;
        case EnemyType::Crawler: return FigureId::Crawler;
        case EnemyType::Charger:
            if (enemy.state == enemies::kChargerTelegraph) return FigureId::ChargerBrace;
            if (enemy.state == enemies::kChargerDash) return FigureId::ChargerDash;
            if (enemy.state == enemies::kChargerRecover) return FigureId::ChargerRecover;
            return FigureId::Charger;
        case EnemyType::Boss: return static_cast<FigureId>(static_cast<int>(FigureId::BossPhase1) + bossPhaseOf(state));
        case EnemyType::Count: break;
    }
    return FigureId::Walker;
}

bool chargerFlashOn(const GameState& state, const Enemy& enemy) {
    return ((state.run.tick + enemy.id) / kFlashPeriodTicks) % 2 == 0;
}

// A locked dash direction wins. Otherwise a zombie faces the player it is chasing.
float facingSign(const GameState& state, const Enemy& enemy, Vec2 feet) {
    const bool locked = enemy.type == EnemyType::Charger && enemy.state != 0 && enemy.dir.x != 0.0f;
    const float dx = locked ? enemy.dir.x : state.player.pos.x - feet.x;
    return dx < 0.0f ? -1.0f : 1.0f;
}

}  // namespace

// The boss's other non-zero states only mean which attack comes next while it
// walks, so it must not look charged up during those.
bool isBossCharging(const Enemy& enemy) {
    return enemy.type == EnemyType::Boss && (enemy.state == enemies::kBossRingCharge || enemy.state == enemies::kBossDashCharge);
}

bool mustStayDetailed(const Enemy& enemy) {
    return enemy.elite || enemy.type == EnemyType::Boss || (enemy.type == EnemyType::Charger && enemy.state != 0);
}

float figureHeight(const Enemy& enemy) { return kHeight[static_cast<int>(enemy.type)] * enemy.radius; }
float figureHalfWidth(const Enemy& enemy) { return kHalfWidth[static_cast<int>(enemy.type)] * enemy.radius; }

void drawZombieShadow(const Enemy& enemy, Vec2 feet, bool detailed) {
    const float width = kShadowWidth[static_cast<int>(enemy.type)] * enemy.radius;
    oval(feet, width, width * kShadowSquash, detailed ? 6 : 12, theme::kShadow);
    if (enemy.elite) groundRing(feet, width * 1.15f, 2.5f, 2, theme::kEliteRing);
}

void drawZombie(const GameState& state, const Enemy& enemy, Vec2 feet, bool detailed) {
    const int type = static_cast<int>(enemy.type);
    const bool moving = lengthSq(enemy.pos - enemy.prevPos) > 1e-4f;
    const FigureId figure = figureFor(state, enemy);

    // The id scatters each zombie's start point, so a crowd doesn't march in step.
    int frame = 0;
    if (moving || enemy.type == EnemyType::Boss || figure == FigureId::ChargerBrace) {
        const uint32_t rate = figure == FigureId::ChargerDash ? kDashGaitRate : kGaitRate[type];
        frame = static_cast<int>((state.run.tick * rate + enemy.id * 0x9E3779B1u) >> 28);
    }

    Palette palette = kPalettes[type];
    if (enemy.type == EnemyType::Boss) {
        const int phase = bossPhaseOf(state);
        palette.colors[kLayerOutline] = phase == 0 ? theme::kBossPhase1 : (phase == 1 ? theme::kBossPhase2 : theme::kBossPhase3);
        palette.colors[kLayerFill] = phase == 2 ? theme::kBossFillPhase3 : theme::kBossFill;
    }
    if (enemy.elite) palette.colors[kLayerOutline] = theme::kEliteOutline;

    const float sign = facingSign(state, enemy, feet);
    // Small size differences stop a crowd looking copy-pasted.
    const float size = enemy.radius * (0.95f + static_cast<float>((enemy.id * 2654435761u) >> 29) * 0.015f);
    Placement place{feet.x, feet.y, sign * size, size};

    if (figure == FigureId::ChargerBrace) {
        if (chargerFlashOn(state, enemy)) {
            palette.colors[kLayerOutline] = theme::kChargerFlash;
            palette.colors[kLayerFill] = theme::kChargerFlashFill;
        }
        place.x += static_cast<float>(static_cast<int>(state.run.tick % 4u) - 1) * 0.7f;
    } else if (enemy.type == EnemyType::Spitter) {
        // Assumes the fire timer counts down from the fire interval, so a timer
        // near the full interval means it has only just spat.
        const float sinceSpit = balance::kSpitterFireInterval - enemy.timer;
        if (sinceSpit >= 0.0f && sinceSpit < kRecoilSeconds) {
            const float recoil = 1.0f - sinceSpit / kRecoilSeconds;
            place.shear = -sign * size * kRecoilLean * recoil;
            place.scaleY = size * (1.0f - 0.08f * recoil);
        }
    } else if (isBossCharging(enemy)) {
        // Charging up: it shakes, swells a little, and its outline goes white hot.
        const bool hotStep = (state.run.tick / 5u) % 2u == 0u;
        if (hotStep) palette.colors[kLayerOutline] = theme::kBossCharge;
        palette.colors[kLayerHot] = theme::kBossCharge;
        place.x += static_cast<float>(static_cast<int>(state.run.tick % 3u) - 1) * 1.5f;
        place.scaleX *= 1.03f;
        place.scaleY *= 1.03f;
    }

    if (enemy.flash > 0.0f) {
        palette.colors[kLayerOutline] = theme::kEnemyHitFlash;
        palette.colors[kLayerFill] = theme::kEnemyHitFill;
    }

    const FigureFrame pose = figureFrame(figure, detailed, frame);
    emitQuads(pose.quads, pose.count, place, palette);
}

void drawZombieOverlay(const GameState& state, const Enemy& enemy, Vec2 feet) {
    if (enemy.type == EnemyType::Charger && enemy.state == enemies::kChargerTelegraph) {
        // A flat warning strip along the coming dash, the same length as the dash.
        const float reach = balance::kChargerDashSpeed * balance::kChargerDashSeconds;
        const Vec2 worldDir = normalizedOr(enemy.dir, Vec2{1.0f, 0.0f});
        const Vec2 worldSide = perpendicular(worldDir) * enemy.radius;
        const Vec2 end = feet + project(worldDir * reach);
        const Vec2 side = project(worldSide);
        const bool on = chargerFlashOn(state, enemy);
        const float strength = on ? 1.0f : 0.55f;
        quad(feet + side, feet - side, end - side, end + side, fade(theme::kChargerLine, 0.22f * strength));
        thickLine(feet + side, end + side, 2.0f, fade(theme::kChargerLine, strength));
        thickLine(feet - side, end - side, 2.0f, fade(theme::kChargerLine, strength));
        // Arrow marks along the strip point the way, so it reads without color.
        const Vec2 step = project(worldDir * (reach / 4.0f));
        const Vec2 point = project(worldDir * (enemy.radius * 1.5f));
        for (int i = 1; i <= 4; ++i) {
            const Vec2 center = feet + step * static_cast<float>(i) - point;
            thickLine(center + side * 0.8f, center + point, 3.0f, fade(theme::kChargerFlash, strength));
            thickLine(center - side * 0.8f, center + point, 3.0f, fade(theme::kChargerFlash, strength));
        }
    }

    if (enemy.elite) {
        const float width = enemy.radius * kBarWidth;
        const float top = feet.y - figureHeight(enemy) - 6.0f;
        const float fraction = enemy.maxHp > 0.0f ? clampf(enemy.hp / enemy.maxHp, 0.0f, 1.0f) : 0.0f;
        const float left = feet.x - width * 0.5f;
        quad(Vec2{left - 1.5f, top - 1.5f}, Vec2{left + width + 1.5f, top - 1.5f}, Vec2{left + width + 1.5f, top + kBarHeight + 1.5f},
             Vec2{left - 1.5f, top + kBarHeight + 1.5f}, theme::kEliteBarBack);
        quad(Vec2{left, top}, Vec2{left + width * fraction, top}, Vec2{left + width * fraction, top + kBarHeight}, Vec2{left, top + kBarHeight},
             theme::kEliteBarFill);
    }

    if (isBossCharging(enemy)) {
        // Rings that close in on the boss show the charge building up.
        const float base = enemy.radius * 2.6f;
        for (int i = 0; i < 3; ++i) {
            const float t = static_cast<float>((state.run.tick + static_cast<uint32_t>(i) * 13u) % 40u) / 40.0f;
            groundRing(feet, base * (1.5f - t), 4.0f, 2, fade(theme::kBossChargeRing, t));
        }
    }
}

}  // namespace ls::worldview
