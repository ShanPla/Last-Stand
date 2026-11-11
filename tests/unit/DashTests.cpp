#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Player/Player.h"
#include "components/Upgrades/Upgrades.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <memory>

using namespace ls;

namespace {

constexpr int kDashSteps = static_cast<int>(balance::kDashSeconds / kDt + 0.5f);

std::unique_ptr<GameState> makeRun(uint64_t seed = 5) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed);
    return state;
}

int countEvents(const GameState& state, EventType type) {
    int total = 0;
    for (const SimEvent& event : state.events) {
        if (event.type == type) total++;
    }
    return total;
}

// One step, with the events thrown away like the app does, so counts are per step.
int stepCountingDashes(GameState& state, InputFrame input) {
    sim::step(state, input);
    const int dashes = countEvents(state, EventType::PlayerDashed);
    state.events.clear();
    return dashes;
}

// A step for long waits: keeps him alive and picks any card, so neither can stop the clock.
int calmStep(GameState& state, InputFrame input) {
    state.firstPlayer().hp = state.firstPlayer().maxHp;
    if (state.run.phase == RunPhase::LevelUp) input.chooseCard = 0;
    return stepCountingDashes(state, input);
}

InputFrame dashInput(float x, float y) {
    InputFrame input;
    input.moveX = x;
    input.moveY = y;
    input.dash = true;
    return input;
}

// Steps until the dash is over, and returns how many steps that took.
int runDashToEnd(GameState& state, float x, float y) {
    int steps = 0;
    do {
        stepCountingDashes(state, steps == 0 ? dashInput(x, y) : InputFrame{x, y});
        steps++;
    } while (state.firstPlayer().dashLeft > 0.0f && steps < 100);
    return steps;
}

// Keeps one tough zombie on top of the player, so contact damage is tried on every step.
void placeZombieOnPlayer(GameState& state) {
    Enemy* zombie = enemies::spawn(state, EnemyType::Walker, state.firstPlayer().pos, false);
    REQUIRE(zombie != nullptr);
    zombie->hp = zombie->maxHp = 1.0e9f;
}

}  // namespace

TEST_CASE("a dash covers the dash distance in about 0.2 seconds, in the direction of travel") {
    auto state = makeRun();
    const Vec2 start = state->firstPlayer().pos;
    const int steps = runDashToEnd(*state, 1.0f, 0.0f);
    CHECK(steps == kDashSteps);
    CHECK(state->firstPlayer().pos.x - start.x == doctest::Approx(balance::kDashDistance).epsilon(0.001));
    CHECK(state->firstPlayer().pos.y == doctest::Approx(start.y).epsilon(0.001));
    // It is several times what walking covers in the same time.
    CHECK(balance::kDashDistance > 3.0f * balance::kPlayerSpeed * balance::kDashSeconds);
    CHECK(balance::kDashDistance < 4.0f * balance::kPlayerSpeed * balance::kDashSeconds);
}

TEST_CASE("a diagonal dash is the same length as a straight one") {
    auto state = makeRun();
    const Vec2 start = state->firstPlayer().pos;
    runDashToEnd(*state, 1.0f, 1.0f);
    CHECK(distance(state->firstPlayer().pos, start) == doctest::Approx(balance::kDashDistance).epsilon(0.001));
    CHECK(state->firstPlayer().pos.x - start.x == doctest::Approx(state->firstPlayer().pos.y - start.y));
}

TEST_CASE("standing still, a dash goes the way he faces") {
    auto state = makeRun();
    // Face up by walking up for a step.
    stepCountingDashes(*state, InputFrame{0.0f, -1.0f});
    const Vec2 start = state->firstPlayer().pos;
    runDashToEnd(*state, 0.0f, 0.0f);
    CHECK(state->firstPlayer().pos.x == doctest::Approx(start.x).epsilon(0.001));
    CHECK(start.y - state->firstPlayer().pos.y == doctest::Approx(balance::kDashDistance).epsilon(0.001));
}

TEST_CASE("a dash with no input and no earlier movement goes along the starting facing") {
    auto state = makeRun();
    const Vec2 start = state->firstPlayer().pos;
    runDashToEnd(*state, 0.0f, 0.0f);
    CHECK(distance(state->firstPlayer().pos, start) == doctest::Approx(balance::kDashDistance).epsilon(0.001));
}

TEST_CASE("steering during a dash does not bend it") {
    auto state = makeRun();
    const Vec2 start = state->firstPlayer().pos;
    stepCountingDashes(*state, dashInput(1.0f, 0.0f));
    for (int i = 1; i < kDashSteps; ++i) stepCountingDashes(*state, InputFrame{0.0f, 1.0f});
    CHECK(state->firstPlayer().dashLeft == 0.0f);
    CHECK(state->firstPlayer().pos.x - start.x == doctest::Approx(balance::kDashDistance).epsilon(0.001));
    CHECK(state->firstPlayer().pos.y == doctest::Approx(start.y).epsilon(0.001));
}

TEST_CASE("walking resumes at the normal speed once the dash is over") {
    auto state = makeRun();
    runDashToEnd(*state, 1.0f, 0.0f);
    const float before = state->firstPlayer().pos.x;
    stepCountingDashes(*state, InputFrame{1.0f, 0.0f});
    CHECK(state->firstPlayer().pos.x - before == doctest::Approx(state->firstPlayer().stats.moveSpeed * kDt));
}

TEST_CASE("a dash sends one event, and starts the cooldown") {
    auto state = makeRun();
    CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 1);
    CHECK(state->firstPlayer().dashCooldown == doctest::Approx(balance::kDashCooldownSeconds - kDt));
    for (int i = 1; i < kDashSteps; ++i) CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 0);
}

TEST_CASE("the cooldown blocks a second dash, and it works again afterwards") {
    auto state = makeRun();
    runDashToEnd(*state, 1.0f, 0.0f);

    // Half a second in: pressing dash does nothing at all.
    for (int i = 0; i < 30; ++i) calmStep(*state, InputFrame{1.0f, 0.0f});
    REQUIRE(state->run.phase == RunPhase::Playing);
    const float before = state->firstPlayer().pos.x;
    CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 0);
    CHECK(state->firstPlayer().dashLeft == 0.0f);
    CHECK(state->firstPlayer().pos.x - before == doctest::Approx(state->firstPlayer().stats.moveSpeed * kDt));

    // The refused press is not remembered: the dash does not fire the moment the cooldown ends.
    int dashes = 0;
    const int stepsToReady = static_cast<int>(balance::kDashCooldownSeconds / kDt) + 5;
    for (int i = 0; i < stepsToReady; ++i) dashes += calmStep(*state, InputFrame{1.0f, 0.0f});
    CHECK(dashes == 0);
    CHECK(state->firstPlayer().dashCooldown == 0.0f);

    // Ready now.
    while (state->run.phase != RunPhase::Playing) calmStep(*state, InputFrame{});
    CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 1);
    CHECK(state->firstPlayer().dashLeft > 0.0f);
}

TEST_CASE("holding dash down during a dash does not chain a second one") {
    auto state = makeRun();
    int dashes = 0;
    for (int i = 0; i < 80; ++i) dashes += stepCountingDashes(*state, dashInput(1.0f, 0.0f));
    CHECK(dashes == 1);
}

TEST_CASE("he cannot be hurt for the first part of a dash, and can be after it") {
    auto state = makeRun();
    placeZombieOnPlayer(*state);
    const float full = state->firstPlayer().hp;
    int firstHitStep = -1;
    for (int step = 1; step <= 30 && firstHitStep < 0; ++step) {
        state->enemies[0].pos = state->firstPlayer().pos;
        stepCountingDashes(*state, step == 1 ? dashInput(1.0f, 0.0f) : InputFrame{1.0f, 0.0f});
        if (state->firstPlayer().hp < full) firstHitStep = step;
    }
    REQUIRE(firstHitStep > 0);
    const int safeSteps = firstHitStep - 1;
    CHECK(safeSteps >= static_cast<int>(balance::kDashInvulnerableSeconds / kDt) - 1);
    CHECK(safeSteps <= static_cast<int>(balance::kDashInvulnerableSeconds / kDt) + 1);
    // The window is shorter than the dash itself.
    CHECK(balance::kDashInvulnerableSeconds < balance::kDashSeconds);
}

TEST_CASE("without a dash the same zombie hurts him at once") {
    auto state = makeRun();
    placeZombieOnPlayer(*state);
    const float full = state->firstPlayer().hp;
    state->enemies[0].pos = state->firstPlayer().pos;
    stepCountingDashes(*state, InputFrame{1.0f, 0.0f});
    CHECK(state->firstPlayer().hp < full);
}

TEST_CASE("a hit during a dash neither extends nor ends it") {
    auto reference = makeRun();
    const int expected = runDashToEnd(*reference, 1.0f, 0.0f);

    auto state = makeRun();
    int steps = 0;
    do {
        // Past the safe window, a hit lands.
        if (steps == 10) {
            const float before = state->firstPlayer().hp;
            player::damage(*state, 0, 10.0f);
            CHECK(state->firstPlayer().hp < before);
        }
        stepCountingDashes(*state, steps == 0 ? dashInput(1.0f, 0.0f) : InputFrame{1.0f, 0.0f});
        steps++;
    } while (state->firstPlayer().dashLeft > 0.0f && steps < 100);
    CHECK(steps == expected);
    CHECK(state->firstPlayer().pos.x == doctest::Approx(reference->firstPlayer().pos.x));
}

TEST_CASE("the post-hit window does not stretch the dash window, and the other way round") {
    auto state = makeRun();
    player::damage(*state, 0, 5.0f);  // Starts the short window after a hit.
    CHECK(state->firstPlayer().invulnerable > 0.0f);
    CHECK(state->firstPlayer().dashInvulnerable == 0.0f);
    stepCountingDashes(*state, dashInput(1.0f, 0.0f));
    CHECK(state->firstPlayer().dashInvulnerable > 0.0f);
    CHECK(state->firstPlayer().dashInvulnerable <= balance::kDashInvulnerableSeconds);
}

TEST_CASE("the cooldown only counts down while the game is played, not on the level-up screen") {
    auto state = makeRun();
    runDashToEnd(*state, 1.0f, 0.0f);
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    stepCountingDashes(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    const float cooldown = state->firstPlayer().dashCooldown;
    const float x = state->firstPlayer().pos.x;
    for (int i = 0; i < 120; ++i) stepCountingDashes(*state, InputFrame{1.0f, 0.0f});
    CHECK(state->firstPlayer().dashCooldown == cooldown);
    CHECK(state->firstPlayer().pos.x == x);
}

TEST_CASE("no dash can start on the level-up screen") {
    auto state = makeRun();
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    stepCountingDashes(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    const Vec2 start = state->firstPlayer().pos;
    CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 0);
    CHECK(state->firstPlayer().dashLeft == 0.0f);
    CHECK(state->firstPlayer().dashCooldown == 0.0f);
    CHECK(state->firstPlayer().pos.x == start.x);
    // And a card choice made in the same input does not dash either.
    InputFrame pick = dashInput(1.0f, 0.0f);
    pick.chooseCard = 0;
    stepCountingDashes(*state, pick);
    CHECK(state->run.phase == RunPhase::Playing);
    CHECK(state->firstPlayer().dashLeft == 0.0f);
}

TEST_CASE("a dash cannot start when he is dead") {
    auto state = makeRun();
    state->firstPlayer().hp = 0.0f;
    state->run.phase = RunPhase::Dead;
    CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 0);
    CHECK(state->firstPlayer().dashLeft == 0.0f);
}

TEST_CASE("a new run resets the dash") {
    auto state = makeRun();
    stepCountingDashes(*state, dashInput(1.0f, 0.0f));
    REQUIRE(state->firstPlayer().dashLeft > 0.0f);
    REQUIRE(state->firstPlayer().dashCooldown > 0.0f);
    sim::newRun(*state, 5);
    CHECK(state->firstPlayer().dashLeft == 0.0f);
    CHECK(state->firstPlayer().dashCooldown == 0.0f);
    CHECK(state->firstPlayer().dashInvulnerable == 0.0f);
    CHECK(stepCountingDashes(*state, dashInput(1.0f, 0.0f)) == 1);
}

TEST_CASE("runs with dash inputs are deterministic, and the dash is part of the state hash") {
    auto play = [](bool dashing) {
        auto state = makeRun(31);
        for (int i = 0; i < 900; ++i) {
            InputFrame input;
            input.moveX = std::cos(static_cast<float>(i) * 0.02f);
            input.moveY = std::sin(static_cast<float>(i) * 0.02f);
            input.dash = dashing && i % 250 == 10;
            if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
            sim::step(*state, input);
            state->events.clear();
        }
        return sim::stateHash(*state);
    };
    CHECK(play(true) == play(true));
    CHECK(play(true) != play(false));
}

TEST_CASE("two states that differ only in the dash cooldown hash differently") {
    auto a = makeRun();
    auto b = makeRun();
    CHECK(sim::stateHash(*a) == sim::stateHash(*b));
    b->firstPlayer().dashCooldown = 1.0f;
    CHECK(sim::stateHash(*a) != sim::stateHash(*b));
}
