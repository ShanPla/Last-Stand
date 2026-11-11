#include "doctest.h"

#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> newRun() {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 11);
    return state;
}

int countAmmoCrates(const GameState& state) {
    int count = 0;
    for (const Pickup& pickup : state.pickups) {
        if (pickup.type == PickupType::Ammo) count++;
    }
    return count;
}

}  // namespace

TEST_CASE("ammo crates land on a timer, even with no kills") {
    auto state = newRun();
    state->run.nextEliteEvent = 1000;
    const int steps = static_cast<int>(std::lround(balance::kTimedAmmoCrateSeconds / kDt)) * 3;
    for (int i = 0; i < steps; ++i) {
        // Keeps the run alive and empty, so that every crate comes from the timer.
        state->enemies.clear();
        state->firstPlayer().hp = state->firstPlayer().maxHp;
        InputFrame input;
        if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        sim::step(*state, input);
        state->events.clear();
    }
    CHECK(state->run.kills == 0);
    CHECK(countAmmoCrates(*state) == 3);
}

TEST_CASE("timed ammo crates land close enough to reach") {
    auto state = newRun();
    state->run.nextEliteEvent = 1000;
    state->run.tick = static_cast<uint32_t>(std::lround(balance::kTimedAmmoCrateSeconds / kDt)) - 1;
    sim::step(*state, InputFrame{});
    REQUIRE(countAmmoCrates(*state) == 1);
    CHECK(distance(state->pickups[0].pos, state->firstPlayer().pos) == doctest::Approx(balance::kTimedAmmoCrateDistance));
}
