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
// run.seed, each player's pos and run.crushed, which are all listed and hashed, so it can't
// change a later step on its own. Unit tests check that it always equals a fresh computation.
//
// Co-op fields are only hashed in a co-op run, so that a single-player run keeps the hashes it
// had before co-op. makeBusyState is a two-player run for that reason, and a test checks that
// a single-player run's hash really doesn't see them.

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
    static const StructInfo info{"Enemy", sizeof(Enemy), 1,
        {LS_FLT(Enemy, pos), LS_DRAW(Enemy, prevPos), LS_FLT(Enemy, knock), LS_FLT(Enemy, dir), LS_FLT(Enemy, stuckFrom), LS_FLT(Enemy, hp),
         LS_FLT(Enemy, maxHp), LS_FLT(Enemy, radius), LS_FLT(Enemy, speed), LS_FLT(Enemy, contactDamage),
         LS_FLT(Enemy, timer), LS_DRAW(Enemy, flash), LS_FLT(Enemy, bladeCooldown), LS_NUM(Enemy, id), LS_NUM(Enemy, stuck), LS_NUM(Enemy, stuckQueued),
         LS_NUM(Enemy, type), LS_NUM(Enemy, state), LS_NUM(Enemy, elite), LS_NUM(Enemy, target)}, &untouchedBytes<Enemy>};
    return info;
}

inline const StructInfo& projectileInfo() {
    static const StructInfo info{"Projectile", sizeof(Projectile), 2,
        {LS_FLT(Projectile, pos), LS_DRAW(Projectile, prevPos), LS_FLT(Projectile, vel), LS_FLT(Projectile, damage),
         LS_FLT(Projectile, radius), LS_FLT(Projectile, life), LS_FLT(Projectile, area), LS_NUM(Projectile, lastHitId),
         LS_NUM(Projectile, pierce), LS_NUM(Projectile, kind), LS_NUM(Projectile, source), LS_NUM(Projectile, evolved),
         LS_NUM(Projectile, owner)}, &untouchedBytes<Projectile>};
    return info;
}

inline const StructInfo& enemyBulletInfo() {
    static const StructInfo info{"EnemyBullet", sizeof(EnemyBullet), 0,
        {LS_FLT(EnemyBullet, pos), LS_DRAW(EnemyBullet, prevPos), LS_FLT(EnemyBullet, vel), LS_FLT(EnemyBullet, damage),
         LS_FLT(EnemyBullet, radius), LS_FLT(EnemyBullet, life)}, &untouchedBytes<EnemyBullet>};
    return info;
}

inline const StructInfo& zoneInfo() {
    static const StructInfo info{"Zone", sizeof(Zone), 2,
        {LS_FLT(Zone, a), LS_FLT(Zone, b), LS_FLT(Zone, radius), LS_FLT(Zone, prevRadius), LS_FLT(Zone, maxRadius),
         LS_FLT(Zone, life), LS_FLT(Zone, maxLife), LS_FLT(Zone, damage), LS_NUM(Zone, kind), LS_NUM(Zone, source),
         LS_NUM(Zone, evolved), LS_NUM(Zone, hostile), LS_NUM(Zone, applied), LS_NUM(Zone, owner)}, &untouchedBytes<Zone>};
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

inline const StructInfo& carIdInfo() {
    static const StructInfo info{"CarId", sizeof(maps::CarId), 0, {LS_NUM(maps::CarId, texelX), LS_NUM(maps::CarId, texelY)}, &untouchedBytes<maps::CarId>};
    return info;
}

inline const StructInfo& playerInfo() {
    static const StructInfo info{"Player", sizeof(Player), 2,
        {LS_FLT(Player, pos), LS_DRAW(Player, prevPos), LS_FLT(Player, facing), LS_FLT(Player, aim), LS_FLT(Player, hp),
         LS_FLT(Player, maxHp), LS_FLT(Player, invulnerable), LS_DRAW(Player, hitFlash), LS_FLT(Player, dashDir),
         LS_FLT(Player, dashLeft), LS_FLT(Player, dashCooldown), LS_FLT(Player, dashInvulnerable), LS_NUM(Player, level),
         LS_NUM(Player, xp), LS_NUM(Player, xpToNext), LS_NEST(Player, stats), LS_NEST(Player, weapons),
         LS_COUNT(Player, weaponCount), LS_NEST(Player, passives), LS_COUNT(Player, passiveCount), LS_NUM(Player, rerollsLeft),
         LS_NUM(Player, pendingLevels), LS_NEST(Player, offers), LS_NUM(Player, offerCount), LS_FLT(Player, bladeAngle),
         LS_FLT(Player, damageByWeapon), LS_FLT(Player, reviveProgress), LS_FLT(Player, cardShield), LS_NUM(Player, downed),
         LS_NUM(Player, cardsOpen)}, &untouchedBytes<Player>};
    return info;
}

inline const StructInfo& runStateInfo() {
    static const StructInfo info{"RunState", sizeof(RunState), 8,
        {LS_NUM(RunState, phase), LS_NUM(RunState, difficulty), LS_NUM(RunState, mode), LS_NUM(RunState, map), LS_NUM(RunState, seed), LS_NUM(RunState, tick),
         LS_NUM(RunState, nextId), LS_COUNT(RunState, playerCount), LS_NUM(RunState, kills), LS_FLT(RunState, damageByWeapon),
         LS_FLT(RunState, waveAccumulators), LS_NUM(RunState, nextEliteEvent), LS_NUM(RunState, nextPickupDropKills),
         LS_NUM(RunState, bossSpawned), LS_NUM(RunState, bossPhase),
         LS_NUM(RunState, bossStep), LS_NUM(RunState, bossRepeat),
         LS_FLT(RunState, bossHpFraction), LS_NUM(RunState, bossesKilled), LS_NUM(RunState, lastBossKillTick),
         LS_NUM(RunState, debugEnemyTarget), LS_NUM(RunState, debugProjectileTarget),
         LS_NEST(RunState, crushed), LS_COUNT(RunState, crushedCount)}, &untouchedBytes<RunState>};
    return info;
}

inline std::vector<const StructInfo*> allStructInfos() {
    return {&rngInfo(),        &enemyInfo(),      &projectileInfo(), &enemyBulletInfo(), &zoneInfo(),   &gemInfo(),
            &pickupInfo(),     &weaponSlotInfo(), &passiveSlotInfo(), &playerStatsInfo(), &cardInfo(),  &playerInfo(),
            &runStateInfo(),   &carIdInfo()};
}

// Names a visited struct for a failure message, such as enemies[3] or player.
inline std::string labelOf(const char* name, int index) {
    if (index < 0) return name;
    char text[64];
    std::snprintf(text, sizeof(text), "%s[%d]", name, index);
    return text;
}

// Calls fn(info, firstByte, name, index) once for every live struct in the state: the run,
// the generator, each player in the run with their stats, held weapons and passives and
// offered cards, and the items of every pool. The index is -1 for a struct that is not in an
// array, and for a player's parts it is the player's slot. Use
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
    for (int i = 0; i < s.run.crushedCount; ++i) fn(carIdInfo(), bytesOf(s.run.crushed[i]), "run.crushed", i);
    // The counts are read once, before fn can change them, so a flipped count never changes
    // which structs are visited.
    // A player's weapons, passives and cards are numbered slot * 10 + index, so a message
    // says whose they are: players.weapons[12] is the third weapon of the player in slot 1.
    const int playerCount = s.run.playerCount;
    for (int p = 0; p < playerCount; ++p) {
        auto& player = s.players[p];
        const int weaponCount = player.weaponCount;
        const int passiveCount = player.passiveCount;
        fn(playerInfo(), bytesOf(player), "players", p);
        fn(playerStatsInfo(), bytesOf(player.stats), "players.stats", p);
        for (int i = 0; i < weaponCount; ++i) fn(weaponSlotInfo(), bytesOf(player.weapons[i]), "players.weapons", p * 10 + i);
        for (int i = 0; i < passiveCount; ++i) fn(passiveSlotInfo(), bytesOf(player.passives[i]), "players.passives", p * 10 + i);
        for (int i = 0; i < kOffersPerLevel; ++i) fn(cardInfo(), bytesOf(player.offers[i]), "players.offers", p * 10 + i);
    }

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

// A run 20 seconds in, with at least one item of every kind in every pool, and for each
// player two weapons and one passive. Each field then holds a real value, so a change to it
// shows. With players above 1 it is a co-op run, whose hash covers the co-op fields too.
inline std::unique_ptr<GameState> makeBusyState(uint64_t seed = 31, int players = 2) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, maps::kDefaultMap, RunMode::Story, players);
    for (int i = 0; i < 60 * 20; ++i) {
        InputFrame inputs[kMaxPlayers];
        for (int p = 0; p < players; ++p) {
            const float angle = static_cast<float>(state->run.tick) * 0.01f + static_cast<float>(p);
            inputs[p].moveX = std::cos(angle);
            inputs[p].moveY = std::sin(angle);
            if (state->run.phase == RunPhase::LevelUp || state->players[p].cardsOpen) inputs[p].chooseCard = 0;
        }
        sim::step(*state, inputs, players);
        state->events.clear();
    }
    // Fixed counts, so a flipped count bit can never make the hash read past an array.
    for (int p = 0; p < players; ++p) {
        Player& player = state->players[p];
        player.weaponCount = 1;
        weapons::grant(*state, p, WeaponId::Shotgun);
        player.passives[0] = PassiveSlot{PassiveId::Damage, 1};
        player.passiveCount = 1;
        player.offerCount = 2;
        player.damageByWeapon[1] = 5.0f;
        player.reviveProgress = 0.5f;
        player.cardShield = 1.0f;
    }
    state->run.crushed[0] = maps::CarId{1234, 567};
    state->run.crushedCount = 1;
    // Endless, so its fields are hashed too and every flip of them shows. A Story run leaves
    // them out of the hash (see sim::stateHash), and the mode byte itself still shows here.
    state->run.mode = RunMode::Endless;
    state->run.bossesKilled = 2;
    state->run.lastBossKillTick = 777;
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
