#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "StateFields.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace ls;

namespace {

constexpr int kStepLimit = 20 * 60 * 60;  // 20 minutes of game time.

const char* const kWeaponNames[static_cast<int>(WeaponId::Count)] = {
    "Rifle", "Shotgun", "Smg", "Sniper", "Flamethrower", "GrenadeLauncher", "Rockets", "Landmines", "Tesla", "Blades",
};

// How close a zombie's edge may get before the bot stops looting and runs.
constexpr float kSafeEdgeDistance = 200.0f;
constexpr float kLookRadius = 460.0f;
constexpr float kLootRadius = 520.0f;

Vec2 closestOnSegment(Vec2 a, Vec2 b, Vec2 point) {
    const Vec2 ab = b - a;
    const float lenSq = lengthSq(ab);
    if (lenSq < 1e-6f) return a;
    const float t = clampf(dot(point - a, ab) / lenSq, 0.0f, 1.0f);
    return a + ab * t;
}

// A simple bot: runs from the nearest zombies, bullets and boss attacks, goes
// for pickups and gems when nothing is close, and always takes card 0.
InputFrame botInput(const GameState& state) {
    InputFrame input;
    if (state.run.phase == RunPhase::LevelUp) {
        input.chooseCard = 0;
        return input;
    }
    const Vec2 pos = state.player.pos;
    Vec2 flee{};
    float nearestEdge = 1.0e9f;

    for (const Enemy& enemy : state.enemies) {
        const Vec2 away = pos - enemy.pos;
        const float distSq = lengthSq(away);
        if (distSq > kLookRadius * kLookRadius) continue;
        const float dist = std::sqrt(distSq);
        const float edge = std::max(dist - enemy.radius, 1.0f);
        nearestEdge = std::min(nearestEdge, edge);
        flee += normalizedOr(away, Vec2{1.0f, 0.0f}) * (1.0f / std::max(edge, 20.0f));
    }
    for (const EnemyBullet& bullet : state.enemyBullets) {
        const Vec2 away = pos - bullet.pos;
        if (lengthSq(away) > 140.0f * 140.0f) continue;
        nearestEdge = std::min(nearestEdge, 1.0f);
        flee += normalizedOr(away, Vec2{1.0f, 0.0f}) * 0.05f;
    }
    // Boss attack zones: step out of the circle or off the line before they hit.
    for (const Zone& zone : state.zones) {
        if (!zone.hostile) continue;
        Vec2 away{};
        float margin = 0.0f;
        if (zone.kind == ZoneKind::BossCircle) {
            away = pos - zone.a;
            margin = length(away) - zone.radius;
        } else if (zone.kind == ZoneKind::BossLine) {
            away = pos - closestOnSegment(zone.a, zone.b, pos);
            margin = length(away) - zone.radius;
            // Standing right on the line has no clear side, so go sideways to it.
            if (lengthSq(away) < 1e-4f) away = Vec2{-(zone.b.y - zone.a.y), zone.b.x - zone.a.x};
        }
        if (margin < 80.0f) {
            nearestEdge = 1.0f;
            flee += normalizedOr(away, Vec2{1.0f, 0.0f}) * 0.2f;
        }
    }

    Vec2 move{};
    if (nearestEdge < kSafeEdgeDistance) {
        // Runs away, with a sideways part so it circles rather than backing into a corner.
        const Vec2 away = normalizedOr(flee, Vec2{1.0f, 0.0f});
        move = away + Vec2{-away.y, away.x} * 0.35f;
    } else {
        // Safe: heads for the nearest pickup, or else the nearest gem.
        float bestSq = kLootRadius * kLootRadius;
        Vec2 target = pos;
        bool found = false;
        for (const Pickup& pickup : state.pickups) {
            const float distSq = distanceSq(pickup.pos, pos);
            if (distSq < bestSq * 4.0f) {
                bestSq = std::min(bestSq, distSq);
                target = pickup.pos;
                found = true;
            }
        }
        if (!found) {
            for (const Gem& gem : state.gems) {
                const float distSq = distanceSq(gem.pos, pos);
                if (distSq < bestSq) {
                    bestSq = distSq;
                    target = gem.pos;
                    found = true;
                }
            }
        }
        if (found) move = normalizedOr(target - pos, Vec2{}) + normalizedOr(flee, Vec2{}) * 0.4f;
        else move = normalizedOr(flee, Vec2{});
    }
    input.moveX = move.x;
    input.moveY = move.y;
    return input;
}

struct BotResult {
    RunPhase outcome = RunPhase::Playing;
    float endSeconds = 0.0f;
    int level = 0;
    int kills = 0;
    int peakZombies = 0;
    bool bossSpawned = false;
    float bossSpawnSeconds = 0.0f;
    bool bossKilled = false;
    float bossKillSeconds = 0.0f;
    int bossPhaseReached = 0;
    float damage[static_cast<int>(WeaponId::Count)] = {};
    bool invariantsHeld = true;
    std::vector<uint64_t> trace;  // The state hash every 5 seconds of game time.
    uint64_t finalHash = 0;
};

// Says what is wrong with the state, or returns an empty string when all is well. A NaN
// position would not crash anything and would not trip a count limit, but it would quietly
// break collisions. So every real number in every struct is checked to be finite, using the
// field lists in StateFields.h, and then the counts and ranges that must hold.
std::string stateProblem(const GameState& s) {
    std::string problem;
    testing::visitStructs(s, true, [&](const testing::StructInfo& info, const unsigned char* base, const char* name, int index) {
        if (!problem.empty()) return;
        for (const size_t offset : info.floatOffsets()) {
            float value = 0.0f;
            std::memcpy(&value, base + offset, sizeof(float));
            if (std::isfinite(value)) continue;
            // Only now, on a failure, work out which field it was, for the message.
            std::string which = "a float";
            for (const testing::Field& field : info.fields) {
                if (field.floating && offset >= field.offset && offset < field.offset + field.size) which = field.name;
            }
            problem = testing::labelOf(name, index) + "." + which + " is not a finite number";
            return;
        }
    });
    if (!problem.empty()) return problem;

    const Player& p = s.player;
    if (p.hp > p.maxHp + 1e-3f) return "player hp is above max hp";
    if (p.level < 1 || p.xp < 0) return "player level or xp is out of range";
    if (p.weaponCount < 1 || p.weaponCount > kMaxHeldWeapons) return "weapon count is out of range";
    if (p.passiveCount < 0 || p.passiveCount > kMaxHeldPassives) return "passive count is out of range";
    if (s.run.kills < 0 || s.run.rerollsLeft < 0 || s.run.pendingLevels < 0) return "a run counter is negative";
    for (int i = 0; i < p.weaponCount; ++i) {
        const WeaponSlot& w = p.weapons[i];
        if (w.magazine < 0 || w.reserve < 0) return "a weapon has negative ammo";
        if (static_cast<int>(w.id) >= static_cast<int>(WeaponId::Count)) return "a held weapon has a bad id";
    }
    for (const Enemy& e : s.enemies) {
        if (e.radius <= 0.0f || e.id == 0 || e.id >= s.run.nextId) return "an enemy has a bad radius or id";
        if (static_cast<int>(e.type) >= static_cast<int>(EnemyType::Count)) return "an enemy has a bad type";
    }
    for (const Gem& g : s.gems) {
        if (g.xp < 1) return "a gem is worth no xp";
    }
    for (const Pickup& pickup : s.pickups) {
        if (static_cast<int>(pickup.type) >= static_cast<int>(PickupType::Count)) return "a pickup has a bad type";
    }
    return "";
}

bool stateIsSane(const GameState& s) { return stateProblem(s).empty(); }

// Plays one run to its end, or to the step limit. The state is checked after every step.
// stopAfterBoss, when above 0, ends the run that many seconds after the boss appears.
BotResult runBot(uint64_t seed, bool invulnerable, float stopAfterBoss = 0.0f, maps::MapId map = maps::kDefaultMap) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, map);
    BotResult result;
    uint32_t lastTraceTick = 0;

    while (state->run.phase != RunPhase::Won && state->run.phase != RunPhase::Dead &&
           static_cast<int>(state->run.tick) < kStepLimit &&
           !(stopAfterBoss > 0.0f && result.bossSpawned && state->timeSeconds() > result.bossSpawnSeconds + stopAfterBoss)) {
        if (invulnerable) state->player.hp = state->player.maxHp;
        sim::step(*state, botInput(*state));
        if (invulnerable && state->run.phase == RunPhase::Dead) result.invariantsHeld = false;  // It must not be possible.

        for (const SimEvent& event : state->events) {
            if (event.type == EventType::BossSpawned && !result.bossSpawned) {
                result.bossSpawned = true;
                result.bossSpawnSeconds = state->timeSeconds();
            }
            if (event.type == EventType::BossKilled) {
                result.bossKilled = true;
                result.bossKillSeconds = state->timeSeconds();
            }
        }
        state->events.clear();

        if (result.invariantsHeld && !stateIsSane(*state)) {
            result.invariantsHeld = false;
            std::printf("[bot] seed %llu: state became invalid at tick %u\n", static_cast<unsigned long long>(seed), state->run.tick);
        }
        result.peakZombies = std::max(result.peakZombies, state->enemies.count);
        // One fingerprint per 5 seconds. The tick stands still during level-up, so a tick is recorded once.
        if (state->run.tick % 300 == 0 && state->run.tick != lastTraceTick) {
            lastTraceTick = state->run.tick;
            result.trace.push_back(sim::stateHash(*state));
        }
    }

    result.finalHash = sim::stateHash(*state);
    result.outcome = state->run.phase;
    result.endSeconds = state->timeSeconds();
    result.level = state->player.level;
    result.kills = state->run.kills;
    result.bossPhaseReached = state->run.bossPhase;
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) result.damage[i] = state->run.damageByWeapon[i];
    return result;
}

void printResult(const char* label, uint64_t seed, const BotResult& result) {
    const char* outcome = result.outcome == RunPhase::Won ? "WON" : (result.outcome == RunPhase::Dead ? "DEAD" : "UNFINISHED");
    std::printf("[bot %s] seed %llu: %s time=%.1fs level=%d kills=%d peakZombies=%d damage:", label,
                static_cast<unsigned long long>(seed), outcome, static_cast<double>(result.endSeconds), result.level,
                result.kills, result.peakZombies);
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) {
        if (result.damage[i] > 0.0f) std::printf(" %s=%.0f", kWeaponNames[i], static_cast<double>(result.damage[i]));
    }
    std::printf("\n");
}

// Damage one weapon deals in an 8 second standing fight against a crowd of 40 walkers.
float damageInCrowdFight(WeaponId id) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 5);
    state->player.weaponCount = 0;
    state->player.xpToNext = 1 << 30;  // No level-up screen, which would pause the fight.
    weapons::grant(*state, id);
    for (int n = 0; n < 40; ++n) {
        const float angle = static_cast<float>(n) * 0.7f;
        const Vec2 pos = fromAngle(angle) * (150.0f + static_cast<float>(n % 5) * 40.0f);
        if (!enemies::spawn(*state, EnemyType::Walker, pos, false)) break;
    }
    for (int step = 0; step < 60 * 8; ++step) {
        state->player.hp = state->player.maxHp;
        sim::step(*state, InputFrame{});
        state->events.clear();
    }
    return state->run.damageByWeapon[static_cast<int>(id)];
}

}  // namespace

TEST_CASE("bot run: each seed ends in a win or a death, and the state stays valid every step") {
    const uint64_t seeds[] = {101, 202, 303};
    for (const uint64_t seed : seeds) {
        const BotResult result = runBot(seed, false);
        printResult("normal", seed, result);
        CHECK(result.invariantsHeld);
        CHECK((result.outcome == RunPhase::Won || result.outcome == RunPhase::Dead));
        CHECK(result.endSeconds > 0.0f);
        CHECK(result.kills > 0);
        CHECK(result.peakZombies > 0);
        // Dying before 10:00 means the boss never came. Winning means it was killed.
        if (result.outcome == RunPhase::Won) CHECK(result.bossKilled);
        if (result.outcome == RunPhase::Dead && result.endSeconds < balance::kRunSeconds) CHECK_FALSE(result.bossSpawned);
    }
}

TEST_CASE("bot run: the desert's cars keep every step valid too") {
    const BotResult result = runBot(404, false, 0.0f, maps::MapId::Wasteland);
    printResult("wasteland", 404, result);
    CHECK(result.invariantsHeld);
    CHECK((result.outcome == RunPhase::Won || result.outcome == RunPhase::Dead));
    CHECK(result.kills > 0);
    const BotResult invulnerable = runBot(404, true, 0.0f, maps::MapId::Wasteland);
    printResult("wasteland invulnerable", 404, invulnerable);
    CHECK(invulnerable.invariantsHeld);
    CHECK(invulnerable.bossKilled);
}

TEST_CASE("the state check catches a broken number") {
    // Proves stateIsSane can fail: a NaN position and a negative magazine are each refused.
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 1);
    REQUIRE(stateIsSane(*state));
    enemies::spawn(*state, EnemyType::Walker, Vec2{200.0f, 0.0f}, false);
    REQUIRE(stateIsSane(*state));
    state->enemies[0].pos.x = std::nanf("");
    CHECK_FALSE(stateIsSane(*state));
    state->enemies[0].pos.x = 200.0f;
    state->player.weapons[0].magazine = -1;
    CHECK_FALSE(stateIsSane(*state));
}

TEST_CASE("the state check refuses NaN and infinity in every real number of the state") {
    // Writes a NaN, then an infinity, into each float of each struct in turn, and requires
    // the check to fail. This is what shows that no field is left out of it.
    auto state = testing::makeBusyState();
    REQUIRE(stateIsSane(*state));
    int tried = 0;
    std::vector<std::string> missed;
    testing::visitStructs(*state, false, [&](const testing::StructInfo& info, unsigned char* base, const char* name, int index) {
        const std::string label = testing::labelOf(name, index);
        for (const testing::Field& field : info.fields) {
            if (!field.floating) continue;
            for (size_t at = 0; at + sizeof(float) <= field.size; at += sizeof(float)) {
                unsigned char* where = base + field.offset + at;
                float saved = 0.0f;
                std::memcpy(&saved, where, sizeof(float));
                for (const float bad : {std::nanf(""), HUGE_VALF, -HUGE_VALF}) {
                    std::memcpy(where, &bad, sizeof(float));
                    ++tried;
                    if (stateIsSane(*state)) missed.push_back(label + "." + field.name + " accepts " + std::to_string(bad));
                }
                std::memcpy(where, &saved, sizeof(float));
            }
        }
    });
    CHECK(tried > 200);
    std::string list;
    for (const std::string& line : missed) list += "\n  " + line;
    CHECK_MESSAGE(missed.empty(), "the state check lets these through:" << list);
    CHECK(stateIsSane(*state));  // Every write was undone.
}

// Compares two runs of one seed on everything the bot records, including a fingerprint of
// the whole state every 5 seconds, so a difference that later cancels out still shows.
static void checkSameRun(const BotResult& first, const BotResult& second) {
    CHECK(first.outcome == second.outcome);
    CHECK(first.endSeconds == second.endSeconds);
    CHECK(first.level == second.level);
    CHECK(first.kills == second.kills);
    CHECK(first.peakZombies == second.peakZombies);
    CHECK(first.bossSpawned == second.bossSpawned);
    CHECK(first.bossKilled == second.bossKilled);
    CHECK(first.bossPhaseReached == second.bossPhaseReached);
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) CHECK(first.damage[i] == second.damage[i]);
    CHECK(first.trace.size() == second.trace.size());
    CHECK(first.trace == second.trace);
    CHECK(first.finalHash == second.finalHash);
}

TEST_CASE("bot run: the same seed gives the same result") {
    const BotResult first = runBot(404, false);
    const BotResult second = runBot(404, false);
    checkSameRun(first, second);
    CHECK(first.trace.size() > 2);
}

TEST_CASE("bot run: the same seed gives the same state through the late game and the boss fight") {
    // The invulnerable bot cannot die, so this always reaches 10:00 and the boss, plus a
    // minute of the fight. Spitters, Bloaters, Chargers and the boss all run in it.
    const BotResult first = runBot(404, true, 60.0f);
    const BotResult second = runBot(404, true, 60.0f);
    REQUIRE(first.bossSpawned);
    CHECK(first.endSeconds > balance::kRunSeconds + 50.0f);
    CHECK(first.trace.size() > static_cast<size_t>(balance::kRunSeconds / 5.0f));
    checkSameRun(first, second);
}

TEST_CASE("every weapon deals damage to a crowd, which the boss fight below relies on") {
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) {
        const float dealt = damageInCrowdFight(static_cast<WeaponId>(i));
        // The name is part of the message, so a failure says which weapon it was.
        const std::string what = std::string("weapon ") + kWeaponNames[i] + " dealt " + std::to_string(dealt) + " damage in 8 seconds against 40 walkers";
        CHECK_MESSAGE(dealt > 0.0f, what);
    }
}

TEST_CASE("invulnerable bot reaches the boss at 10:00, and kills it in time") {
    const uint64_t seeds[] = {101, 202, 303};
    for (const uint64_t seed : seeds) {
        const BotResult result = runBot(seed, true);
        printResult("invulnerable", seed, result);
        CHECK(result.invariantsHeld);
        CHECK(result.bossSpawned);
        CHECK(result.bossSpawnSeconds == doctest::Approx(balance::kRunSeconds).epsilon(0.001));
        // The run has 20 minutes of game time in total, so the boss gets 10 minutes. A
        // boss that is not killed fails here, whatever the cause.
        CHECK(result.bossKilled);
        if (result.bossKilled) {
            std::printf("[bot invulnerable] seed %llu: boss killed %.1fs after it spawned\n",
                        static_cast<unsigned long long>(seed),
                        static_cast<double>(result.bossKillSeconds - result.bossSpawnSeconds));
            CHECK(result.outcome == RunPhase::Won);
        } else {
            std::printf("[bot invulnerable] seed %llu: boss NOT killed, reached boss phase %d\n",
                        static_cast<unsigned long long>(seed), result.bossPhaseReached);
        }
    }
}
