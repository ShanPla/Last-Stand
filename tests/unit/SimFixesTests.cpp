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

// --- Spitter fire timer ----------------------------------------------------------

TEST_CASE("a spitter back in range fires one shot, not a catch-up burst") {
    auto state = makeRun();
    Enemy* spitter = enemies::spawn(*state, EnemyType::Spitter, Vec2{1400.0f, 0.0f}, false);
    REQUIRE(spitter != nullptr);

    // Thirty seconds beyond the reach of its bullets, held there so it can't walk in.
    for (int i = 0; i < 1800; ++i) {
        spitter->pos = Vec2{1400.0f, 0.0f};
        enemies::update(*state);
    }
    REQUIRE(state->enemyBullets.count == 0);

    spitter->pos = Vec2{800.0f, 0.0f};
    for (int i = 0; i < 60; ++i) {
        enemies::update(*state);
        spitter->pos = Vec2{800.0f, 0.0f};
    }
    CHECK(state->enemyBullets.count == 1);
}

TEST_CASE("a spitter in range keeps its normal rate of fire") {
    auto state = makeRun();
    Enemy* spitter = enemies::spawn(*state, EnemyType::Spitter, Vec2{800.0f, 0.0f}, false);
    REQUIRE(spitter != nullptr);
    const int steps = 1800;
    for (int i = 0; i < steps; ++i) {
        enemies::update(*state);
        spitter->pos = Vec2{800.0f, 0.0f};
    }
    const float expected = static_cast<float>(steps) * kDt / balance::kSpitterFireInterval;
    CHECK(static_cast<float>(state->enemyBullets.count) == doctest::Approx(expected).epsilon(0.1));
}

// --- Separation from wide neighbours ---------------------------------------------

namespace {

// How far one update moves a Walker at pos, once with the neighbour and once without.
// The player is well away from both, and near enough that a boss just walks.
Vec2 pushOnWalker(Vec2 walkerPos, EnemyType neighbourType, Vec2 neighbourPos, bool elite = false) {
    Vec2 result[2];
    for (int withNeighbour = 0; withNeighbour < 2; ++withNeighbour) {
        auto state = makeRun();
        state->player.pos = Vec2{300.0f, 300.0f};
        REQUIRE(enemies::spawn(*state, EnemyType::Walker, walkerPos, false));
        if (withNeighbour == 1) {
            REQUIRE(enemies::spawn(*state, neighbourType, neighbourPos, elite));
            // A Brute also gets pushed by the Walker and could step clear by itself. A
            // second Walker on its far side cancels that, as in a crowd around a Brute.
            if (neighbourType != EnemyType::Boss) {
                REQUIRE(enemies::spawn(*state, EnemyType::Walker, neighbourPos + (neighbourPos - walkerPos), false));
            }
        }
        enemies::update(*state);
        result[withNeighbour] = state->enemies[0].pos;
    }
    return result[1] - result[0];
}

}  // namespace

TEST_CASE("a zombie is pushed out of a wide neighbour whose center is in another grid cell") {
    // A Walker's own short look covers only grid cell 0 here. Each neighbour's center is
    // in cell 1, yet they overlap.
    CHECK(pushOnWalker(Vec2{33.0f, 10.0f}, EnemyType::Brute, Vec2{68.0f, 10.0f}).x < -1.0f);
    CHECK(pushOnWalker(Vec2{33.0f, 10.0f}, EnemyType::Brute, Vec2{78.0f, 10.0f}, true).x < -1.0f);
    CHECK(pushOnWalker(Vec2{10.0f, 10.0f}, EnemyType::Boss, Vec2{70.0f, 10.0f}).x < -1.0f);
}

TEST_CASE("the push out of a wide neighbour doesn't depend on grid cells") {
    const float expected = -60.0f * (71.0f - 60.0f) / 71.0f * balance::kSeparationStrength * kDt;
    for (int offsetY = 0; offsetY < 64; offsetY += 8) {
        for (int offsetX = 0; offsetX < 64; offsetX += 8) {
            const Vec2 walker{static_cast<float>(offsetX), static_cast<float>(offsetY)};
            const Vec2 push = pushOnWalker(walker, EnemyType::Boss, walker + Vec2{60.0f, 0.0f});
            CHECK(push.x == doctest::Approx(expected).epsilon(0.25));
        }
    }
}

TEST_CASE("a wide neighbour that an enemy can see pushes it only once") {
    // The two share a grid cell, so both of the push's code paths could reach this pair.
    // One push is at most this much. It can be less, as the Brute moves away first.
    const float onePush = 25.0f * (39.0f - 25.0f) / 39.0f * balance::kSeparationStrength * kDt;
    const Vec2 push = pushOnWalker(Vec2{20.0f, 10.0f}, EnemyType::Brute, Vec2{45.0f, 10.0f});
    CHECK(push.x < -1.0f);
    CHECK(push.x > -onePush * 1.1f);
}

TEST_CASE("wide enemies push each other") {
    const Vec2 push = [] {
        auto state = makeRun();
        state->player.pos = Vec2{600.0f, 600.0f};
        REQUIRE(enemies::spawn(*state, EnemyType::Brute, Vec2{33.0f, 10.0f}, false));
        const float startX = state->enemies[0].pos.x;
        REQUIRE(enemies::spawn(*state, EnemyType::Brute, Vec2{75.0f, 10.0f}, false));
        enemies::update(*state);
        return Vec2{state->enemies[0].pos.x - startX, state->enemies[1].pos.x - 75.0f};
    }();
    CHECK(push.x < 0.0f);  // The left one moves left, even though the player is to the right.
    CHECK(push.y > 0.0f);
}

TEST_CASE("the crowd doesn't push the boss") {
    Vec2 bossMove[2];
    for (int withCrowd = 0; withCrowd < 2; ++withCrowd) {
        auto state = makeRun();
        state->player.pos = Vec2{600.0f, 600.0f};
        REQUIRE(enemies::spawn(*state, EnemyType::Boss, Vec2{0.0f, 0.0f}, false));
        if (withCrowd == 1) {
            REQUIRE(enemies::spawn(*state, EnemyType::Brute, Vec2{40.0f, 0.0f}, true));
            REQUIRE(enemies::spawn(*state, EnemyType::Walker, Vec2{-40.0f, 0.0f}, false));
        }
        enemies::update(*state);
        bossMove[withCrowd] = state->enemies[0].pos;
    }
    CHECK(bossMove[1].x == doctest::Approx(bossMove[0].x));
    CHECK(bossMove[1].y == doctest::Approx(bossMove[0].y));
}

// --- Pickup pool eviction --------------------------------------------------------

namespace {

// Each pickup's x position is its drop number, far from the player.
void dropNumbered(GameState& state, int number) {
    pickups::spawnPickup(state, Vec2{5000.0f + static_cast<float>(number), 5000.0f}, PickupType::Heal);
}

bool holdsNumber(const GameState& state, int number) {
    for (const Pickup& pickup : state.pickups) {
        if (pickup.pos.x == 5000.0f + static_cast<float>(number)) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("a full pickup pool evicts the oldest drops, in order") {
    auto state = makeRun();
    for (int i = 0; i < kMaxPickups; ++i) dropNumbered(*state, i);
    const int extra = 5;
    for (int i = 0; i < extra; ++i) dropNumbered(*state, kMaxPickups + i);

    CHECK(state->pickups.count == kMaxPickups);
    for (int i = 0; i < extra; ++i) CHECK_FALSE(holdsNumber(*state, i));
    for (int i = extra; i < kMaxPickups + extra; ++i) CHECK(holdsNumber(*state, i));
}

TEST_CASE("collecting a pickup doesn't change which one is the oldest") {
    auto state = makeRun();
    for (int i = 0; i < kMaxPickups; ++i) dropNumbered(*state, i);
    // Only number 3 is left within reach, so the player collects it from mid-pool.
    state->player.pos = Vec2{5003.0f, 5000.0f};
    for (int i = 0; i < state->pickups.count; ++i) {
        if (i != 3) state->pickups[i].pos.y = 9000.0f;
    }
    pickups::update(*state);
    REQUIRE(state->pickups.count == kMaxPickups - 1);
    CHECK_FALSE(holdsNumber(*state, 3));

    // The gap is refilled by one drop, then the next two must evict numbers 0 and 1.
    dropNumbered(*state, 100);
    dropNumbered(*state, 101);
    dropNumbered(*state, 102);
    CHECK_FALSE(holdsNumber(*state, 0));
    CHECK_FALSE(holdsNumber(*state, 1));
    CHECK(holdsNumber(*state, 2));
    CHECK(holdsNumber(*state, kMaxPickups - 1));
    CHECK(holdsNumber(*state, 100));
    CHECK(holdsNumber(*state, 101));
    CHECK(holdsNumber(*state, 102));
}

