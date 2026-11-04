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
// Fields a weapon does not use stay 0. See each weapon's folder for what it reads.
struct WeaponLevel {
    float damage;
    float cooldown;
    int count;      // Shots, blades, chain jumps or missiles.
    float area;     // Radius, range or beam width.
    float speed;    // Projectile or ring speed.
    float duration; // Lifetime of the shot, mine or ring.
    int pierce;
};

inline constexpr WeaponLevel kWeaponLevels[static_cast<int>(WeaponId::Count)][kMaxLevelPerItem] = {
    // Pulse: shots at the nearest enemy.
    {{13, 0.62f, 1, 0, 620, 1.2f, 0}, {15, 0.54f, 1, 0, 640, 1.2f, 0}, {18, 0.48f, 2, 0, 660, 1.2f, 0},
     {22, 0.42f, 2, 0, 680, 1.2f, 1}, {27, 0.36f, 3, 0, 700, 1.2f, 1}},
    // Orbit: blades circling the player. Speed is radians per second.
    {{9, 0.35f, 2, 95, 3.4f, 0, 0}, {11, 0.33f, 2, 105, 3.6f, 0, 0}, {13, 0.31f, 3, 110, 3.8f, 0, 0},
     {16, 0.29f, 3, 120, 4.0f, 0, 0}, {20, 0.27f, 4, 130, 4.3f, 0, 0}},
    // Nova: radial burst around the player.
    {{16, 2.2f, 0, 150, 0, 0.30f, 0}, {20, 2.0f, 0, 165, 0, 0.30f, 0}, {25, 1.85f, 0, 180, 0, 0.30f, 0},
     {31, 1.7f, 0, 200, 0, 0.30f, 0}, {38, 1.5f, 0, 225, 0, 0.30f, 0}},
    // Arc: lightning that chains between enemies. Area is the jump range.
    {{14, 1.3f, 3, 170, 0, 0.18f, 0}, {17, 1.2f, 4, 180, 0, 0.18f, 0}, {21, 1.1f, 5, 190, 0, 0.18f, 0},
     {25, 1.0f, 6, 200, 0, 0.18f, 0}, {30, 0.9f, 8, 220, 0, 0.18f, 0}},
    // Mines: dropped behind the player. Area is the blast radius.
    {{32, 1.6f, 1, 85, 0, 7.0f, 0}, {40, 1.5f, 1, 95, 0, 7.0f, 0}, {50, 1.35f, 2, 105, 0, 8.0f, 0},
     {62, 1.2f, 2, 115, 0, 8.0f, 0}, {78, 1.05f, 3, 130, 0, 9.0f, 0}},
    // Lance: piercing beam toward the nearest enemy. Area is half width, speed is length.
    {{22, 1.9f, 1, 10, 700, 0.16f, 0}, {28, 1.75f, 1, 12, 760, 0.16f, 0}, {35, 1.6f, 2, 14, 820, 0.16f, 0},
     {44, 1.45f, 2, 16, 880, 0.16f, 0}, {55, 1.3f, 3, 19, 960, 0.16f, 0}},
    // Swarm: homing missiles. Area is the blast radius.
    {{11, 1.5f, 2, 40, 330, 3.0f, 0}, {13, 1.4f, 3, 44, 350, 3.0f, 0}, {16, 1.3f, 4, 48, 370, 3.0f, 0},
     {19, 1.2f, 5, 54, 390, 3.0f, 0}, {23, 1.1f, 6, 60, 420, 3.0f, 0}},
    // Ripple: expanding shockwave ring.
    {{12, 2.6f, 1, 260, 300, 0, 0}, {15, 2.4f, 1, 300, 320, 0, 0}, {19, 2.2f, 1, 340, 340, 0, 0},
     {24, 2.0f, 2, 380, 360, 0, 0}, {30, 1.8f, 2, 440, 380, 0, 0}},
};

inline constexpr float kShotRadius = 6.0f;
inline constexpr float kMissileRadius = 6.0f;
inline constexpr float kMissileTurnRate = 7.0f;
inline constexpr float kMineTriggerRadius = 26.0f;
inline constexpr float kOrbitBladeRadius = 15.0f;
inline constexpr float kWeaponRange = 720.0f;  // Weapons ignore enemies further than this.
inline constexpr float kShotSpreadRadians = 0.14f;
inline constexpr float kShotKnockback = 90.0f;
inline constexpr float kAreaKnockback = 170.0f;
inline constexpr float kKnockbackDecayPerSecond = 7.0f;
inline constexpr float kHitFlashSeconds = 0.09f;

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
    {WeaponId::Pulse, PassiveId::FireRate, "Storm Pulse", 1.35f, 0.65f, 2, 1.0f, 2},
    {WeaponId::Orbit, PassiveId::Area, "Halo Saw", 1.5f, 0.8f, 2, 1.25f, 0},
    {WeaponId::Nova, PassiveId::Damage, "Supernova", 1.8f, 0.8f, 0, 1.35f, 0},
    {WeaponId::Arc, PassiveId::PickupRange, "Tempest", 1.5f, 0.75f, 5, 1.3f, 0},
};

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
    /* Drifter  */ {18, 70, 13, 8, 1, 1.0f},
    /* Swarmer  */ {7, 150, 8, 5, 1, 1.3f},
    /* Brute    */ {130, 48, 26, 18, 6, 0.25f},
    /* Spitter  */ {32, 80, 14, 6, 3, 0.9f},
    /* Splitter */ {55, 62, 19, 10, 3, 0.7f},
    /* Shard    */ {12, 115, 9, 5, 1, 1.2f},
    /* Charger  */ {60, 75, 16, 14, 4, 0.5f},
    /* Boss     */ {16000, 72, 58, 26, 0, 0.0f},
};

// Enemy HP grows with time, so upgrades do not make the late game trivial.
inline constexpr float enemyHpScale(float seconds) { return 1.0f + seconds / 150.0f; }
inline constexpr float enemyDamageScale(float seconds) { return 1.0f + seconds / 400.0f; }

inline constexpr float kElite_HpMult = 8.0f;
inline constexpr float kElite_RadiusMult = 1.5f;
inline constexpr float kElite_DamageMult = 1.5f;
inline constexpr int kElite_XpMult = 8;

inline constexpr float kSpawnRadius = 820.0f;     // Just outside the widest view.
inline constexpr float kRecycleRadius = 1400.0f;  // Enemies left this far behind reappear ahead.
inline constexpr float kSeparationStrength = 60.0f;

inline constexpr int kShardsPerSplit = 3;
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

// --- Boss --------------------------------------------------------------------
inline constexpr float kBossPhase2HpFraction = 0.66f;
inline constexpr float kBossPhase3HpFraction = 0.33f;
inline constexpr float kBossAttackInterval[3] = {3.2f, 2.6f, 2.0f};
inline constexpr float kBossTelegraphSeconds = 1.1f;
inline constexpr float kBossCircleRadius = 150.0f;
inline constexpr float kBossCircleDamage = 26.0f;
inline constexpr int kBossCircleCount[3] = {1, 3, 5};
inline constexpr float kBossLineHalfWidth = 42.0f;
inline constexpr float kBossLineLength = 1500.0f;
inline constexpr float kBossLineDamage = 30.0f;
inline constexpr int kBossBulletRing[3] = {14, 20, 28};
inline constexpr float kBossBulletSpeed = 190.0f;
inline constexpr float kBossBulletDamage = 12.0f;
inline constexpr float kBossDashSpeed = 480.0f;

// --- Waves -------------------------------------------------------------------
// Each row spawns one enemy type over a time window. The rate moves from
// startRate to endRate, in enemies per second. Gaps between strong rows are the
// calmer moments.
struct WaveRow {
    float startSec;
    float endSec;
    EnemyType type;
    float startRate;
    float endRate;
    int groupSize;
};

inline constexpr WaveRow kWaves[] = {
    {0, 60, EnemyType::Drifter, 1.0f, 2.2f, 1},
    {45, 170, EnemyType::Swarmer, 0.5f, 1.2f, 5},
    {60, 165, EnemyType::Drifter, 2.0f, 3.5f, 1},
    {120, 170, EnemyType::Brute, 0.15f, 0.3f, 1},
    // 2:50 to 3:05 is a short breather, then the Spitters arrive.
    {185, 340, EnemyType::Spitter, 0.35f, 0.8f, 1},
    {185, 340, EnemyType::Drifter, 3.5f, 6.0f, 1},
    {210, 345, EnemyType::Splitter, 0.3f, 0.9f, 1},
    {260, 345, EnemyType::Swarmer, 1.0f, 2.0f, 7},
    // 5:45 to 6:00 is the midpoint breather.
    {360, 480, EnemyType::Charger, 0.3f, 0.9f, 1},
    {360, 480, EnemyType::Brute, 0.3f, 0.7f, 1},
    {360, 480, EnemyType::Drifter, 6.0f, 10.0f, 1},
    {400, 480, EnemyType::Spitter, 0.8f, 1.4f, 1},
    // 8:00 to 8:10 is the last breather before the final push.
    {490, 600, EnemyType::Drifter, 10.0f, 18.0f, 1},
    {490, 600, EnemyType::Swarmer, 2.0f, 3.5f, 8},
    {490, 600, EnemyType::Brute, 0.7f, 1.3f, 1},
    {490, 600, EnemyType::Charger, 0.9f, 1.5f, 1},
    {490, 600, EnemyType::Splitter, 0.9f, 1.5f, 1},
    {490, 600, EnemyType::Spitter, 1.2f, 2.0f, 1},
};

struct EliteEvent {
    float timeSec;
    EnemyType type;
};

inline constexpr EliteEvent kEliteEvents[] = {
    {90, EnemyType::Brute},   {150, EnemyType::Drifter},  {210, EnemyType::Splitter}, {270, EnemyType::Spitter},
    {330, EnemyType::Brute},  {390, EnemyType::Charger},  {450, EnemyType::Splitter}, {510, EnemyType::Brute},
    {555, EnemyType::Charger}, {585, EnemyType::Brute},
};

// --- Pickups and gems ---------------------------------------------------------
inline constexpr float kGemAttractSpeed = 520.0f;
inline constexpr float kPickupCollectRadius = 34.0f;
inline constexpr int kGemMergeStartCount = 500;  // Above this, nearby gems merge into one.
inline constexpr float kGemMergeCellSize = 96.0f;
inline constexpr int kKillsPerPickupDrop = 70;   // A pickup drops about this often.
inline constexpr float kHealPickupFraction = 0.25f;
inline constexpr float kBombDamage = 1200.0f;
inline constexpr float kBombRadius = 760.0f;

}  // namespace ls::balance
