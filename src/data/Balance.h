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
    {{13, 0.45f, 1, 0, 720, 1.1f, 0, 30, 180, 1.2f}, {15, 0.41f, 1, 0, 740, 1.1f, 0, 30, 210, 1.15f}, {18, 0.38f, 2, 0, 760, 1.1f, 0, 36, 240, 1.1f},
     {22, 0.34f, 2, 0, 780, 1.1f, 1, 40, 270, 1.05f}, {27, 0.30f, 3, 0, 800, 1.1f, 1, 45, 300, 1.0f}},
    // Shotgun: a wide cone of short-range pellets.
    {{6, 1.10f, 5, 0, 560, 0.45f, 0, 6, 36, 1.5f}, {7, 1.00f, 6, 0, 570, 0.45f, 0, 6, 42, 1.4f}, {8, 0.92f, 7, 0, 580, 0.48f, 0, 8, 54, 1.3f},
     {9, 0.84f, 8, 0, 590, 0.50f, 1, 8, 60, 1.25f}, {11, 0.75f, 9, 0, 600, 0.52f, 1, 10, 72, 1.2f}},
    // SMG: very fast, weak bullets with some scatter.
    {{5, 0.13f, 1, 0, 700, 0.8f, 0, 40, 240, 1.4f}, {6, 0.12f, 1, 0, 710, 0.8f, 0, 45, 280, 1.35f}, {7, 0.11f, 1, 0, 720, 0.85f, 0, 50, 320, 1.3f},
     {8, 0.10f, 2, 0, 730, 0.85f, 0, 55, 360, 1.25f}, {10, 0.09f, 2, 0, 740, 0.9f, 1, 60, 400, 1.2f}},
    // Sniper: an instant beam through every zombie in a line, at the toughest target in range.
    {{40, 1.60f, 1, 6, 1000, 0.14f, 0, 5, 30, 1.8f}, {55, 1.50f, 1, 7, 1050, 0.14f, 0, 5, 35, 1.7f}, {72, 1.38f, 1, 8, 1100, 0.14f, 0, 6, 42, 1.6f},
     {95, 1.25f, 2, 9, 1150, 0.14f, 0, 6, 48, 1.5f}, {125, 1.10f, 2, 11, 1200, 0.14f, 0, 8, 56, 1.4f}},
    // Flamethrower: a stream of short-lived flames that pass through zombies.
    {{4, 0.07f, 1, 16, 340, 0.55f, 3, 100, 300, 2.0f}, {5, 0.07f, 1, 18, 350, 0.58f, 3, 110, 340, 1.9f}, {6, 0.065f, 2, 20, 360, 0.60f, 4, 120, 380, 1.8f},
     {7, 0.06f, 2, 22, 370, 0.64f, 4, 130, 420, 1.7f}, {9, 0.055f, 3, 25, 380, 0.68f, 5, 150, 480, 1.6f}},
    // Grenade Launcher: lobbed at the nearest zombie, exploding when the fuse ends.
    {{30, 1.60f, 1, 80, 340, 0.75f, 0, 4, 20, 1.6f}, {38, 1.50f, 1, 90, 350, 0.75f, 0, 4, 24, 1.5f}, {48, 1.40f, 2, 100, 360, 0.75f, 0, 5, 30, 1.45f},
     {60, 1.28f, 2, 110, 370, 0.75f, 0, 6, 36, 1.4f}, {75, 1.15f, 3, 125, 380, 0.75f, 0, 6, 42, 1.3f}},
    // Homing Rockets: seek zombies, and explode on contact.
    {{11, 1.50f, 2, 40, 330, 3.0f, 0, 6, 30, 1.6f}, {13, 1.40f, 3, 44, 350, 3.0f, 0, 6, 36, 1.5f}, {16, 1.30f, 4, 48, 370, 3.0f, 0, 8, 42, 1.45f},
     {19, 1.20f, 5, 54, 390, 3.0f, 0, 8, 48, 1.4f}, {23, 1.10f, 6, 60, 420, 3.0f, 0, 10, 54, 1.3f}},
    // Landmines: dropped behind the player, exploding when a zombie steps close.
    {{32, 1.60f, 1, 85, 0, 7.0f, 0, 3, 20, 1.2f}, {40, 1.50f, 1, 95, 0, 7.0f, 0, 3, 24, 1.2f}, {50, 1.35f, 2, 105, 0, 8.0f, 0, 4, 28, 1.1f},
     {62, 1.20f, 2, 115, 0, 8.0f, 0, 4, 32, 1.1f}, {78, 1.05f, 3, 130, 0, 9.0f, 0, 5, 36, 1.0f}},
    // Tesla Coil: lightning that chains between zombies. Uses no ammo.
    {{14, 1.30f, 3, 170, 0, 0.18f, 0, 0, 0, 0}, {17, 1.20f, 4, 180, 0, 0.18f, 0, 0, 0, 0}, {21, 1.10f, 5, 190, 0, 0.18f, 0, 0, 0, 0},
     {25, 1.00f, 6, 200, 0, 0.18f, 0, 0, 0, 0}, {30, 0.90f, 8, 220, 0, 0.18f, 0, 0, 0, 0}},
    // Combat Blades: circle the player. Cooldown is how often one zombie can be hit. Uses no ammo.
    {{9, 0.35f, 2, 95, 3.4f, 0, 0, 0, 0, 0}, {11, 0.33f, 2, 105, 3.6f, 0, 0, 0, 0, 0}, {13, 0.31f, 3, 110, 3.8f, 0, 0, 0, 0, 0},
     {16, 0.29f, 3, 120, 4.0f, 0, 0, 0, 0, 0}, {20, 0.27f, 4, 130, 4.3f, 0, 0, 0, 0, 0}},
};
// clang-format on

inline constexpr float kBulletRadius = 6.0f;
inline constexpr float kPelletRadius = 4.5f;
inline constexpr float kFlameRadius = 13.0f;
inline constexpr float kGrenadeRadius = 7.0f;
inline constexpr float kRocketRadius = 6.0f;
inline constexpr float kRocketTurnRate = 7.0f;
inline constexpr float kMineTriggerRadius = 26.0f;
inline constexpr float kBladeRadius = 15.0f;
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
inline constexpr float kRifleDryCooldownMult = 2.2f;
inline constexpr float kRifleDryDamageMult = 0.7f;

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
    {WeaponId::Rifle, PassiveId::FireRate, "Storm Rifle", 1.35f, 0.65f, 2, 1.0f, 2},
    {WeaponId::Shotgun, PassiveId::Area, "Street Sweeper", 1.4f, 0.75f, 5, 1.0f, 2},
    {WeaponId::Flamethrower, PassiveId::Damage, "Inferno", 1.6f, 0.9f, 2, 1.4f, 3},
    {WeaponId::Tesla, PassiveId::PickupRange, "Tempest", 1.5f, 0.75f, 5, 1.3f, 0},
    {WeaponId::Blades, PassiveId::Armor, "Halo Saw", 1.5f, 0.8f, 2, 1.25f, 0},
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
    /* Walker   */ {18, 70, 13, 8, 1, 1.0f},
    /* Runner   */ {7, 150, 8, 5, 1, 1.3f},
    /* Brute    */ {130, 48, 26, 18, 6, 0.25f},
    /* Spitter  */ {32, 80, 14, 6, 3, 0.9f},
    /* Bloater  */ {55, 62, 19, 10, 3, 0.7f},
    /* Crawler  */ {12, 115, 9, 5, 1, 1.2f},
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

inline constexpr float kSpawnRadius = 960.0f;     // Just outside the widest view, including portrait.
inline constexpr float kRecycleRadius = 1500.0f;  // Enemies left this far behind reappear ahead.
inline constexpr float kSeparationStrength = 60.0f;

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
    {0, 60, EnemyType::Walker, 1.0f, 2.2f, 1},
    {45, 170, EnemyType::Runner, 0.5f, 1.2f, 5},
    {60, 165, EnemyType::Walker, 2.0f, 3.5f, 1},
    {120, 170, EnemyType::Brute, 0.15f, 0.3f, 1},
    // 2:50 to 3:05 is a short breather, then the Spitters arrive.
    {185, 340, EnemyType::Spitter, 0.35f, 0.8f, 1},
    {185, 340, EnemyType::Walker, 3.5f, 6.0f, 1},
    {210, 345, EnemyType::Bloater, 0.3f, 0.9f, 1},
    {260, 345, EnemyType::Runner, 1.0f, 2.0f, 7},
    // 5:45 to 6:00 is the midpoint breather.
    {360, 480, EnemyType::Charger, 0.3f, 0.9f, 1},
    {360, 480, EnemyType::Brute, 0.3f, 0.7f, 1},
    {360, 480, EnemyType::Walker, 6.0f, 10.0f, 1},
    {400, 480, EnemyType::Spitter, 0.8f, 1.4f, 1},
    // 8:00 to 8:10 is the last breather before the final push.
    {490, 600, EnemyType::Walker, 10.0f, 18.0f, 1},
    {490, 600, EnemyType::Runner, 2.0f, 3.5f, 8},
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
inline constexpr int kGemMergeStartCount = 500;  // Above this, nearby gems merge into one.
inline constexpr float kGemMergeCellSize = 96.0f;
inline constexpr int kKillsPerPickupDrop = 70;   // A pickup drops about this often.
inline constexpr float kHealPickupFraction = 0.25f;
inline constexpr float kAmmoCrateFraction = 0.35f;  // Refills this much of each gun's reserve.
inline constexpr int kKillsPerAmmoCrate = 45;      // An ammo crate drops about this often.
inline constexpr float kBombDamage = 1200.0f;
inline constexpr float kBombRadius = 760.0f;

}  // namespace ls::balance
