#pragma once

#include "core/Math.h"
#include "core/Pool.h"
#include "core/Rng.h"
#include "core/SpatialHash.h"

#include <cstdint>

namespace ls {

// All simulation state lives in this one struct. It holds plain data only, so a
// run can be reset, hashed for the determinism test, and read by the renderer.
// The simulation never calls raylib and never reads the clock.

inline constexpr float kDt = 1.0f / 60.0f;  // Fixed simulation step.

inline constexpr int kMaxEnemies = 2400;
inline constexpr int kMaxProjectiles = 3200;
inline constexpr int kMaxEnemyBullets = 800;
inline constexpr int kMaxZones = 256;
inline constexpr int kMaxGems = 800;
inline constexpr int kMaxPickups = 32;
inline constexpr int kMaxEvents = 512;
inline constexpr int kMaxHeldWeapons = 6;
inline constexpr int kMaxHeldPassives = 6;
inline constexpr int kMaxLevelPerItem = 5;
inline constexpr int kOffersPerLevel = 3;

enum class EnemyType : uint8_t { Walker, Runner, Brute, Spitter, Bloater, Crawler, Charger, Boss, Count };
enum class WeaponId : uint8_t { Rifle, Shotgun, Smg, Sniper, Flamethrower, GrenadeLauncher, Rockets, Landmines, Tesla, Blades, Count };
enum class PassiveId : uint8_t { Damage, FireRate, Area, MoveSpeed, PickupRange, Armor, Count };
enum class PickupType : uint8_t { Heal, Magnet, Bomb, Ammo, Count };

// Player-owned moving objects. Beams, blasts and lightning are Zones instead.
enum class ProjectileKind : uint8_t { Bullet, Flame, Grenade, Rocket, Mine };

// Short-lived area effects that have a shape and sometimes deal damage.
// Hostile zones are boss attacks: they show a telegraph, then hit at the end.
enum class ZoneKind : uint8_t { Explosion, SniperBeam, TeslaLink, BossCircle, BossLine };

struct Enemy {
    Vec2 pos;
    Vec2 prevPos;       // Position at the previous step, for render interpolation.
    Vec2 knock;         // Knockback velocity, decays each step.
    Vec2 dir;           // Locked direction, for dashes and attacks.
    float hp = 0.0f;
    float maxHp = 0.0f;
    float radius = 0.0f;
    float speed = 0.0f;
    float contactDamage = 0.0f;
    float timer = 0.0f;  // Meaning depends on the enemy type's behaviour.
    float flash = 0.0f;  // Seconds of white hit flash left. Only the renderer reads it.
    float bladeCooldown = 0.0f;  // Stops one Combat Blade hitting every step.
    uint32_t id = 0;     // Stays the same when the pool moves this item.
    EnemyType type = EnemyType::Walker;
    uint8_t state = 0;   // Meaning depends on the enemy type's behaviour.
    bool elite = false;
};

struct Projectile {
    Vec2 pos;
    Vec2 prevPos;
    Vec2 vel;
    float damage = 0.0f;
    float radius = 0.0f;
    float life = 0.0f;
    float area = 0.0f;       // Blast radius, for mines and missiles.
    uint32_t lastHitId = 0;  // Stops a piercing shot hitting one enemy twice in a row.
    int16_t pierce = 0;      // Enemies it can pass through before it is used up.
    ProjectileKind kind = ProjectileKind::Bullet;
    WeaponId source = WeaponId::Rifle;
    bool evolved = false;
};

struct EnemyBullet {
    Vec2 pos;
    Vec2 prevPos;
    Vec2 vel;
    float damage = 0.0f;
    float radius = 0.0f;
    float life = 0.0f;
};

struct Zone {
    Vec2 a;                   // Center, or the beam start.
    Vec2 b;                   // Beam end. Unused for circles.
    float radius = 0.0f;      // Current radius, or beam half width.
    float prevRadius = 0.0f;  // Rings hit enemies between prevRadius and radius.
    float maxRadius = 0.0f;
    float life = 0.0f;
    float maxLife = 0.0f;
    float damage = 0.0f;
    ZoneKind kind = ZoneKind::Explosion;
    WeaponId source = WeaponId::Rifle;
    bool evolved = false;
    bool hostile = false;
    bool applied = false;     // True once single-hit zones have dealt damage.
};

struct Gem {
    Vec2 pos;
    Vec2 prevPos;
    int xp = 1;
    bool attracted = false;   // Once true, it flies to the player until collected.
};

struct Pickup {
    Vec2 pos;
    PickupType type = PickupType::Heal;
};

struct WeaponSlot {
    WeaponId id = WeaponId::Rifle;
    uint8_t level = 1;
    bool evolved = false;
    float cooldown = 0.0f;
    int magazine = 0;         // Rounds loaded. Weapons with no ammo keep this at 0.
    int reserve = 0;          // Rounds carried, not loaded.
    float reloadLeft = 0.0f;  // Above 0 means reloading. The renderer also reads this.
    bool dryReported = false;  // True once this weapon's WeaponDry event has been sent.
};

struct PassiveSlot {
    PassiveId id = PassiveId::Damage;
    uint8_t level = 1;
};

// Player stats after passives are applied. Recomputed when the build changes.
struct PlayerStats {
    float damageMult = 1.0f;
    float cooldownMult = 1.0f;
    float areaMult = 1.0f;
    float moveSpeed = 0.0f;
    float pickupRange = 0.0f;
    float armor = 0.0f;
};

struct Player {
    Vec2 pos;
    Vec2 prevPos;
    Vec2 facing{1.0f, 0.0f};
    Vec2 aim{1.0f, 0.0f};  // Direction of the last shot. The gun is drawn pointing this way.
    float hp = 0.0f;
    float maxHp = 0.0f;
    float invulnerable = 0.0f;
    float hitFlash = 0.0f;
    // Dash. The direction is locked when it starts, so steering can't bend it.
    Vec2 dashDir;
    float dashLeft = 0.0f;          // Seconds of dash still to run. Above 0 means dashing.
    float dashCooldown = 0.0f;      // Seconds until the next dash is allowed.
    float dashInvulnerable = 0.0f;  // Kept apart from the post-hit window, so neither can stretch the other.
    int level = 1;
    int xp = 0;
    int xpToNext = 0;
    PlayerStats stats;
    WeaponSlot weapons[kMaxHeldWeapons];
    int weaponCount = 0;
    PassiveSlot passives[kMaxHeldPassives];
    int passiveCount = 0;
};

enum class CardKind : uint8_t { Weapon, Passive, Evolution, Heal };

// One level-up choice.
struct Card {
    CardKind kind = CardKind::Heal;
    WeaponId weapon = WeaponId::Rifle;    // For Weapon and Evolution cards.
    PassiveId passive = PassiveId::Damage;  // For Passive cards.
    uint8_t newLevel = 1;
};

enum class RunPhase : uint8_t { Playing, LevelUp, Won, Dead };
enum class Difficulty : uint8_t { Easy, Normal, Hard, Count };

struct RunState {
    RunPhase phase = RunPhase::Playing;
    Difficulty difficulty = Difficulty::Normal;
    uint64_t seed = 0;
    uint32_t tick = 0;       // Steps played. Does not advance during level-up.
    uint32_t nextId = 1;
    int kills = 0;
    int rerollsLeft = 0;
    int pendingLevels = 0;
    Card offers[kOffersPerLevel];
    int offerCount = 0;
    float damageByWeapon[static_cast<int>(WeaponId::Count)] = {};
    float waveAccumulators[32] = {};  // Fractional spawns owed, per wave table row.
    int nextEliteEvent = 0;
    int nextPickupDropKills = 0;
    float bladeAngle = 0.0f;
    bool bossSpawned = false;
    uint8_t bossPhase = 0;
    float bossHpFraction = 0.0f;  // Copied each step, for the HUD boss bar.
    int debugEnemyTarget = 0;     // Debug stress test only.
    int debugProjectileTarget = 0;
};

// Things that happened this step. The simulation only records them. Effects and
// audio read them later, which keeps the simulation free of graphics and sound.
enum class EventType : uint8_t {
    EnemyHit, EnemyKilled, EliteKilled, PlayerHit, GemCollected, LevelUp, PickupCollected,
    BombBlast, WeaponFired, ReloadStarted, WeaponDry, EvolutionGained, BossSpawned, BossPhaseChanged, BossKilled, PlayerDied,
    Explosion,  // detail is the weapon that made it. value is the blast radius.
    PlayerDashed,  // At the spot the dash started. The direction is in player.dashDir.
};

struct SimEvent {
    EventType type = EventType::EnemyHit;
    uint8_t detail = 0;  // Weapon, enemy type or pickup type, depending on the event.
    bool flag = false;   // For EnemyHit: true when the hit was on an elite.
    Vec2 pos;
    float value = 0.0f;  // Damage amount, or XP amount.
};

struct GameState {
    Rng rng;
    RunState run;
    Player player;
    Pool<Enemy, kMaxEnemies> enemies;
    Pool<Projectile, kMaxProjectiles> projectiles;
    Pool<EnemyBullet, kMaxEnemyBullets> enemyBullets;
    Pool<Zone, kMaxZones> zones;
    Pool<Gem, kMaxGems> gems;
    Pool<Pickup, kMaxPickups> pickups;
    Pool<SimEvent, kMaxEvents> events;  // Cleared by whoever reads them.
    SpatialHash<kMaxEnemies> enemyGrid;

    float timeSeconds() const { return static_cast<float>(run.tick) * kDt; }
};

// Records an event. Dropped silently if the frame's list is full, because events
// only drive presentation, so losing one never changes the simulation.
inline void pushEvent(GameState& state, EventType type, Vec2 pos, float value = 0.0f, uint8_t detail = 0, bool flag = false) {
    if (SimEvent* event = state.events.add()) {
        event->type = type;
        event->pos = pos;
        event->value = value;
        event->detail = detail;
        event->flag = flag;
    }
}

}  // namespace ls
