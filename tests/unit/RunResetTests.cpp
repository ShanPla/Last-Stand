#include "doctest.h"

#include "StateFields.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace ls;
using namespace ls::testing;

namespace {

// Walks in a slow circle and takes the first card, so both runs get the same input.
InputFrame circleInput(const GameState& state) {
    InputFrame input;
    const float angle = static_cast<float>(state.run.tick) * 0.01f;
    input.moveX = std::cos(angle);
    input.moveY = std::sin(angle);
    if (state.run.phase == RunPhase::LevelUp) input.chooseCard = 0;
    return input;
}

// A state that has been played and left messy: something in every pool, a half-finished
// boss fight, a dead player, and a random number generator that has moved on. Starting a
// new run on it must forget all of that.
std::unique_ptr<GameState> makeUsedState(Difficulty difficulty) {
    auto state = makeBusyState(999);
    state->run.difficulty = difficulty;
    state->run.phase = RunPhase::Dead;
    state->run.bossSpawned = true;
    state->run.bossPhase = 2;
    state->run.bossHpFraction = 0.4f;
    state->firstPlayer().rerollsLeft = 0;
    state->firstPlayer().pendingLevels = 3;
    state->firstPlayer().offerCount = 3;
    state->run.nextEliteEvent = 7;
    state->run.nextPickupDropKills = 9999;
    state->firstPlayer().bladeAngle = 2.5f;
    state->run.debugEnemyTarget = 40;
    state->run.debugProjectileTarget = 40;
    state->run.damageByWeapon[2] = 123.0f;
    state->run.waveAccumulators[4] = 0.75f;
    state->firstPlayer().hp = 1.0f;
    state->firstPlayer().dashCooldown = 3.0f;
    state->firstPlayer().dashLeft = 0.1f;
    state->firstPlayer().level = 12;
    state->firstPlayer().xp = 77;
    state->rng.nextU32();
    // seed() sets the position in the sequence but not the stream, so a reset that was left
    // out would only show if the stream is not the default one.
    state->rng.stream = 0x1234567ULL;
    return state;
}

}  // namespace

TEST_CASE("a new run on a used game state starts exactly like a new run on a fresh one") {
    const Difficulty levels[] = {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard};
    for (const Difficulty difficulty : levels) {
        CAPTURE(static_cast<int>(difficulty));
        auto used = makeUsedState(difficulty == Difficulty::Hard ? Difficulty::Easy : Difficulty::Hard);
        REQUIRE(used->enemies.count > 0);
        REQUIRE(used->projectiles.count > 0);
        REQUIRE(used->enemyBullets.count > 0);
        REQUIRE(used->zones.count > 0);
        REQUIRE(used->gems.count > 0);
        REQUIRE(used->pickups.count > 0);
        REQUIRE(used->events.count > 0);

        sim::newRun(*used, 5, difficulty);
        auto fresh = std::make_unique<GameState>();
        sim::newRun(*fresh, 5, difficulty);

        // The hash covers the run, the generator, the player and the count of every pool.
        CHECK(sim::stateHash(*used) == sim::stateHash(*fresh));
        CHECK(used->enemies.count == 0);
        CHECK(used->projectiles.count == 0);
        CHECK(used->enemyBullets.count == 0);
        CHECK(used->zones.count == 0);
        CHECK(used->gems.count == 0);
        CHECK(used->pickups.count == 0);
        // The event list is not in the hash, so it is checked directly.
        CHECK(used->events.count == 0);

        // And the whole run that follows is the same, step by step, including its events.
        int firstMismatch = -1;
        int eventMismatch = -1;
        for (int i = 0; i < 60 * 15; ++i) {
            sim::step(*used, circleInput(*used));
            sim::step(*fresh, circleInput(*fresh));
            if (firstMismatch < 0 && sim::stateHash(*used) != sim::stateHash(*fresh)) firstMismatch = i;
            bool sameEvents = used->events.count == fresh->events.count;
            for (int e = 0; sameEvents && e < used->events.count; ++e) {
                sameEvents = used->events[e].type == fresh->events[e].type && used->events[e].detail == fresh->events[e].detail &&
                             used->events[e].value == fresh->events[e].value;
            }
            if (eventMismatch < 0 && !sameEvents) eventMismatch = i;
            used->events.clear();
            fresh->events.clear();
        }
        CHECK_MESSAGE(firstMismatch == -1, "the state parted at step " << firstMismatch);
        CHECK_MESSAGE(eventMismatch == -1, "the events parted at step " << eventMismatch);
        CHECK(used->run.kills > 0);
    }
}

TEST_CASE("a second run on the same state equals the first run, state for state") {
    // The same thing the Restart button does: play, then start again on the same object.
    auto game = std::make_unique<GameState>();
    sim::newRun(*game, 12);
    std::vector<uint64_t> firstRun;
    for (int i = 0; i < 60 * 20; ++i) {
        sim::step(*game, circleInput(*game));
        game->events.clear();
        firstRun.push_back(sim::stateHash(*game));
    }
    sim::newRun(*game, 12);
    for (int i = 0; i < 60 * 20; ++i) {
        sim::step(*game, circleInput(*game));
        game->events.clear();
        REQUIRE_MESSAGE(sim::stateHash(*game) == firstRun[static_cast<size_t>(i)], "the second run parted at step " << i);
    }
}
