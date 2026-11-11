#include "doctest.h"

#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Fingerprints of whole single-player runs, recorded with the game as it was just before
// co-op was added. Co-op changed how the simulation stores and steps its players, and a
// single-player run must still be exactly the run it was: same seed, same input, same map,
// difficulty and mode give the same state, step for step. The runs below take real damage,
// dash, reroll, take different cards and pick up everything, so most of the game is in them.
// If a change to single-player is meant, record new values: a failure prints them.

using namespace ls;

namespace {

// The one player of a single-player run.
Player& me(GameState& state) { return state.firstPlayer(); }
const Player& me(const GameState& state) { return state.firstPlayer(); }
int offerCount(const GameState& state) { return state.firstPlayer().offerCount; }
int rerollsLeft(const GameState& state) { return state.firstPlayer().rerollsLeft; }

// Runs from what is close, heads for loot when it is safe, dashes when something is on top
// of it, and spreads its card choices and rerolls over the run. Only reads the state, so the
// same state always gives the same input.
InputFrame goldenInput(const GameState& state) {
    InputFrame input;
    if (state.run.phase == RunPhase::LevelUp) {
        // Rerolls at level 3 and 7, then takes a card that depends on the level.
        const int level = me(state).level;
        if ((level == 3 || level == 7) && rerollsLeft(state) > 0) {
            input.reroll = true;
            return input;
        }
        input.chooseCard = static_cast<int8_t>(level % std::max(1, offerCount(state)));
        return input;
    }
    const Vec2 pos = me(state).pos;
    Vec2 flee{};
    float nearestEdge = 1.0e9f;
    for (const Enemy& enemy : state.enemies) {
        const Vec2 away = pos - enemy.pos;
        const float distSq = lengthSq(away);
        if (distSq > 450.0f * 450.0f) continue;
        const float edge = std::max(std::sqrt(distSq) - enemy.radius, 1.0f);
        nearestEdge = std::min(nearestEdge, edge);
        flee += normalizedOr(away, Vec2{1.0f, 0.0f}) * (1.0f / std::max(edge, 20.0f));
    }
    for (const EnemyBullet& bullet : state.enemyBullets) {
        const Vec2 away = pos - bullet.pos;
        if (lengthSq(away) > 140.0f * 140.0f) continue;
        nearestEdge = std::min(nearestEdge, 1.0f);
        flee += normalizedOr(away, Vec2{1.0f, 0.0f}) * 0.05f;
    }
    for (const Zone& zone : state.zones) {
        if (!zone.hostile) continue;
        const Vec2 away = pos - zone.a;
        if (length(away) - zone.radius < 80.0f) flee += normalizedOr(away, Vec2{1.0f, 0.0f}) * 0.2f;
    }
    Vec2 move{};
    if (nearestEdge < 190.0f) {
        const Vec2 away = normalizedOr(flee, Vec2{1.0f, 0.0f});
        move = away + Vec2{-away.y, away.x} * 0.35f;
    } else {
        float bestSq = 520.0f * 520.0f;
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
        move = found ? normalizedOr(target - pos, Vec2{}) + normalizedOr(flee, Vec2{}) * 0.4f : normalizedOr(flee, Vec2{});
    }
    input.moveX = move.x;
    input.moveY = move.y;
    input.dash = nearestEdge < 40.0f && state.run.tick % 7u == 0u;
    return input;
}

struct GoldenRun {
    uint64_t seed;
    Difficulty difficulty;
    maps::MapId map;
    RunMode mode;
    int topUpSeconds;    // Health is refilled after every step until then, so the run gets further.
    int minutes;         // Game time played at most.
};

// The state's fingerprint once a minute of game time, then at the end, then the outcome.
std::vector<uint64_t> playGolden(const GoldenRun& run) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, run.seed, run.difficulty, run.map, run.mode);
    std::vector<uint64_t> points;
    const uint32_t limit = static_cast<uint32_t>(run.minutes) * 3600u;
    uint32_t lastPoint = 0;
    while (state->run.phase != RunPhase::Won && state->run.phase != RunPhase::Dead && state->run.tick < limit) {
        sim::step(*state, goldenInput(*state));
        state->events.clear();
        if (state->run.tick < static_cast<uint32_t>(run.topUpSeconds) * 60u) me(*state).hp = me(*state).maxHp;
        if (state->run.tick % 3600u == 0u && state->run.tick != lastPoint) {
            lastPoint = state->run.tick;
            points.push_back(sim::stateHash(*state));
        }
    }
    points.push_back(sim::stateHash(*state));
    points.push_back(state->run.tick);
    points.push_back(static_cast<uint64_t>(state->run.phase));
    points.push_back(static_cast<uint64_t>(state->run.kills));
    points.push_back(static_cast<uint64_t>(state->run.bossesKilled));
    return points;
}

std::string listed(const std::vector<uint64_t>& values) {
    std::string text = "{";
    for (size_t i = 0; i < values.size(); ++i) {
        char item[32];
        std::snprintf(item, sizeof(item), "%s0x%llxULL", i == 0 ? "" : ", ", static_cast<unsigned long long>(values[i]));
        text += item;
    }
    return text + "}";
}

void checkGolden(const GoldenRun& run, const std::vector<uint64_t>& expected) {
    CAPTURE(run.seed);
    const std::vector<uint64_t> got = playGolden(run);
    const std::string now = "now " + listed(got);
    INFO(now);
    CHECK(got == expected);
}

}  // namespace

TEST_CASE("single-player golden: Normal city run with real damage, to its end") {
    // Dies at 4:58 with 667 kills.
    checkGolden({101, Difficulty::Normal, maps::MapId::City, RunMode::Story, 0, 20},
                {0x1ee31f82268f66bULL, 0x31bd2dc1bb879ecdULL, 0xd4d094b5ad6080c9ULL, 0xe6df3de88279caf8ULL, 0xa19882a905d5eea7ULL, 0x437cULL,
                 0x3ULL, 0x29bULL, 0x0ULL});
}

TEST_CASE("single-player golden: Hard wasteland run with real damage, to its end") {
    checkGolden({202, Difficulty::Hard, maps::MapId::Wasteland, RunMode::Story, 0, 20},
                {0x943cf72826d38faeULL, 0xcc6da33c311fa814ULL, 0x6a6095b858537a6aULL, 0x7be3f32ce2e595d5ULL, 0x6efe63bfba0c05c4ULL, 0x396cULL,
                 0x3ULL, 0x133ULL, 0x0ULL});
}

TEST_CASE("single-player golden: Hell wasteland run with real damage, to its end") {
    checkGolden({505, Difficulty::Hell, maps::MapId::Wasteland, RunMode::Story, 0, 20},
                {0xedc147d4e5075152ULL, 0xf80747dd873e7eULL, 0x3c874943de5d8211ULL, 0x1ef6ULL, 0x3ULL, 0x6eULL, 0x0ULL});
}

TEST_CASE("single-player golden: Easy city run kept alive through the boss and the win") {
    checkGolden({303, Difficulty::Easy, maps::MapId::City, RunMode::Story, 1200, 20},
                {0xec574535efa74fb5ULL, 0xbea094513b1a00ULL, 0x280aa87f70237243ULL, 0x67247eeaf4310b1bULL, 0x6163af43d311c88cULL,
                 0x360067f2718a9ac1ULL, 0xa822a8a89bb5493cULL, 0xec773b6f39a6bd14ULL, 0x3079f954900e91f7ULL, 0x74b0071fb3858644ULL,
                 0xfed30c10a7ba43c4ULL, 0x9684ULL, 0x2ULL, 0x795ULL, 0x0ULL});
}

TEST_CASE("single-player golden: long Endless Hell run kept alive past the first boss") {
    // 24 minutes, one boss killed.
    checkGolden({404, Difficulty::Hell, maps::MapId::Wasteland, RunMode::Endless, 1440, 24},
                {0x63d4eeb61b2d13faULL, 0xd122837d54c2246fULL, 0x3245acd2236e6090ULL, 0xc947c4227601d24eULL, 0x49a6d4de748c82b8ULL,
                 0xc2eb7bee15510c0ULL,  0xfbeb4317174f1f37ULL, 0x477a68f46a4a5f8cULL, 0x8061ef95cb7fe86ULL,  0x7189167a97ebca3ULL,
                 0xdb82ae0be7f17f1cULL, 0xf4e7a89c3fcc37baULL, 0x68c30a429ccb5a2fULL, 0xa6013c19c5c9ac2ULL,  0xc0ca8ba86ab5595bULL,
                 0x85271e49d8864cdcULL, 0xfdb3be21138d0980ULL, 0xed3918b81d95ecaeULL, 0xfb2edc0e5fe8d08eULL, 0x4218bab5e665e236ULL,
                 0x35f7aae1a9731f28ULL, 0xd192eb3dc1dc8acbULL, 0x14a89016d79473d4ULL, 0xb3a06fc1d78bb23aULL, 0xb3a06fc1d78bb23aULL,
                 0x15180ULL,            0x0ULL,                0xb99ULL,              0x1ULL});
}

TEST_CASE("single-player golden: Normal wasteland run kept alive until the boss has come, then fighting it with real damage") {
    // Dies to the boss at 10:58.
    checkGolden({606, Difficulty::Normal, maps::MapId::Wasteland, RunMode::Story, 605, 20},
                {0x184964fc5f8296b6ULL, 0x1be23453497b7e86ULL, 0x9668e5c8a0169c73ULL, 0x2450af8fde764e8fULL, 0xae9e542531fe1d72ULL,
                 0x7e9409a3a3e16da1ULL, 0xe1b0657815c4dd0aULL, 0xa9749e2b16b380a9ULL, 0x3bdb549828a06d5fULL, 0xdd22203a824e8f92ULL,
                 0x4e2852cce898b2feULL, 0x9a0dULL, 0x3ULL, 0x12aeULL, 0x0ULL});
}
