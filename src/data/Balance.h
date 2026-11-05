#pragma once

#include "core/GameState.h"

// Every balance number in the game lives in this file, so tuning never needs a
// logic change. Times are seconds, distances are world units, and speeds are
// world units per second. The camera shows about 1280 world units on the long
// side of the screen.

namespace ls::balance {

// --- Run ---------------------------------------------------------------------
inline constexpr float kRunSeconds = 600.0f;  // Boss arrives at 10:00.
inline constexpr int kStartingRerolls = 2;

// --- Player ------------------------------------------------------------------
inline constexpr float kPlayerMaxHp = 100.0f;
inline constexpr float kPlayerRadius = 14.0f;
inline constexpr float kPlayerSpeed = 230.0f;
inline constexpr float kPlayerPickupRange = 80.0f;
inline constexpr float kPlayerInvulnerableAfterHit = 0.35f;
inline constexpr float kMinDamageTaken = 1.0f;
inline constexpr float kHealCardFraction = 0.30f;
inline constexpr float kPlayerHitFlashSeconds = 0.15f;

// --- Dash --------------------------------------------------------------------
// A short burst of speed with a cooldown. The cooldown runs from the moment of the
// dash, and only counts down while the game is being played.
inline constexpr float kDashSeconds = 0.20f;
inline constexpr float kDashDistance = 170.0f;  // About 3.7 times what walking covers in the same time.
inline constexpr float kDashSpeed = kDashDistance / kDashSeconds;
inline constexpr float kDashInvulnerableSeconds = 0.15f;  // From the start of the dash only.
inline constexpr float kDashCooldownSeconds = 4.0f;

// --- Solid cars --------------------------------------------------------------
// Their sizes come from their pictures, and live with the map's layout.
inline constexpr float kCarClearOfStart = 240.0f;    // No car this close to the start, a full dash and more away.
inline constexpr float kCarCornerRadius = 8.0f;      // Rounded corners, so nothing snags on one.
inline constexpr float kCarSteerLookAhead = 24.0f;   // How far past its edge a zombie looks for a car.
inline constexpr float kZombieCarRadiusShare = 0.8f; // Zombies squeeze in a little closer than their full width.
inline constexpr int kZombieStuckSteps = 330;        // A slow Brute takes about this long to walk around a bus.
inline constexpr float kChargerCrashKnock = 120.0f;  // It bounces back off the car this hard.
inline constexpr float kChargerCrashSeconds = 1.5f;  // Stunned for this long, longer than a normal recovery.
inline constexpr float kItemCarRadius = 10.0f;       // How far from a car a dropped gem, pickup or mine lands.

// XP needed to go from this level to the next.
inline constexpr int xpForLevel(int level) { return 5 + (level - 1) * 4 + (level - 1) * (level - 1) / 3; }

// --- Passives (index 0 is level 1) -------------------------------------------
inline constexpr float kPassiveDamage[kMaxLevelPerItem] = {1.10f, 1.20f, 1.32f, 1.45f, 1.60f};
inline constexpr float kPassiveCooldown[kMaxLevelPerItem] = {0.94f, 0.88f, 0.82f, 0.77f, 0.72f};
inline constexpr float kPassiveArea[kMaxLevelPerItem] = {1.10f, 1.20f, 1.30f, 1.40f, 1.55f};
inline constexpr float kPassiveMoveSpeed[kMaxLevelPerItem] = {1.06f, 1.12f, 1.18f, 1.24f, 1.30f};
inline constexpr float kPassivePickupRange[kMaxLevelPerItem] = {1.30f, 1.60f, 1.90f, 2.30f, 2.80f};
inline constexpr float kPassiveArmor[kMaxLevelPerItem] = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f};

// --- Weapons -----------------------------------------------------------------
// Fields a weapon does not use stay 0. A magazine of 0 means the weapon uses no ammo.
// One trigger pull uses one round, however many bullets it fires, so upgrades
// that add bullets per shot are a pure gain.
struct WeaponLevel {
    float damage;
    float cooldown;
    int count;       // Bullets per trigger, pellets, grenades, rockets, mines, chain jumps or blades.
    float area;      // Blast radius, beam half width, flame width, jump range or blade circle radius.
    float speed;     // Projectile speed, beam length, or blade spin in radians per second.
    float duration;  // Lifetime of the bullet, flame, grenade fuse, rocket or mine.
    int pierce;      // Extra enemies a bullet or flame passes through.
    int magazine;    // Rounds loaded at once.
    int maxReserve;  // Most rounds carried outside the magazine.
    float reload;    // Seconds to reload.
};

// clang-format off
inline constexpr WeaponLevel kWeaponLevels[static_cast<int>(WeaponId::Count)][kMaxLevelPerItem] = {
    // Rifle: aimed bursts at the nearest zombie. The starting weapon.
    {{15, 0.34f, 1, 0, 720, 1.1f, 0, 30, 180, 1.2f}, {17, 0.31f, 1, 0, 740, 1.1f, 0, 30, 210, 1.15f}, {18, 0.32f, 2, 0, 760, 1.1f, 0, 36, 240, 1.1f},
     {22, 0.30f, 2, 0, 780, 1.1f, 1, 40, 270, 1.05f}, {26, 0.28f, 3, 0, 800, 1.1f, 1, 45, 300, 1.0f}},
    // Shotgun: a wide cone of short-range pellets.
    {{17, 0.90f, 5, 0, 560, 0.58f, 0, 6, 36, 1.3f}, {19, 0.85f, 6, 0, 570, 0.60f, 0, 6, 42, 1.2f}, {23, 0.80f, 7, 0, 580, 0.62f, 0, 8, 54, 1.15f},
     {26, 0.75f, 8, 0, 590, 0.64f, 1, 8, 60, 1.1f}, {31, 0.68f, 9, 0, 600, 0.66f, 1, 10, 72, 1.05f}},
    // SMG: very fast, weak bullets with some scatter.
    {{7, 0.12f, 1, 0, 700, 0.8f, 0, 40, 240, 1.4f}, {8, 0.11f, 1, 0, 710, 0.8f, 0, 45, 280, 1.35f}, {9, 0.10f, 1, 0, 720, 0.85f, 0, 50, 320, 1.3f},
     {10, 0.095f, 2, 0, 730, 0.85f, 0, 55, 360, 1.25f}, {12, 0.09f, 2, 0, 740, 0.9f, 1, 60, 400, 1.2f}},
    // Sniper: an instant beam through every zombie in a line, at the toughest target in range.
    {{40, 1.60f, 1, 6, 1000, 0.14f, 0, 5, 30, 1.8f}, {50, 1.50f, 1, 7, 1050, 0.14f, 0, 5, 35, 1.7f}, {63, 1.38f, 1, 8, 1100, 0.14f, 0, 6, 42, 1.6f},
     {80, 1.25f, 1, 9, 1150, 0.14f, 0, 6, 48, 1.5f}, {100, 1.10f, 2, 11, 1200, 0.14f, 0, 8, 56, 1.4f}},
    // Flamethrower: a stream of short-lived flames that pass through zombies.
    {{6, 0.07f, 1, 16, 340, 0.62f, 3, 100, 300, 2.0f}, {7, 0.07f, 1, 18, 350, 0.65f, 3, 110, 340, 1.9f}, {8, 0.065f, 2, 20, 360, 0.68f, 4, 120, 380, 1.8f},
     {10, 0.06f, 2, 22, 370, 0.72f, 4, 130, 420, 1.7f}, {13, 0.055f, 3, 25, 380, 0.78f, 5, 150, 480, 1.6f}},
    // Grenade Launcher: lobbed at the nearest zombie, exploding when the fuse ends.
    {{30, 1.60f, 1, 80, 340, 0.75f, 0, 4, 20, 1.6f}, {38, 1.50f, 1, 90, 350, 0.75f, 0, 4, 24, 1.5f}, {52, 1.40f, 1, 100, 360, 0.75f, 0, 5, 30, 1.45f},
     {66, 1.28f, 2, 110, 370, 0.75f, 0, 6, 36, 1.4f}, {80, 1.15f, 2, 125, 380, 0.75f, 0, 6, 42, 1.3f}},
    // Homing Rockets: seek zombies, and explode on contact.
    {{11, 1.50f, 2, 40, 330, 3.0f, 0, 6, 30, 1.6f}, {13, 1.40f, 3, 44, 350, 3.0f, 0, 6, 36, 1.5f}, {16, 1.30f, 4, 48, 370, 3.0f, 0, 8, 42, 1.45f},
     {19, 1.20f, 5, 54, 390, 3.0f, 0, 8, 48, 1.4f}, {23, 1.10f, 6, 60, 420, 3.0f, 0, 10, 54, 1.3f}},
    // Landmines: dropped behind the player, exploding when a zombie steps close.
    {{32, 1.60f, 1, 85, 0, 7.0f, 0, 3, 26, 1.2f}, {40, 1.50f, 1, 95, 0, 7.0f, 0, 3, 30, 1.2f}, {56, 1.35f, 1, 105, 0, 8.0f, 0, 4, 35, 1.1f},
     {68, 1.20f, 2, 115, 0, 8.0f, 0, 4, 40, 1.1f}, {84, 1.05f, 2, 130, 0, 9.0f, 0, 5, 45, 1.0f}},
    // Tesla Coil: lightning that chains between zombies. Uses no ammo.
    {{17, 1.00f, 3, 170, 0, 0.18f, 0, 0, 0, 0}, {20, 0.95f, 4, 180, 0, 0.18f, 0, 0, 0, 0}, {23, 0.90f, 5, 190, 0, 0.18f, 0, 0, 0, 0},
     {27, 0.85f, 6, 200, 0, 0.18f, 0, 0, 0, 0}, {31, 0.80f, 8, 220, 0, 0.18f, 0, 0, 0, 0}},
    // Combat Blades: circle the player. Cooldown is how often one zombie can be hit. Uses no ammo.
    {{17, 0.28f, 3, 110, 4.2f, 0, 0, 0, 0, 0}, {21, 0.26f, 3, 118, 4.4f, 0, 0, 0, 0, 0}, {25, 0.24f, 4, 126, 4.6f, 0, 0, 0, 0, 0},
     {30, 0.22f, 4, 134, 4.8f, 0, 0, 0, 0, 0}, {36, 0.20f, 5, 145, 5.0f, 0, 0, 0, 0, 0}},
};
// clang-format on

// The tables are sized by the enum, so a missing row would quietly fill with zeros.
static_assert(kWeaponLevels[static_cast<int>(WeaponId::Count) - 1][0].cooldown > 0.0f, "a weapon row is missing");

inline constexpr float kBulletRadius = 6.0f;
inline constexpr float kPelletRadius = 4.5f;
inline constexpr float kFlameRadius = 13.0f;
inline constexpr float kGrenadeRadius = 7.0f;
inline constexpr float kRocketRadius = 6.0f;
inline constexpr float kRocketTurnRate = 7.0f;
inline constexpr float kMineTriggerRadius = 26.0f;
inline constexpr float kBladeRadius = 15.0f;
// Every second blade spins on this smaller circle, so zombies right next to the
// player get cut too. The outer circle alone passes over their heads.
inline constexpr float kBladeInnerRadiusFraction = 0.38f;
inline constexpr float kWeaponRange = 720.0f;  // Weapons ignore zombies further than this.
inline constexpr float kBulletSpreadRadians = 0.14f;   // Between rifle bullets in one trigger pull.
inline constexpr float kShotgunConeRadians = 0.62f;    // Full width of the pellet cone.
inline constexpr float kSmgScatterRadians = 0.10f;     // Random scatter either way.
inline constexpr float kFlameConeRadians = 0.30f;
inline constexpr float kExplosionSeconds = 0.30f;      // How long an explosion is drawn.
inline constexpr float kShotKnockback = 90.0f;
inline constexpr float kAreaKnockback = 170.0f;
inline constexpr float kKnockbackDecayPerSecond = 7.0f;
inline constexpr float kHitFlashSeconds = 0.09f;

// The Rifle never fully runs dry, so an empty player can always fight back. When
// its ammo is gone, it fires free single shots at this much of its normal rate.
inline constexpr float kRifleDryCooldownMult = 1.6f;
inline constexpr float kRifleDryDamageMult = 0.9f;

// --- Evolutions --------------------------------------------------------------
// A level 5 weapon plus this passive offers an evolution card.
struct Evolution {
    WeaponId weapon;
    PassiveId passive;
    const char* name;
    float damageMult;
    float cooldownMult;
    int countBonus;
    float areaMult;
    int pierceBonus;
};

inline constexpr Evolution kEvolutions[] = {
    {WeaponId::Rifle, PassiveId::FireRate, "Storm Rifle", 1.25f, 0.75f, 1, 1.0f, 1},
    {WeaponId::Shotgun, PassiveId::Area, "Street Sweeper", 1.3f, 0.8f, 3, 1.0f, 1},
    {WeaponId::Flamethrower, PassiveId::Damage, "Inferno", 1.6f, 0.85f, 2, 1.4f, 3},
    {WeaponId::Tesla, PassiveId::PickupRange, "Tempest", 1.4f, 0.8f, 4, 1.25f, 0},
    {WeaponId::Blades, PassiveId::Armor, "Halo Saw", 1.4f, 0.85f, 2, 1.25f, 0},
};

// --- Weapon extras -----------------------------------------------------------
// Boss radius 58, or the biggest elite (Brute 26 * 1.5), whichever is larger, plus a margin.
// Grid queries add this to a shot's radius so no big enemy is missed.
inline constexpr float kLargestEnemyRadius = 87.0f;
inline constexpr float kShortRangeSlack = 1.3f;          // Shotgun and grenades aim a little past their reach.
inline constexpr float kSniperBeamSpreadRadians = 0.06f; // Between beams when the Sniper fires several.
inline constexpr float kRocketFanStepRadians = 0.35f;    // Between rockets at launch, before they home.
inline constexpr float kGrenadeScatterRadians = 0.12f;   // Used when there are more grenades than targets.
inline constexpr float kMineDropDistance = 40.0f;        // How far behind the player a mine lands.
inline constexpr float kMineSpreadRadians = 0.45f;       // Between mines dropped in one trigger pull.
inline constexpr float kFlameBaseArea = 16.0f;           // Flame area at which a flame has exactly kFlameRadius.
inline constexpr int kMaxWeaponTargets = 16;               // Fixed size of target and hit lists, so no heap is needed.

// --- Difficulty --------------------------------------------------------------
// One small table, so a level is only these numbers and needs no logic of its own.
struct DifficultyTuning {
    float enemyHp;      // Multiplies zombie and boss health.
    float enemyDamage;  // Multiplies every hit on the player, before armor.
    float spawnRate;    // Multiplies the wave table's spawn rates.
};

inline constexpr DifficultyTuning kDifficulties[static_cast<int>(Difficulty::Count)] = {
    /* Easy   */ {0.5f, 0.45f, 0.6f},
    /* Normal */ {1.0f, 1.0f, 1.0f},
    /* Hard   */ {1.3f, 1.25f, 1.2f},
};

inline constexpr const DifficultyTuning& difficultyTuning(Difficulty difficulty) {
    return kDifficulties[static_cast<int>(difficulty)];
}

// --- Enemies -----------------------------------------------------------------
struct EnemyStats {
    float hp;
    float speed;
    float radius;
    float contactDamage;
    int xp;
    float knockbackTaken;  // 1 is normal, and lower means heavier.
};

inline constexpr EnemyStats kEnemyStats[static_cast<int>(EnemyType::Count)] = {
    /* Walker   */ {18, 70, 13, 8, 1, 1.0f},
    /* Runner   */ {7, 150, 8, 5, 1, 1.3f},
    /* Brute    */ {130, 48, 26, 18, 6, 0.25f},
    /* Spitter  */ {32, 80, 14, 6, 3, 0.9f},
    /* Bloater  */ {55, 62, 19, 10, 3, 0.7f},
    /* Crawler  */ {12, 115, 9, 5, 1, 1.2f},
    /* Charger  */ {60, 75, 16, 14, 4, 0.5f},
    /* Boss     */ {65000, 72, 58, 30, 0, 0.0f},  // HP is replaced at spawn. See kBossHpAtReferenceDps.
};

// Enemy HP grows with time, so upgrades do not make the late game trivial.
// Both stop growing at the boss, so a long boss fight does not make its Walkers tougher and tougher.
inline constexpr float enemyHpScale(float seconds) {
    const float late = (seconds < kRunSeconds ? seconds : kRunSeconds) / kRunSeconds;
    return 1.0f + late * kRunSeconds / 120.0f + 2.5f * late * late;  // Straight growth, with a steeper late game.
}
inline constexpr float enemyDamageScale(float seconds) {
    return 1.0f + (seconds < kRunSeconds ? seconds : kRunSeconds) / 340.0f;
}

inline constexpr float kElite_HpMult = 8.0f;
inline constexpr float kElite_RadiusMult = 1.5f;
inline constexpr float kElite_DamageMult = 1.5f;
inline constexpr int kElite_XpMult = 8;
inline constexpr int kElite_CrawlerMult = 2;  // An elite Bloater releases this many times the crawlers.

// The widest enemy that separation can push, as an elite. The boss is left out, because it
// is never pushed. Worked out from the table, so it stays right when the table changes.
inline constexpr float largestPushedEnemyRadius() {
    float largest = 0.0f;
    for (int i = 0; i < static_cast<int>(EnemyType::Boss); ++i) {
        if (kEnemyStats[i].radius > largest) largest = kEnemyStats[i].radius;
    }
    return largest * kElite_RadiusMult;
}

// Just outside the widest view, including portrait. The camera may only show what this
// circle allows, so this also sets how much a tall phone can see: 1200 shows 20% more
// than 960 did, and the balance tool shows Novice survival doesn't change.
inline constexpr float kSpawnRadius = 1200.0f;
// The boss is far wider and taller than a zombie, so it starts this much further out to
// be fully out of sight on any window shape. Catch-up speed brings it in.
inline constexpr float kBossSpawnExtra = 140.0f;
inline constexpr float kRecycleRadius = 1500.0f;  // Enemies left this far behind reappear ahead.
inline constexpr float kSeparationStrength = 60.0f;
inline constexpr float kSeparationReach = 16.0f;  // An enemy looks this far past its own edge for neighbours.
inline constexpr int kSeparationMaxChecks = 8;    // Neighbours looked at per enemy, which keeps huge crowds cheap.

inline constexpr int kCrawlersPerBloater = 3;
inline constexpr float kSpitterKeepDistance = 340.0f;
inline constexpr float kSpitterFireInterval = 2.4f;
inline constexpr float kSpitterBulletSpeed = 210.0f;
inline constexpr float kSpitterBulletDamage = 9.0f;
inline constexpr float kEnemyBulletRadius = 7.0f;
inline constexpr float kEnemyBulletLife = 5.0f;
inline constexpr float kChargerTriggerDistance = 340.0f;
inline constexpr float kChargerTelegraphSeconds = 0.8f;
inline constexpr float kChargerDashSeconds = 0.55f;
inline constexpr float kChargerDashSpeed = 560.0f;
inline constexpr float kChargerRecoverSeconds = 1.1f;

// --- Enemy extras ------------------------------------------------------------
inline constexpr float kRunnerWeaveSpeedFraction = 0.35f;   // Sideways speed, as a share of forward speed.
inline constexpr float kRunnerWeaveRadiansPerSecond = 5.0f;
inline constexpr float kRunnerWeavePhasePerId = 2.4f;       // Spreads a pack out of step.
inline constexpr float kSpitterDistanceBand = 40.0f;        // Slack around the keep distance before it moves.
inline constexpr float kSpitterStrafeSpeedFraction = 0.6f;
inline constexpr float kBloaterCrawlerSpread = 22.0f;       // How far from the corpse crawlers appear.
inline constexpr float kBombBossMaxHpFraction = 0.015f;      // A bomb takes at most this much of the boss's HP.
inline constexpr float kPickupDropOffset = 18.0f;           // Moves a second drop off the first.
inline constexpr float kPickupDropFanRadians = 2.1f;        // Angle between drops from one kill.
inline constexpr float kWaveGroupSpread = 44.0f;            // A wave group spawns within this of its center.
inline constexpr uint32_t kSpitterFirePhases = 16;          // Spitters start at one of this many points in the fire cycle.
inline constexpr float kBossDashSeconds = 0.6f;
inline constexpr float kBossCircleScatter = 260.0f;         // Extra circles land within this of the player.
inline constexpr float kBossSecondLineRadians = 0.5f;       // Extra lines in the last phase, either side.
inline constexpr float kBossLeashDistance = 560.0f;         // Further than this, the boss hurries to catch up.
// The boss stands still during telegraphs, so this must beat the player by a lot. Worst case
// is its last phase: 6.4 s of walking in a 9 s cycle, plus a 288 unit dash, is 3040 units per
// cycle. The fastest player (speed passive 5, dashing whenever allowed) averages 327 units per
// second, or 2940 per cycle. So the boss always gains, and running away can't stall a run.
inline constexpr float kBossCatchUpSpeed = 430.0f;
inline constexpr float kBossAddsPerSecond = 0.2f;           // Walkers during the boss fight, which keep XP coming.
inline constexpr float kBossAmmoCrateSeconds = 12.0f;       // A crate lands this often, so guns can't stay empty.
inline constexpr float kBossAmmoCrateDistance = 50.0f;      // How far from the player it lands.

// Boss HP is set from the player's build when the boss arrives, so a weak build
// isn't stuck in an endless fight and a strong build isn't done in seconds. The power
// is below 1, so a stronger build still kills the boss faster, and building well pays.
inline constexpr float kBossHpAtReferenceDps = 46000.0f;    // HP for a build with kBossReferenceDps.
inline constexpr float kBossReferenceDps = 1000.0f;         // Estimated damage per second on one target.
inline constexpr float kBossHpScalePower = 0.75f;
inline constexpr float kBossHpMin = 15000.0f;
inline constexpr float kBossHpMax = 120000.0f;
// How much of each weapon's on-paper damage really lands on one big moving target.
// Short-range and ground weapons land less, since the boss rarely stays in reach.
inline constexpr float kBossDamageShare[static_cast<int>(WeaponId::Count)] = {
    /* Rifle           */ 0.85f,
    /* Shotgun         */ 0.5f,
    /* Smg             */ 0.8f,
    /* Sniper          */ 0.9f,
    /* Flamethrower    */ 0.4f,
    /* GrenadeLauncher */ 0.7f,
    /* Rockets         */ 0.8f,
    /* Landmines       */ 0.3f,
    /* Tesla           */ 1.0f,
    /* Blades          */ 0.4f,
};
static_assert(kBossDamageShare[static_cast<int>(WeaponId::Count) - 1] > 0.0f, "a weapon is missing from kBossDamageShare");

// --- Boss --------------------------------------------------------------------
inline constexpr float kBossPhase2HpFraction = 0.66f;
inline constexpr float kBossPhase3HpFraction = 0.33f;
inline constexpr float kBossAttackInterval[3] = {2.8f, 2.2f, 1.6f};
inline constexpr float kBossTelegraphSeconds = 1.0f;
inline constexpr float kBossCircleRadius = 165.0f;
inline constexpr float kBossCircleDamage = 34.0f;
inline constexpr int kBossCircleCount[3] = {2, 4, 6};
inline constexpr float kBossLineHalfWidth = 42.0f;
inline constexpr float kBossLineLength = 1500.0f;
inline constexpr float kBossLineDamage = 38.0f;
inline constexpr int kBossBulletRing[3] = {16, 24, 32};
inline constexpr float kBossBulletSpeed = 210.0f;
inline constexpr float kBossBulletDamage = 16.0f;
inline constexpr float kBossDashSpeed = 480.0f;

// --- Waves -------------------------------------------------------------------
// Each row spawns one enemy type over a time window. The rate moves from
// startRate to endRate, in groups per second, and each group is groupSize enemies
// that arrive together. Gaps between strong rows are the calmer moments.
struct WaveRow {
    float startSec;
    float endSec;
    EnemyType type;
    float startRate;
    float endRate;
    int groupSize;
};

inline constexpr WaveRow kWaves[] = {
    {0, 60, EnemyType::Walker, 1.0f, 2.2f, 1},
    {75, 170, EnemyType::Runner, 0.3f, 0.9f, 5},
    {60, 165, EnemyType::Walker, 1.6f, 3.2f, 1},
    {120, 170, EnemyType::Brute, 0.15f, 0.3f, 1},
    // 2:50 to 3:05 is a short breather, then the Spitters arrive.
    {185, 340, EnemyType::Spitter, 0.35f, 0.8f, 1},
    {185, 340, EnemyType::Walker, 3.5f, 6.0f, 1},
    {210, 345, EnemyType::Bloater, 0.3f, 0.9f, 1},
    {260, 345, EnemyType::Runner, 1.0f, 2.0f, 7},
    // 5:45 to 6:00 is the midpoint breather.
    {360, 480, EnemyType::Charger, 0.3f, 0.9f, 1},
    {360, 480, EnemyType::Brute, 0.3f, 0.7f, 1},
    {360, 480, EnemyType::Walker, 8.0f, 13.0f, 1},
    {400, 480, EnemyType::Spitter, 0.8f, 1.4f, 1},
    // 8:00 to 8:10 is the last breather before the final push.
    {490, 600, EnemyType::Walker, 14.0f, 24.0f, 1},
    {490, 600, EnemyType::Runner, 1.4f, 2.4f, 8},
    {490, 600, EnemyType::Brute, 0.7f, 1.3f, 1},
    {490, 600, EnemyType::Charger, 0.9f, 1.5f, 1},
    {490, 600, EnemyType::Bloater, 0.9f, 1.5f, 1},
    {490, 600, EnemyType::Spitter, 1.2f, 2.0f, 1},
};

struct EliteEvent {
    float timeSec;
    EnemyType type;
};

inline constexpr EliteEvent kEliteEvents[] = {
    {90, EnemyType::Brute},   {150, EnemyType::Walker},  {210, EnemyType::Bloater}, {270, EnemyType::Spitter},
    {330, EnemyType::Brute},  {390, EnemyType::Charger},  {450, EnemyType::Bloater}, {510, EnemyType::Brute},
    {555, EnemyType::Charger}, {585, EnemyType::Brute},
};

// --- Pickups and gems ---------------------------------------------------------
inline constexpr float kGemAttractSpeed = 520.0f;
inline constexpr float kPickupCollectRadius = 34.0f;
inline constexpr float kGemCollectMargin = 6.0f;  // A gem is collected this far past the player's edge.
inline constexpr int kGemMergeStartCount = 500;  // Above this, nearby gems merge into one.
inline constexpr float kGemMergeCellSize = 96.0f;
inline constexpr uint32_t kGemMergeIntervalSteps = 30;  // How often merging runs: half a second at 60 steps per second.
inline constexpr int kKillsPerPickupDrop = 70;   // A pickup drops about this often.
inline constexpr float kHealPickupFraction = 0.25f;
inline constexpr float kAmmoCrateFraction = 0.40f;  // Refills this much of each gun's reserve.
inline constexpr int kKillsPerAmmoCrate = 200;     // An ammo crate drops about this often.
// A crate also lands near the player on this timer, so a player making few kills
// still gets a supply. Kill drops alone reward those who need ammo least.
inline constexpr float kTimedAmmoCrateSeconds = 40.0f;
inline constexpr float kTimedAmmoCrateDistance = 110.0f;
inline constexpr float kBombDamage = 1200.0f;
inline constexpr float kBombRadius = 760.0f;

}  // namespace ls::balance
