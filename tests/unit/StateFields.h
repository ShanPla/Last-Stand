#pragma once

// A hand-written list of every member of the simulation's state structs, with its place
// in memory. Tests use it to touch or inspect each field by name without knowing the
// field's type: the state hash tests flip one byte at a time, and the bot test checks that
// every real number is finite.
//
// When a field is added to a struct in core/GameState.h, add it here too. The padding
// number of each struct is checked against sizeof, so a forgotten field fails
// "the field lists cover every byte of the state structs". Then decide whether the new
// field belongs in sim::stateHash (almost always yes), and mark it LS_DRAW only if it is
// used for drawing and never read by a simulation step.
//
// What this cannot catch: a new field that fits into existing padding, so the struct does
// not grow. Padding is only the gap the compiler leaves for alignment.
//
// GameState::obstacleCache is not listed on purpose: it is worked out only from run.map,
// run.seed and player.pos, which are all listed and hashed, so it can't change a later step
// on its own. A unit test checks that it always equals a fresh computation.

#include "components/Enemies/Enemies.h"
#include "components/Pickups/Pickups.h"
#include "components/Weapons/Weapons.h"
#include "core/GameState.h"
#include "core/Sim.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

namespace ls::testing {

struct Field {
    const char* name;
    size_t offset;
    size_t size;
    bool floating;  // Holds floats (a Vec2 is two). These must always be finite numbers.
    bool hashed;    // True when sim::stateHash is meant to see it.
    bool nested;    // A struct or array of structs, listed by its own StructInfo instead.
    size_t flipBytes;  // How many bytes a test may change. Less than size only for a count that is a loop limit.
};

// Which bytes of a fresh T still hold what the memory held before, which are the alignment
// gaps the compiler does not write to. T is built twice, in memory filled with two different
// patterns, and the bytes that differ afterwards were not written. A field that has been
// initialised, such as a bool added into a gap, reads the same both times.
template <class T>
std::vector<bool> untouchedBytes() {
    alignas(T) unsigned char first[sizeof(T)];
    alignas(T) unsigned char second[sizeof(T)];
    std::memset(first, 0xAA, sizeof(first));
    std::memset(second, 0x55, sizeof(second));
    new (first) T{};
    new (second) T{};
    std::vector<bool> differs(sizeof(T), false);
    for (size_t i = 0; i < sizeof(T); ++i) differs[i] = first[i] != second[i];
    return differs;
}

struct StructInfo {
    const char* name;
    size_t size;     // sizeof the struct.
    size_t padding;  // Bytes the listed fields do not cover: alignment gaps only.
    std::vector<Field> fields;
    std::vector<bool> (*untouched)();  // untouchedBytes<T> for this struct.

    // The offset of every float in the struct, found once. The bot test checks every float of
    // every zombie after every step, so this has to be quick.
    const std::vector<size_t>& floatOffsets() const {
        if (!floatsFound_) {
            for (const Field& field : fields) {
                if (!field.floating) continue;
                for (size_t at = 0; at + sizeof(float) <= field.size; at += sizeof(float)) floats_.push_back(field.offset + at);
            }
            floatsFound_ = true;
        }
        return floats_;
    }

    // Filled by floatOffsets. Public only so that the struct stays a plain aggregate.
    mutable std::vector<size_t> floats_;
    mutable bool floatsFound_ = false;
};

#define LS_NUM(T, m) ::ls::testing::Field{#m, offsetof(T, m), sizeof(T::m), false, true, false, sizeof(T::m)}
#define LS_FLT(T, m) ::ls::testing::Field{#m, offsetof(T, m), sizeof(T::m), true, true, false, sizeof(T::m)}
// Drawing only: the renderer reads it and no simulation step does, so it is left out of the hash.
#define LS_DRAW(T, m) ::ls::testing::Field{#m, offsetof(T, m), sizeof(T::m), true, false, false, sizeof(T::m)}
// A count that the hash uses as a loop limit over an array. Only its lowest byte is changed,
// because flipping a high byte would make the hash read far past the end of the array.
#define LS_COUNT(T, m) ::ls::testing::Field{#m, offsetof(T, m), sizeof(T::m), false, true, false, 1}
#define LS_NEST(T, m) ::ls::testing::Field{#m, offsetof(T, m), sizeof(T::m), false, true, true, sizeof(T::m)}

inline const StructInfo& rngInfo() {
    static const StructInfo info{"Rng", sizeof(Rng), 0, {LS_NUM(Rng, state), LS_NUM(Rng, stream)}, &untouchedBytes<Rng>};
    return info;
}

inline const StructInfo& enemyInfo() {
    static const StructInfo info{"Enemy", sizeof(Enemy), 3,
        {LS_FLT(Enemy, pos), LS_DRAW(Enemy, prevPos), LS_FLT(Enemy, knock), LS_FLT(Enemy, dir), LS_FLT(Enemy, hp),
         LS_FLT(Enemy, maxHp), LS_FLT(Enemy, radius), LS_FLT(Enemy, speed), LS_FLT(Enemy, contactDamage),
         LS_FLT(Enemy, timer), LS_DRAW(Enemy, flash), LS_FLT(Enemy, bladeCooldown), LS_NUM(Enemy, id), LS_NUM(Enemy, stuck),
         LS_NUM(Enemy, type), LS_NUM(Enemy, state), LS_NUM(Enemy, elite)}, &untouchedBytes<Enemy>};
    return info;
}

inline const StructInfo& projectileInfo() {
    static const StructInfo info{"Projectile", sizeof(Projectile), 3,
        {LS_FLT(Projectile, pos), LS_DRAW(Projectile, prevPos), LS_FLT(Projectile, vel), LS_FLT(Projectile, damage),
         LS_FLT(Projectile, radius), LS_FLT(Projectile, life), LS_FLT(Projectile, area), LS_NUM(Projectile, lastHitId),
         LS_NUM(Projectile, pierce), LS_NUM(Projectile, kind), LS_NUM(Projectile, source), LS_NUM(Projectile, evolved)}, &untouchedBytes<Projectile>};
    return info;
}

inline const StructInfo& enemyBulletInfo() {
    static const StructInfo info{"EnemyBullet", sizeof(EnemyBullet), 0,
        {LS_FLT(EnemyBullet, pos), LS_DRAW(EnemyBullet, prevPos), LS_FLT(EnemyBullet, vel), LS_FLT(EnemyBullet, damage),
         LS_FLT(EnemyBullet, radius), LS_FLT(EnemyBullet, life)}, &untouchedBytes<EnemyBullet>};
    return info;
}

inline const StructInfo& zoneInfo() {
    static const StructInfo info{"Zone", sizeof(Zone), 3,
        {LS_FLT(Zone, a), LS_FLT(Zone, b), LS_FLT(Zone, radius), LS_FLT(Zone, prevRadius), LS_FLT(Zone, maxRadius),
         LS_FLT(Zone, life), LS_FLT(Zone, maxLife), LS_FLT(Zone, damage), LS_NUM(Zone, kind), LS_NUM(Zone, source),
         LS_NUM(Zone, evolved), LS_NUM(Zone, hostile), LS_NUM(Zone, applied)}, &untouchedBytes<Zone>};
    return info;
}

inline const StructInfo& gemInfo() {
    static const StructInfo info{"Gem", sizeof(Gem), 3,
        {LS_FLT(Gem, pos), LS_DRAW(Gem, prevPos), LS_NUM(Gem, xp), LS_NUM(Gem, attracted)}, &untouchedBytes<Gem>};
    return info;
}

inline const StructInfo& pickupInfo() {
    static const StructInfo info{"Pickup", sizeof(Pickup), 3, {LS_FLT(Pickup, pos), LS_NUM(Pickup, type)}, &untouchedBytes<Pickup>};
    return info;
}

inline const StructInfo& weaponSlotInfo() {
    static const StructInfo info{"WeaponSlot", sizeof(WeaponSlot), 4,
        {LS_NUM(WeaponSlot, id), LS_NUM(WeaponSlot, level), LS_NUM(WeaponSlot, evolved), LS_FLT(WeaponSlot, cooldown),
         LS_NUM(WeaponSlot, magazine), LS_NUM(WeaponSlot, reserve), LS_FLT(WeaponSlot, reloadLeft),
         LS_NUM(WeaponSlot, dryReported)}, &untouchedBytes<WeaponSlot>};
    return info;
}

inline const StructInfo& passiveSlotInfo() {
    static const StructInfo info{"PassiveSlot", sizeof(PassiveSlot), 0, {LS_NUM(PassiveSlot, id), LS_NUM(PassiveSlot, level)}, &untouchedBytes<PassiveSlot>};
    return info;
}

inline const StructInfo& playerStatsInfo() {
    static const StructInfo info{"PlayerStats", sizeof(PlayerStats), 0,
        {LS_FLT(PlayerStats, damageMult), LS_FLT(PlayerStats, cooldownMult), LS_FLT(PlayerStats, areaMult),
         LS_FLT(PlayerStats, moveSpeed), LS_FLT(PlayerStats, pickupRange), LS_FLT(PlayerStats, armor)}, &untouchedBytes<PlayerStats>};
    return info;
}

inline const StructInfo& cardInfo() {
    static const StructInfo info{"Card", sizeof(Card), 0,
        {LS_NUM(Card, kind), LS_NUM(Card, weapon), LS_NUM(Card, passive), LS_NUM(Card, newLevel)}, &untouchedBytes<Card>};
    return info;
}

inline const StructInfo& playerInfo() {
    static const StructInfo info{"Player", sizeof(Player), 0,
        {LS_FLT(Player, pos), LS_DRAW(Player, prevPos), LS_FLT(Player, facing), LS_FLT(Player, aim), LS_FLT(Player, hp),
         LS_FLT(Player, maxHp), LS_FLT(Player, invulnerable), LS_DRAW(Player, hitFlash), LS_FLT(Player, dashDir),
         LS_FLT(Player, dashLeft), LS_FLT(Player, dashCooldown), LS_FLT(Player, dashInvulnerable), LS_NUM(Player, level),
         LS_NUM(Player, xp), LS_NUM(Player, xpToNext), LS_NEST(Player, stats), LS_NEST(Player, weapons),
         LS_COUNT(Player, weaponCount), LS_NEST(Player, passives), LS_COUNT(Player, passiveCount)}, &untouchedBytes<Player>};
    return info;
}

inline const StructInfo& runStateInfo() {
    static const StructInfo info{"RunState", sizeof(RunState), 7,
        {LS_NUM(RunState, phase), LS_NUM(RunState, difficulty), LS_NUM(RunState, map), LS_NUM(RunState, seed), LS_NUM(RunState, tick),
         LS_NUM(RunState, nextId), LS_NUM(RunState, kills), LS_NUM(RunState, rerollsLeft), LS_NUM(RunState, pendingLevels),
         LS_NEST(RunState, offers), LS_NUM(RunState, offerCount), LS_FLT(RunState, damageByWeapon),
         LS_FLT(RunState, waveAccumulators), LS_NUM(RunState, nextEliteEvent), LS_NUM(RunState, nextPickupDropKills),
         LS_FLT(RunState, bladeAngle), LS_NUM(RunState, bossSpawned), LS_NUM(RunState, bossPhase),
         LS_FLT(RunState, bossHpFraction), LS_NUM(RunState, debugEnemyTarget), LS_NUM(RunState, debugProjectileTarget)}, &untouchedBytes<RunState>};
    return info;
}

inline std::vector<const StructInfo*> allStructInfos() {
    return {&rngInfo(),        &enemyInfo(),      &projectileInfo(), &enemyBulletInfo(), &zoneInfo(),   &gemInfo(),
            &pickupInfo(),     &weaponSlotInfo(), &passiveSlotInfo(), &playerStatsInfo(), &cardInfo(),  &playerInfo(),
            &runStateInfo()};
}

// Names a visited struct for a failure message, such as enemies[3] or player.
inline std::string labelOf(const char* name, int index) {
    if (index < 0) return name;
    char text[64];
    std::snprintf(text, sizeof(text), "%s[%d]", name, index);
    return text;
}

// Calls fn(info, firstByte, name, index) once for every live struct in the state: the run,
// the generator, the player and its stats, held weapons and passives, the offered cards,
// and the items of every pool. The index is -1 for a struct that is not in an array. Use
// labelOf to turn the name and index into text, only when a message is needed, because
// building a string for every zombie every step would be slow.
// With everyElement false only the first and last item of each pool are visited, which is
// enough to show that a whole array is hashed and keeps big states fast. The bytes are
// const when the state is const.
template <class State, class Fn>
void visitStructs(State& s, bool everyElement, Fn&& fn) {
    using Byte = std::conditional_t<std::is_const_v<State>, const unsigned char, unsigned char>;
    const auto bytesOf = [](auto& thing) { return reinterpret_cast<Byte*>(&thing); };

    fn(rngInfo(), bytesOf(s.rng), "rng", -1);
    fn(runStateInfo(), bytesOf(s.run), "run", -1);
    for (int i = 0; i < kOffersPerLevel; ++i) fn(cardInfo(), bytesOf(s.run.offers[i]), "run.offers", i);
    fn(playerInfo(), bytesOf(s.player), "player", -1);
    fn(playerStatsInfo(), bytesOf(s.player.stats), "player.stats", -1);
    for (int i = 0; i < s.player.weaponCount; ++i) fn(weaponSlotInfo(), bytesOf(s.player.weapons[i]), "player.weapons", i);
    for (int i = 0; i < s.player.passiveCount; ++i) fn(passiveSlotInfo(), bytesOf(s.player.passives[i]), "player.passives", i);

    const auto visitPool = [&](auto& pool, const StructInfo& info, const char* name) {
        for (int i = 0; i < pool.count; ++i) {
            if (!everyElement && i != 0 && i != pool.count - 1) continue;
            fn(info, bytesOf(pool[i]), name, i);
        }
    };
    visitPool(s.enemies, enemyInfo(), "enemies");
    visitPool(s.projectiles, projectileInfo(), "projectiles");
    visitPool(s.enemyBullets, enemyBulletInfo(), "enemyBullets");
    visitPool(s.zones, zoneInfo(), "zones");
    visitPool(s.gems, gemInfo(), "gems");
    visitPool(s.pickups, pickupInfo(), "pickups");
}

// A run 20 seconds in, with at least one item of every kind in every pool, two weapons
// and one passive. Each field then holds a real value, so a change to it shows.
inline std::unique_ptr<GameState> makeBusyState(uint64_t seed = 31) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed);
    for (int i = 0; i < 60 * 20; ++i) {
        InputFrame input;
        const float angle = static_cast<float>(state->run.tick) * 0.01f;
        input.moveX = std::cos(angle);
        input.moveY = std::sin(angle);
        if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        sim::step(*state, input);
        state->events.clear();
    }
    // Fixed counts, so a flipped count bit can never make the hash read past an array.
    state->player.weaponCount = 1;
    weapons::grant(*state, WeaponId::Shotgun);
    state->player.passives[0] = PassiveSlot{PassiveId::Damage, 1};
    state->player.passiveCount = 1;
    enemies::spawn(*state, EnemyType::Charger, Vec2{500.0f, 100.0f}, false);
    state->projectiles.add()->life = 1.0f;
    state->enemyBullets.add()->life = 1.0f;
    state->zones.add()->life = 1.0f;
    state->gems.add()->xp = 1;
    pickups::spawnPickup(*state, Vec2{300.0f, 300.0f}, PickupType::Heal);
    pushEvent(*state, EventType::EnemyHit, Vec2{});
    return state;
}

}  // namespace ls::testing
