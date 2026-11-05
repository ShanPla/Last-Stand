#include "components/Effects/Effects.h"

#include "components/Effects/Budget/Budget.h"
#include "components/Effects/Bursts/Bursts.h"
#include "components/Effects/DamageNumbers/DamageNumbers.h"
#include "components/Effects/FlashLimiter/FlashLimiter.h"
#include "components/Effects/FrameWatch/FrameWatch.h"
#include "components/Effects/Glow/Glow.h"
#include "components/Effects/HitFlash/HitFlash.h"
#include "components/Effects/HitPause/HitPause.h"
#include "components/Effects/Particles/Particles.h"
#include "components/Effects/ScreenShake/ScreenShake.h"
#include "core/Rng.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ls::effects {

namespace {

// --- Tuning -------------------------------------------------------------------

// Reduced motion keeps the world's cause and effect readable, with far less going on.
constexpr float kReducedParticles = 0.35f;
constexpr float kWeakDeviceParticles = 0.5f;

// Trauma: see ScreenShake. A shake grows with the square of this.
constexpr float kShakeExplosion = 0.32f;
constexpr float kShakeBomb = 0.72f;
constexpr float kShakePlayerHit = 0.42f;
constexpr float kShakePlayerDied = 0.7f;
constexpr float kShakeElite = 0.22f;
constexpr float kShakeBossArrives = 0.85f;
constexpr float kShakeBossSeen = 0.6f;
constexpr float kShakeBossPhase = 0.7f;
constexpr float kShakeBossDied = 1.0f;
constexpr float kShakeEvolution = 0.6f;
constexpr float kShakeCarCrush = 0.42f;  // Small: about one art pixel, and it never adds up.
constexpr float kShakeReach = 720.0f;  // World units. A blast this far away shakes nothing.

constexpr float kPauseBossPhase = 0.07f;
constexpr float kPauseBossDied = 0.12f;
constexpr float kPauseEvolution = 0.08f;
constexpr float kPauseNearDeath = 0.05f;
constexpr float kNearDeathBelow = 0.25f;  // Share of health that counts as nearly dead.
constexpr float kNearDeathRearm = 0.4f;   // It must heal past this to pause again.

constexpr float kFlashBomb = 0.30f;
constexpr float kFlashBossArrives = 0.34f;
constexpr float kFlashBossPhase = 0.26f;
constexpr float kFlashBossDied = 0.42f;
constexpr float kFlashEvolution = 0.30f;
constexpr float kFlashFade = 0.22f;
constexpr float kFlashFadeReduced = 0.5f;   // A slow, simple fade.
constexpr float kFlashReducedMost = 0.10f;

constexpr float kBossFlashMost = 0.22f;   // It is under constant fire, so it must never glow solid.
constexpr float kEliteFlashMost = 0.5f;
constexpr float kSoldierFlashStrength = 0.75f;
constexpr float kSoldierFlashSeconds = 0.15f;

constexpr float kTargetHeight = 16.0f;    // Art pixels: roughly where a shot lands on a zombie.
constexpr float kEliteHeight = 26.0f;
constexpr float kBossHeight = 70.0f;
constexpr float kBossNumberHeight = 150.0f;  // Above its head, not on its face.
constexpr float kMuzzleHeight = 18.0f;
constexpr float kMuzzleReach = 24.0f;     // World units ahead of the soldier.
constexpr int kHotGlowsMost = 40;

// Budgets: tokens a second, and the most that can build up. Each is one kind of event.
Budget g_hitBudget{110.0f, 18.0f};
Budget g_killBudget{70.0f, 14.0f};
Budget g_stainBudget{14.0f, 6.0f};
Budget g_blastBudget{10.0f, 5.0f};
Budget g_muzzleBudget{28.0f, 6.0f};
Budget g_casingBudget{10.0f, 3.0f};
Budget g_gemBudget{24.0f, 6.0f};
Budget g_playerBloodBudget{8.0f, 2.0f};
Budget g_crushBudget{4.0f, 3.0f};

// --- State --------------------------------------------------------------------

Settings g_settings;
int g_quality = 1;
Rng g_random;             // Effects' own numbers. Never the simulation's generator.
ScreenShake g_shake;
HitPause g_hitPause;
FlashLimiter g_limiter;
DamageNumberPool g_numbers;

float g_clock = 0.0f;     // Real seconds, for the flash limiter.
float g_flashLeft = 0.0f;
float g_flashFullSeconds = kFlashFade;
float g_flashStrength = 0.0f;
Color g_flashColor{};
int g_flashingCount = 0;
int g_flashStride = 1;
float g_flashStrengthNow = kFlashLone;
bool g_nearDeathArmed = true;
bool g_bossEntrancePending = false;
bool g_bossOnScreen = false;
float g_bossDistance = -1.0f;
View g_lastView;          // The last camera drawn, to skip effects nobody can see.
bool g_viewKnown = false;
const GameState* g_lastState = nullptr;  // For the telegraph test while numbers draw.

// What the browser tests read, to prove each setting really changes behavior.
struct Counters {
    int shakeEvents = 0;
    float shakePeak = 0.0f;    // Biggest whole-pixel offset seen, on either axis.
    int hitPauses = 0;
    float hitPauseSeconds = 0.0f;
    int flashesShown = 0;
    int flashesRefused = 0;
    float flashPeak = 0.0f;
    float explosionLightPeak = 0.0f;
    float hitFlashPeak = 0.0f;
    int zombiesFlashedPeak = 0;
    int zombiesFlashedNow = 0;
    int glowsDrawn = 0;
    int numberEvents = 0;
    int numbersDrawnPeak = 0;
};
Counters g_counters;

// --- Helpers ------------------------------------------------------------------

bool nearView(Vec2 world) {
    if (!g_viewKnown) return true;
    return onTarget(g_lastView, project(g_lastView, world), 30.0f, 90.0f, 30.0f);
}

float shakeScale() { return g_settings.reducedMotion ? 0.0f : g_settings.screenShake; }

void addShake(float trauma, bool quiet) {
    if (quiet || shakeScale() <= 0.0f) return;
    g_shake.add(trauma);
    g_counters.shakeEvents++;
}

// For small events that come in runs. See ScreenShake::raiseTo.
void raiseShake(float trauma, bool quiet) {
    if (quiet || shakeScale() <= 0.0f) return;
    g_shake.raiseTo(trauma);
    g_counters.shakeEvents++;
}

void addHitPause(float seconds, bool quiet) {
    if (quiet || g_settings.reducedMotion) return;
    if (g_hitPause.trigger(seconds)) {
        g_counters.hitPauses++;
        g_counters.hitPauseSeconds += seconds;
    }
}

// Every full-screen flash goes through here, and so through the limiter.
void requestScreenFlash(float strength, Color color, bool quiet) {
    if (quiet) return;
    const float wanted = g_settings.reducedMotion ? std::min(strength, kFlashReducedMost) : strength;
    const float shown = limitFlash(g_limiter, g_clock, wanted, g_settings.flashIntensity);
    if (shown <= 0.0f) {
        g_counters.flashesRefused++;
        return;
    }
    g_flashStrength = shown;
    g_flashFullSeconds = g_settings.reducedMotion ? kFlashFadeReduced : kFlashFade;
    g_flashLeft = g_flashFullSeconds;
    g_flashColor = color;
    g_counters.flashesShown++;
    g_counters.flashPeak = std::max(g_counters.flashPeak, shown);
}

Vec2 awayFromPlayer(const GameState& state, Vec2 at) {
    return normalizedOr(at - state.player.pos, Vec2{1.0f, 0.0f});
}

// --- One handler for each thing that can happen --------------------------------

void onEnemyHit(const GameState& state, const SimEvent& event, const Enemy* boss) {
    const bool isBoss = boss && distanceSq(boss->pos, event.pos) < boss->radius * boss->radius;
    const float height = isBoss ? kBossHeight : (event.flag ? kEliteHeight : kTargetHeight);
    if (g_settings.damageNumbers) {
        g_numbers.add(event.pos, isBoss ? kBossNumberHeight : height + 10.0f, event.value);
        g_counters.numberEvents++;
    }
    if (!g_hitBudget.take()) return;
    const Vec2 away = awayFromPlayer(state, event.pos);
    const auto weapon = static_cast<WeaponId>(event.detail);
    const Vec2 at = isBoss ? event.pos + Vec2{g_random.range(-40.0f, 40.0f), g_random.range(-10.0f, 10.0f)} : event.pos;
    const float where = isBoss ? g_random.range(30.0f, 110.0f) : height;
    switch (weapon) {
        case WeaponId::Flamethrower:
            emberRise(at, where, 2, 40.0f);
            break;
        case WeaponId::Tesla:
            impactSparks(at, where, away, 2, theme::kFxTeslaSpark);
            break;
        case WeaponId::Rifle:
        case WeaponId::Smg:
        case WeaponId::Shotgun:
        case WeaponId::Sniper:
            bloodSpray(at, where, away, 3 + g_random.below(2), theme::kFxBlood);
            impactSparks(at, where, away * -1.0f, 1 + g_random.below(2), theme::kFxSpark);
            break;
        case WeaponId::Blades:
            bloodSpray(at, where, away, 3, theme::kFxBlood);
            break;
        default:
            bloodSpray(at, where, away, 1 + g_random.below(2), theme::kFxBlood);
            break;
    }
}

void onEnemyKilled(const GameState& state, const SimEvent& event, bool elite, bool quiet) {
    if (elite) addShake(kShakeElite, quiet);
    if (!g_killBudget.take() && !elite) return;
    const auto type = static_cast<EnemyType>(event.detail);
    const bool bile = type == EnemyType::Bloater;
    const Color* drops = bile ? theme::kFxBile : theme::kFxBlood;
    const int size = elite ? 2 : (type == EnemyType::Brute || bile ? 1 : 0);
    const Vec2 away = awayFromPlayer(state, event.pos);
    bloodSpray(event.pos, kTargetHeight, away, 5 + size * 4, drops);
    Burst around;
    around.at = event.pos;
    around.height = kTargetHeight;
    around.count = 2 + size * 3;
    around.speedMin = 20.0f;
    around.speedMax = 80.0f;
    around.liftMin = 30.0f;
    around.liftMax = 90.0f;
    around.gravity = 300.0f;
    around.colors = drops;
    around.colorCount = 3;
    around.flags = kFlagTinted;
    emitBurst(around, g_random);
    chunkBurst(event.pos, kTargetHeight, 1 + size * 2, bile ? theme::kFxBile : theme::kFxGore, 3, 110.0f, 3);
    if (g_quality > 0 && g_stainBudget.take()) {
        addStain(event.pos, 6.0f + static_cast<float>(size) * 4.0f + g_random.range(0.0f, 3.0f), bile ? StainKind::Bile : StainKind::Blood, g_random);
    }
}

void onExplosion(const GameState& state, const SimEvent& event, bool quiet) {
    const float away = distance(event.pos, state.player.pos);
    if (away < kShakeReach) addShake(kShakeExplosion * (1.0f - away / kShakeReach), quiet);
    if (!g_blastBudget.take()) return;
    const float size = clampf(event.value / 130.0f, 0.5f, 2.0f);
    Burst fire;
    fire.at = event.pos;
    fire.height = 8.0f;
    fire.count = static_cast<int>(14.0f * size);
    fire.speedMin = 80.0f;
    fire.speedMax = 320.0f * size;
    fire.liftMin = 10.0f;
    fire.liftMax = 110.0f;
    fire.lifeMin = 0.12f;
    fire.lifeMax = 0.34f;
    fire.gravity = 180.0f;
    fire.sizeMin = 1;
    fire.sizeMax = 2;
    fire.colors = theme::kFxSpark;
    fire.colorCount = 3;
    fire.flags = kFlagHot | kFlagDrag;
    emitBurst(fire, g_random);
    chunkBurst(event.pos, 6.0f, static_cast<int>(9.0f * size), theme::kFxDebris, 3, 190.0f * size, 3);
    emberRise(event.pos, 10.0f, static_cast<int>(6.0f * size), 90.0f * size);
    if (g_quality > 0) addStain(event.pos, event.value * 0.14f, StainKind::Scorch, g_random);
}

void onWeaponFired(const GameState& state, const SimEvent& event) {
    const auto weapon = static_cast<WeaponId>(event.detail);
    if (weapon == WeaponId::Flamethrower || weapon == WeaponId::Tesla || weapon == WeaponId::Blades || weapon == WeaponId::Landmines) return;
    if (!g_muzzleBudget.take()) return;
    const Vec2 aim = state.player.aim;
    const Vec2 muzzle = state.player.pos + aim * kMuzzleReach;
    const bool heavy = weapon == WeaponId::Shotgun || weapon == WeaponId::Sniper || weapon == WeaponId::Rockets || weapon == WeaponId::GrenadeLauncher;
    impactSparks(muzzle, kMuzzleHeight, aim, heavy ? 3 : 1, theme::kFxSpark);
    if (heavy) {
        Burst smoke;
        smoke.at = muzzle;
        smoke.height = kMuzzleHeight;
        smoke.count = 2;
        smoke.speedMin = 10.0f;
        smoke.speedMax = 40.0f;
        smoke.liftMin = 10.0f;
        smoke.liftMax = 26.0f;
        smoke.lifeMin = 0.25f;
        smoke.lifeMax = 0.45f;
        smoke.gravity = 0.0f;
        smoke.sizeMin = 2;
        smoke.sizeMax = 3;
        smoke.colors = theme::kFxSmoke;
        smoke.colorCount = 2;
        smoke.aim = aim;
        smoke.aimSpread = 0.5f;
        smoke.flags = kFlagDrag | kFlagTinted;
        emitBurst(smoke, g_random);
    }
    if (weapon != WeaponId::Rockets && weapon != WeaponId::GrenadeLauncher && g_casingBudget.take()) {
        // A brass casing thrown out sideways. It is one pixel, but it sells the gun.
        Burst casing;
        casing.at = state.player.pos;
        casing.height = kMuzzleHeight;
        casing.count = 1;
        casing.speedMin = 50.0f;
        casing.speedMax = 90.0f;
        casing.liftMin = 40.0f;
        casing.liftMax = 80.0f;
        casing.lifeMin = 0.4f;
        casing.lifeMax = 0.6f;
        casing.gravity = 340.0f;
        casing.sizeMin = 1;
        casing.sizeMax = 1;
        casing.colors = &theme::kFxBrass;
        casing.aim = Vec2{-aim.y, aim.x};
        casing.aimSpread = 0.5f;
        casing.flags = kFlagBounce | kFlagTinted;
        emitBurst(casing, g_random);
    }
}

void onPlayerHit(const GameState& state, const SimEvent& event, bool quiet) {
    addShake(kShakePlayerHit, quiet);
    if (g_playerBloodBudget.take()) bloodSpray(event.pos, kTargetHeight, Vec2{}, 4, theme::kFxPlayerBlood);
    const float health = state.player.maxHp > 0.0f ? state.player.hp / state.player.maxHp : 1.0f;
    if (health >= kNearDeathRearm) g_nearDeathArmed = true;
    if (g_nearDeathArmed && health < kNearDeathBelow && health > 0.0f) {
        g_nearDeathArmed = false;
        addHitPause(kPauseNearDeath, quiet);
    }
}

// The boss has just flattened a car. A thick puff along the whole car, because the repaint
// takes a few frames and the dust is what hides the moment the picture changes.
void onCarCrushed(const GameState& state, const SimEvent& event, bool quiet) {
    const float away = distance(event.pos, state.player.pos);
    if (away < kShakeReach && nearView(event.pos)) raiseShake(kShakeCarCrush * (1.0f - 0.5f * away / kShakeReach), quiet);
    if (!nearView(event.pos) || !g_crushBudget.take()) return;
    const Vec2 half = maps::makeObstacle(static_cast<maps::CarSprite>(event.detail), 0, 0).half;
    const bool big = std::max(half.x, half.y) > 60.0f;
    for (const float along : {-0.7f, -0.25f, 0.25f, 0.7f}) {
        Burst dust;
        dust.at = event.pos + Vec2{half.x * along, half.y * along};
        dust.height = 6.0f;
        dust.count = big ? 16 : 12;
        dust.speedMin = 6.0f;
        dust.speedMax = 70.0f;
        dust.liftMin = 10.0f;
        dust.liftMax = 55.0f;
        dust.lifeMin = 0.35f;
        dust.lifeMax = 0.8f;
        dust.gravity = 50.0f;
        dust.sizeMin = 3;
        dust.sizeMax = 5;
        dust.colors = theme::kFxCrushDust;
        dust.colorCount = 3;
        dust.flags = kFlagTinted | kFlagDrag | kFlagShrink;
        emitBurst(dust, g_random);
    }
    groundRing(event.pos, big ? 18 : 12, big ? 230.0f : 170.0f, theme::kFxCrushDust, 3, 0.4f, 2, kFlagTinted);
    chunkBurst(event.pos, 10.0f, big ? 16 : 10, theme::kFxCrushMetal, 3, 190.0f, 3);
    impactSparks(event.pos, 8.0f, Vec2{}, big ? 8 : 5, theme::kFxSpark);
}

void onBomb(const SimEvent& event, bool quiet) {
    addShake(kShakeBomb, quiet);
    requestScreenFlash(kFlashBomb, theme::kFxScreenFlashWarm, quiet);
    groundRing(event.pos, 70, event.value * 1.6f, theme::kFxSpark, 3, 0.45f, 2, kFlagHot);
    groundRing(event.pos, 40, event.value * 1.1f, theme::kFxDust, 2, 0.6f, 3, kFlagTinted);
}

void onBossPhase(const SimEvent& event, bool quiet) {
    const int phase = std::clamp(static_cast<int>(event.detail), 0, 2);
    addShake(kShakeBossPhase, quiet);
    addHitPause(kPauseBossPhase, quiet);
    requestScreenFlash(kFlashBossPhase, theme::kFxScreenFlashHostile, quiet);
    hotBurst(event.pos, 60.0f, 46, 300.0f, 0.7f, theme::kFxBossBurst[phase]);
    chunkBurst(event.pos, 40.0f, 16, theme::kFxGore, 3, 220.0f, 3);
    groundRing(event.pos, 30, 420.0f, theme::kFxDust, 2, 0.5f, 3, kFlagTinted);
}

void onBossKilled(const SimEvent& event, bool quiet) {
    addShake(kShakeBossDied, quiet);
    addHitPause(kPauseBossDied, quiet);
    requestScreenFlash(kFlashBossDied, theme::kFxScreenFlashWarm, quiet);
    bloodSpray(event.pos, 70.0f, Vec2{}, 90, theme::kFxBlood);
    chunkBurst(event.pos, 70.0f, 60, theme::kFxGore, 3, 340.0f, 4);
    hotBurst(event.pos, 80.0f, 90, 460.0f, 1.0f, theme::kFxBossBurst[2]);
    groundRing(event.pos, 60, 620.0f, theme::kFxDust, 2, 0.7f, 3, kFlagTinted);
    if (g_quality > 0) addStain(event.pos, 34.0f, StainKind::Blood, g_random);
}

void onEvolution(const SimEvent& event, bool quiet) {
    addShake(kShakeEvolution, quiet);
    addHitPause(kPauseEvolution, quiet);
    requestScreenFlash(kFlashEvolution, theme::kFxScreenFlashEvolved, quiet);
    fountain(event.pos, 60, theme::kFxEvolvedSpark, 240.0f, 0.9f);
    groundRing(event.pos, 40, 460.0f, theme::kFxEvolvedSpark, 3, 0.5f, 2, kFlagHot);
}

// A kick of dust behind his feet. It only reads the dash direction, and a dash comes at
// most every few seconds, so it needs no budget.
void onPlayerDashed(const GameState& state, const SimEvent& event) {
    const Vec2 back = state.player.dashDir * -1.0f;
    Burst dust;
    dust.at = event.pos;
    dust.height = 3.0f;
    dust.count = 12;
    dust.speedMin = 40.0f;
    dust.speedMax = 190.0f;
    dust.liftMin = 4.0f;
    dust.liftMax = 26.0f;
    dust.lifeMin = 0.2f;
    dust.lifeMax = 0.4f;
    dust.gravity = 120.0f;
    dust.sizeMin = 1;
    dust.sizeMax = 2;
    dust.colors = theme::kFxDust;
    dust.colorCount = 2;
    dust.aim = back;
    dust.aimSpread = 0.6f;
    dust.flags = kFlagDrag | kFlagTinted;
    emitBurst(dust, g_random);
}

void onPickup(const SimEvent& event) {
    static constexpr Color kColors[static_cast<int>(PickupType::Count)][2] = {
        {theme::kPickupHeal, theme::kPickupIcon},
        {theme::kPickupMagnet, theme::kPickupIcon},
        {theme::kPickupBomb, theme::kPickupIcon},
        {theme::kPickupAmmo, theme::kPickupIcon},
    };
    const int type = std::clamp(static_cast<int>(event.detail), 0, static_cast<int>(PickupType::Count) - 1);
    sparkle(event.pos, 12, kColors[type], 2);
}

const Enemy* findBoss(const GameState& state) {
    if (!state.run.bossSpawned) return nullptr;
    for (const Enemy& enemy : state.enemies) {
        if (enemy.type == EnemyType::Boss) return &enemy;
    }
    return nullptr;
}

// True when a number here would sit over an attack warning on the ground.
bool overTelegraph(Vec2 world) {
    if (!g_lastState) return false;
    for (const Zone& zone : g_lastState->zones) {
        if (!zone.hostile) continue;
        if (zone.kind == ZoneKind::BossCircle) {
            if (distanceSq(world, zone.a) < zone.maxRadius * zone.maxRadius) return true;
        } else if (zone.kind == ZoneKind::BossLine) {
            const Vec2 along = zone.b - zone.a;
            const float t = clampf(dot(world - zone.a, along) / std::max(1.0f, lengthSq(along)), 0.0f, 1.0f);
            if (distanceSq(world, zone.a + along * t) < zone.radius * zone.radius) return true;
        }
    }
    return false;
}

}  // namespace

void init() {
    loadGlow();
    loadNumberGlyphs();
}

void applySettings(const Settings& settings) {
    g_settings = settings;
    setParticleAmount((settings.reducedMotion ? kReducedParticles : 1.0f) * (g_quality == 0 ? kWeakDeviceParticles : 1.0f));
    // Reduced motion takes effect at once, even in the middle of a shake or a pause.
    if (settings.reducedMotion || settings.screenShake <= 0.0f) g_shake.clear();
    g_shake.resample();
    if (settings.reducedMotion) g_hitPause.clear();
    if (!settings.damageNumbers) g_numbers.clear();
}

void setQuality(int quality) {
    g_quality = quality;
    applySettings(g_settings);
}

int quality() { return g_quality; }

void watchFrameTime(float frameSeconds, bool fighting) {
    // Lives on through a reset: what a device can do doesn't change with a new run.
    static FrameWatch watch;
    if (watch.update(frameSeconds, fighting) && g_quality > 0) setQuality(0);
}

void reset() {
    clearParticles();
    clearStains();
    g_numbers.clear();
    g_shake.clear();
    g_hitPause.clear();
    g_flashLeft = 0.0f;
    g_nearDeathArmed = true;
    g_bossEntrancePending = false;
    g_bossOnScreen = false;
}

void consumeEvents(const GameState& state, bool quiet) {
    g_lastState = &state;
    const Enemy* boss = findBoss(state);
    g_bossDistance = boss ? distance(boss->pos, state.player.pos) : -1.0f;
    for (const SimEvent& event : state.events) {
        switch (event.type) {
            case EventType::EnemyHit:
                if (!nearView(event.pos)) break;
                if (quiet) {
                    // The title screen shows the blood, but none of the numbers.
                    if (g_hitBudget.take()) bloodSpray(event.pos, kTargetHeight, awayFromPlayer(state, event.pos), 2, theme::kFxBlood);
                } else {
                    onEnemyHit(state, event, boss);
                }
                break;
            case EventType::EnemyKilled:
                if (nearView(event.pos)) onEnemyKilled(state, event, false, quiet);
                break;
            case EventType::EliteKilled:
                if (nearView(event.pos)) onEnemyKilled(state, event, true, quiet);
                break;
            case EventType::PlayerHit: onPlayerHit(state, event, quiet); break;
            case EventType::GemCollected:
                if (g_gemBudget.take()) {
                    const float xp = event.value;
                    sparkle(state.player.pos, 2, &theme::kFxGemSparkle[xp >= 20.0f ? 2 : (xp >= 5.0f ? 1 : 0)], 1);
                }
                break;
            case EventType::LevelUp:
                fountain(event.pos, 34, theme::kFxLevelUp, 150.0f, 0.75f);
                break;
            case EventType::PickupCollected: onPickup(event); break;
            case EventType::BombBlast: onBomb(event, quiet); break;
            case EventType::WeaponFired: onWeaponFired(state, event); break;
            case EventType::EvolutionGained: onEvolution(event, quiet); break;
            case EventType::BossSpawned:
                addShake(kShakeBossArrives, quiet);
                requestScreenFlash(kFlashBossArrives, theme::kFxScreenFlashHostile, quiet);
                g_bossEntrancePending = !quiet;
                break;
            case EventType::BossPhaseChanged: onBossPhase(event, quiet); break;
            case EventType::BossKilled: onBossKilled(event, quiet); break;
            case EventType::PlayerDied:
                addShake(kShakePlayerDied, quiet);
                bloodSpray(event.pos, kTargetHeight, Vec2{}, 28, theme::kFxPlayerBlood);
                chunkBurst(event.pos, kTargetHeight, 8, theme::kFxGore, 3, 130.0f, 3);
                break;
            case EventType::Explosion:
                if (nearView(event.pos) || distanceSq(event.pos, state.player.pos) < kShakeReach * kShakeReach) onExplosion(state, event, quiet);
                break;
            case EventType::PlayerDashed: onPlayerDashed(state, event); break;
            case EventType::ZombieUnstuck: break;
            case EventType::CarCrushed: onCarCrushed(state, event, quiet); break;
            case EventType::ReloadStarted:
            case EventType::WeaponDry: break;
        }
    }
}

void update(float dtReal, bool worldMoving) {
    g_clock += dtReal;
    const float worldDt = worldMoving ? dtReal : 0.0f;
    const float amount = particleAmount();
    g_hitBudget.refill(worldDt, amount);
    g_killBudget.refill(worldDt, amount);
    g_stainBudget.refill(worldDt, 1.0f);
    g_blastBudget.refill(worldDt, 1.0f);
    g_muzzleBudget.refill(worldDt, amount);
    g_casingBudget.refill(worldDt, amount);
    g_gemBudget.refill(worldDt, amount);
    g_playerBloodBudget.refill(worldDt, 1.0f);
    g_crushBudget.refill(worldDt, 1.0f);
    updateParticles(worldDt);
    updateStains(worldDt);
    g_numbers.update(worldDt);
    g_flashLeft = std::max(0.0f, g_flashLeft - dtReal);
    g_shake.update(dtReal, shakeScale(), g_random.range(-1.0f, 1.0f), g_random.range(-1.0f, 1.0f));
    g_counters.shakePeak = std::max({g_counters.shakePeak, static_cast<float>(std::abs(g_shake.x())), static_cast<float>(std::abs(g_shake.y()))});
    g_counters.zombiesFlashedPeak = std::max(g_counters.zombiesFlashedPeak, g_counters.zombiesFlashedNow);
    g_counters.zombiesFlashedNow = 0;
}

float filterSimulationTime(float dtReal) { return g_hitPause.filter(dtReal); }
bool hitPauseActive() { return g_hitPause.active(); }

void shakeArtPixels(int* columns, int* rows) {
    *columns = g_shake.x();
    *rows = g_shake.y();
}

void setFlashingCount(int flashing) {
    g_flashingCount = flashing;
    g_flashStride = flashStride(flashing);
    g_flashStrengthNow = crowdFlashStrength(flashing);
}

uint8_t zombieFlash(float secondsLeft, uint32_t id, bool important, bool boss) {
    if (!important && g_flashStride > 1 && id % static_cast<uint32_t>(g_flashStride) != 0u) return 0;
    float strength = g_flashStrengthNow;
    if (boss) strength = std::min(strength, kBossFlashMost);
    else if (important) strength = std::max(std::min(kEliteFlashMost, strength), kFlashCrowd);
    const float amount = hitFlashAmount(secondsLeft, balance::kHitFlashSeconds, strength, g_settings.flashIntensity, g_settings.reducedMotion);
    if (amount <= 0.0f) return 0;
    g_counters.hitFlashPeak = std::max(g_counters.hitFlashPeak, amount);
    g_counters.zombiesFlashedNow++;
    // Top value is kept below 255: that value marks a light pool in the sprite shader.
    return static_cast<uint8_t>(std::min(amount, 0.99f) * 255.0f);
}

uint8_t soldierFlash(float secondsLeft) {
    const float amount = hitFlashAmount(secondsLeft, kSoldierFlashSeconds, kSoldierFlashStrength, g_settings.flashIntensity, g_settings.reducedMotion);
    g_counters.hitFlashPeak = std::max(g_counters.hitFlashPeak, amount);
    return static_cast<uint8_t>(std::min(amount, 0.99f) * 255.0f);
}

float flashScale() { return g_settings.reducedMotion ? g_settings.flashIntensity * 0.5f : g_settings.flashIntensity; }

float explosionLight(float life) {
    const float light = life * flashScale();
    g_counters.explosionLightPeak = std::max(g_counters.explosionLightPeak, light);
    return light;
}

bool flickerAllowed() { return !g_settings.reducedMotion && g_settings.flashIntensity >= 0.05f; }
bool reducedMotion() { return g_settings.reducedMotion; }
bool glowEnabled() { return g_quality > 0; }

void queueGlowSpot(float x, float y, float radius, Color color, float strength) {
    if (g_quality > 0) queueGlow(x, y, radius, color, strength);
}

void noteBossOnScreen(bool onScreen, const GameState& state) {
    g_bossOnScreen = onScreen;
    if (!onScreen || !g_bossEntrancePending) return;
    g_bossEntrancePending = false;
    // It has just come into view: it shakes the street and throws up dust as it arrives.
    addShake(kShakeBossSeen, false);
    if (const Enemy* boss = findBoss(state)) groundRing(boss->pos, 44, 380.0f, theme::kFxDust, 2, 0.6f, 3, kFlagTinted);
}

void drawStains(const View& view, const float spriteTint[3]) {
    g_lastView = view;
    g_viewKnown = true;
    if (g_quality > 0) drawStainPool(view, spriteTint);
}

void drawParticles(const View& view, const float spriteTint[3]) {
    drawParticlePool(view, spriteTint, !g_settings.reducedMotion);
}

void drawGlow(const View& view) {
    if (g_quality == 0) return;
    queueHotParticleGlows(view, kHotGlowsMost);
    g_counters.glowsDrawn += flushGlow();
}

void drawScreenFlash(const View& view) {
    if (g_flashLeft <= 0.0f) return;
    const float t = g_flashLeft / g_flashFullSeconds;
    Color color = g_flashColor;
    color.a = static_cast<unsigned char>(255.0f * g_flashStrength * t * t);
    if (color.a == 0) return;
    const float w = static_cast<float>(view.width);
    const float h = static_cast<float>(view.height);
    // Added on top, not mixed in: light keeps the picture's contrast, where a pale wash
    // over everything just looks like fog.
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex2f(0.0f, 0.0f);
    rlVertex2f(0.0f, h);
    rlVertex2f(w, h);
    rlVertex2f(w, 0.0f);
    rlEnd();
    rlSetTexture(0);
    rlSetBlendMode(RL_BLEND_ALPHA);
}

void drawNumbers(const View& view, const GameState& state) {
    g_lastState = &state;
    if (!g_settings.damageNumbers || g_numbers.live() == 0) return;
    g_counters.numbersDrawnPeak = std::max(g_counters.numbersDrawnPeak, g_numbers.live());
    drawNumberPool(view, g_numbers, !g_settings.reducedMotion, overTelegraph);
}

const char* debugJson() {
    static char json[900];
    std::snprintf(json, sizeof(json),
                  "{\"particles\":%d,\"particlesSpawned\":%d,\"stains\":%d,\"numbers\":%d,\"numbersSpawned\":%d,\"numbersMerged\":%d,\"numbersDropped\":%d,"
                  "\"numberEvents\":%d,\"numbersPeak\":%d,\"shakeEvents\":%d,\"shakePeak\":%.1f,\"shakeX\":%d,\"shakeY\":%d,\"trauma\":%.3f,"
                  "\"hitPauses\":%d,\"hitPauseSeconds\":%.3f,\"pausing\":%d,\"flashesShown\":%d,\"flashesRefused\":%d,\"flashPeak\":%.3f,"
                  "\"explosionLightPeak\":%.3f,\"hitFlashPeak\":%.3f,\"zombiesFlashedPeak\":%d,\"flashingCount\":%d,\"glows\":%d,\"quality\":%d,\"amount\":%.2f,"
                  "\"bossOnScreen\":%d,\"bossDistance\":%.1f,\"reduced\":%d,\"numbersOn\":%d,\"shakeSetting\":%.2f,\"flashSetting\":%.2f}",
                  liveParticles(), spawnedParticles(), liveStains(), g_numbers.live(), g_numbers.spawned(), g_numbers.merged(), g_numbers.dropped(),
                  g_counters.numberEvents, g_counters.numbersDrawnPeak, g_counters.shakeEvents, static_cast<double>(g_counters.shakePeak), g_shake.x(), g_shake.y(),
                  static_cast<double>(g_shake.trauma()), g_counters.hitPauses, static_cast<double>(g_counters.hitPauseSeconds), g_hitPause.active() ? 1 : 0,
                  g_counters.flashesShown, g_counters.flashesRefused, static_cast<double>(g_counters.flashPeak), static_cast<double>(g_counters.explosionLightPeak), static_cast<double>(g_counters.hitFlashPeak),
                  g_counters.zombiesFlashedPeak, g_flashingCount, g_counters.glowsDrawn, g_quality, static_cast<double>(particleAmount()),
                  g_bossOnScreen ? 1 : 0, static_cast<double>(g_bossDistance), g_settings.reducedMotion ? 1 : 0, g_settings.damageNumbers ? 1 : 0, static_cast<double>(g_settings.screenShake),
                  static_cast<double>(g_settings.flashIntensity));
    return json;
}

void resetDebugCounters() { g_counters = Counters{}; }

}  // namespace ls::effects
