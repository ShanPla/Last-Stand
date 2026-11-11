#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Pickups/Pickups.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> runAtBossTime() {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 5);
    state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 1;
    state->run.nextEliteEvent = 1000;
    return state;
}

const Enemy* findBoss(const GameState& state) {
    for (const Enemy& enemy : state.enemies) {
        if (enemy.type == EnemyType::Boss) return &enemy;
    }
    return nullptr;
}

int countPickups(const GameState& state, PickupType type) {
    int count = 0;
    for (const Pickup& pickup : state.pickups) {
        if (pickup.type == type) count++;
    }
    return count;
}

}  // namespace

TEST_CASE("the boss arrival clears the horde with no kills or drops") {
    auto state = runAtBossTime();
    for (int i = 0; i < 300; ++i) {
        enemies::spawn(*state, EnemyType::Walker, Vec2{500.0f + static_cast<float>(i), 400.0f}, false);
    }
    sim::step(*state, InputFrame{});
    CHECK(state->run.bossSpawned);
    CHECK(state->enemies.count == 1);
    REQUIRE(findBoss(*state) != nullptr);
    CHECK(state->run.kills == 0);
    CHECK(state->gems.count == 0);
}

TEST_CASE("the boss always appears when the enemy pool was full") {
    auto state = runAtBossTime();
    while (enemies::spawn(*state, EnemyType::Walker, Vec2{600.0f, 600.0f}, false)) {
    }
    sim::step(*state, InputFrame{});
    CHECK(findBoss(*state) != nullptr);
}

TEST_CASE("a player running away can't leave the boss out of weapon range") {
    auto state = runAtBossTime();
    sim::step(*state, InputFrame{});
    InputFrame run;
    run.moveX = 1.0f;
    // A full minute of running at top speed, with invulnerability so the run continues.
    // The boss stands still while charging an attack, so the player can briefly get
    // out of range. What matters is that the boss spends most of the time in range.
    int stepsInRange = 0;
    for (int i = 0; i < 3600; ++i) {
        state->firstPlayer().hp = state->firstPlayer().maxHp;
        if (state->run.phase == RunPhase::LevelUp) {
            run.chooseCard = 0;
        } else {
            run.chooseCard = -1;
        }
        sim::step(*state, run);
        state->events.clear();
        const Enemy* boss = findBoss(*state);
        REQUIRE(boss != nullptr);
        if (distance(boss->pos, state->firstPlayer().pos) < balance::kWeaponRange) stepsInRange++;
    }
    MESSAGE("boss in weapon range for ", stepsInRange, " of 3600 steps");
    CHECK(stepsInRange > 3600 * 3 / 4);
}

TEST_CASE("walkers and ammo crates keep arriving during the boss fight") {
    auto state = runAtBossTime();
    sim::step(*state, InputFrame{});
    const int seconds = 30;
    int cratesSeen = 0;
    int mostEnemies = 0;
    for (int i = 0; i < seconds * 60; ++i) {
        state->firstPlayer().hp = state->firstPlayer().maxHp;
        InputFrame input;
        if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        const int before = countPickups(*state, PickupType::Ammo);
        sim::step(*state, input);
        state->events.clear();
        if (countPickups(*state, PickupType::Ammo) > before) cratesSeen++;
        if (state->enemies.count > mostEnemies) mostEnemies = state->enemies.count;
    }
    CHECK(cratesSeen >= 2);
    CHECK(mostEnemies > 1);
}

TEST_CASE("a full pickup pool still accepts a new drop") {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 6);
    for (int i = 0; i < kMaxPickups; ++i) pickups::spawnPickup(*state, Vec2{5000.0f, 5000.0f}, PickupType::Heal);
    CHECK(state->pickups.count == kMaxPickups);
    pickups::spawnPickup(*state, Vec2{5000.0f, 5000.0f}, PickupType::Ammo);
    CHECK(state->pickups.count == kMaxPickups);
    CHECK(countPickups(*state, PickupType::Ammo) == 1);
}
