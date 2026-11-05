#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Pickups/Pickups.h"
#include "components/Upgrades/Upgrades.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> makeRun(uint64_t seed = 77) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed);
    return state;
}

int countEvents(const GameState& state, EventType type) {
    int count = 0;
    for (const SimEvent& event : state.events) {
        if (event.type == type) count++;
    }
    return count;
}

const Enemy* findBoss(const GameState& state) {
    for (const Enemy& enemy : state.enemies) {
        if (enemy.type == EnemyType::Boss) return &enemy;
    }
    return nullptr;
}

// Puts a boss with almost no HP on top of the player, so the rifle's first shot kills it.
Enemy* addDyingBossOnPlayer(GameState& state) {
    Enemy* boss = enemies::spawn(state, EnemyType::Boss, state.player.pos + Vec2{10.0f, 0.0f}, false);
    REQUIRE(boss != nullptr);
    boss->hp = 1.0f;
    return boss;
}

}  // namespace

// --- Death in a step beats anything later in that step ---------------------------

TEST_CASE("a heal pickup can't undo a lethal hit from the same step") {
    auto state = makeRun();
    state->player.hp = 1.0f;
    REQUIRE(enemies::spawn(*state, EnemyType::Walker, state->player.pos + Vec2{5.0f, 0.0f}, false));
    pickups::spawnPickup(*state, state->player.pos, PickupType::Heal);

    sim::step(*state, InputFrame{});

    CHECK(state->pickups.count == 0);  // It was picked up, so the heal really was tried.
    CHECK(state->player.hp <= 0.0f);
    CHECK(state->run.phase == RunPhase::Dead);
    CHECK(countEvents(*state, EventType::PlayerDied) == 1);
}

TEST_CASE("a heal pickup still heals a living player") {
    auto state = makeRun();
    state->player.hp = 50.0f;
    pickups::spawnPickup(*state, state->player.pos, PickupType::Heal);
    sim::step(*state, InputFrame{});
    CHECK(state->player.hp == doctest::Approx(50.0f + state->player.maxHp * balance::kHealPickupFraction));
}

TEST_CASE("a player killed in the step the boss dies loses the run") {
    auto state = makeRun();
    state->player.hp = 1.0f;
    addDyingBossOnPlayer(*state);

    sim::step(*state, InputFrame{});

    CHECK(findBoss(*state) == nullptr);  // The shot really did kill the boss.
    CHECK(countEvents(*state, EventType::BossKilled) == 1);
    CHECK(state->run.phase == RunPhase::Dead);
    CHECK(countEvents(*state, EventType::PlayerDied) == 1);
}

TEST_CASE("a player who survives the boss's last step wins") {
    auto state = makeRun();
    addDyingBossOnPlayer(*state);

    sim::step(*state, InputFrame{});

    CHECK(state->player.hp > 0.0f);
    CHECK(state->run.phase == RunPhase::Won);
    CHECK(countEvents(*state, EventType::PlayerDied) == 0);
}

// --- Reroll and card in one step -------------------------------------------------

TEST_CASE("a card and a reroll in one step take the card on screen and keep the reroll") {
    // Two runs that differ only in the reroll flag must end exactly alike.
    auto cardOnly = makeRun();
    auto both = makeRun();
    upgrades::addXp(*cardOnly, balance::xpForLevel(1));
    upgrades::addXp(*both, balance::xpForLevel(1));
    sim::step(*cardOnly, InputFrame{});
    sim::step(*both, InputFrame{});
    REQUIRE(both->run.phase == RunPhase::LevelUp);

    InputFrame pick;
    pick.chooseCard = 1;
    InputFrame pickAndReroll = pick;
    pickAndReroll.reroll = true;
    sim::step(*cardOnly, pick);
    sim::step(*both, pickAndReroll);

    CHECK(both->run.phase == RunPhase::Playing);
    CHECK(both->run.rerollsLeft == balance::kStartingRerolls);
    CHECK(sim::stateHash(*both) == sim::stateHash(*cardOnly));
}

