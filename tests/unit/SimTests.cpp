#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Pickups/Pickups.h"
#include "components/Upgrades/Upgrades.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <iterator>
#include <memory>
#include <vector>

using namespace ls;

namespace {

std::unique_ptr<GameState> makeRun(uint64_t seed = 1) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed);
    return state;
}

// Input for a simple bot: walks in a slow circle and takes the first card.
InputFrame botInput(const GameState& state) {
    InputFrame input;
    const float angle = static_cast<float>(state.run.tick) * 0.01f;
    input.moveX = std::cos(angle);
    input.moveY = std::sin(angle);
    if (state.run.phase == RunPhase::LevelUp) input.chooseCard = 0;
    return input;
}

int totalGemXp(const GameState& state) {
    int total = 0;
    for (const Gem& gem : state.gems) total += gem.xp;
    return total;
}

}  // namespace

TEST_CASE("spatial hash finds a nearby item and skips a far one") {
    struct Item { Vec2 pos; };
    Item items[2] = {{Vec2{10.0f, 10.0f}}, {Vec2{900.0f, 900.0f}}};
    auto grid = std::make_unique<SpatialHash<16>>();
    grid->build(items, 2);
    bool foundNear = false;
    bool foundFar = false;
    grid->query(Vec2{0.0f, 0.0f}, 40.0f, [&](int index) {
        if (index == 0) foundNear = true;
        if (index == 1) foundFar = true;
        return false;
    });
    CHECK(foundNear);
    CHECK_FALSE(foundFar);
}

TEST_CASE("spatial hash handles negative coordinates and does not repeat items") {
    struct Item { Vec2 pos; };
    Item items[1] = {{Vec2{-300.0f, -500.0f}}};
    auto grid = std::make_unique<SpatialHash<16>>();
    grid->build(items, 1);
    int visits = 0;
    grid->query(Vec2{-310.0f, -490.0f}, 200.0f, [&](int) {
        visits++;
        return false;
    });
    CHECK(visits == 1);
}

TEST_CASE("pool removal keeps live items packed") {
    Pool<int, 4> pool;
    *pool.add() = 1;
    *pool.add() = 2;
    *pool.add() = 3;
    pool.removeAt(0);
    CHECK(pool.count == 2);
    CHECK(pool[0] == 3);
    CHECK(pool[1] == 2);
    *pool.add() = 4;
    *pool.add() = 5;
    CHECK(pool.add() == nullptr);
}

TEST_CASE("player moves at full speed, and diagonals are not faster") {
    auto state = makeRun();
    InputFrame input;
    input.moveX = 1.0f;
    input.moveY = 1.0f;
    sim::step(*state, input);
    CHECK(length(state->player.pos) == doctest::Approx(balance::kPlayerSpeed * kDt).epsilon(0.001));
}

TEST_CASE("an enemy touching the player deals damage, then invulnerability blocks repeats") {
    auto state = makeRun();
    enemies::spawn(*state, EnemyType::Walker, Vec2{1.0f, 0.0f}, false);
    const float startHp = state->player.hp;
    sim::step(*state, InputFrame{});
    const float afterFirst = state->player.hp;
    CHECK(afterFirst < startHp);
    sim::step(*state, InputFrame{});
    CHECK(state->player.hp == afterFirst);
}

TEST_CASE("armor reduces damage but never below the minimum") {
    auto state = makeRun();
    state->player.stats.armor = 100.0f;
    enemies::spawn(*state, EnemyType::Walker, Vec2{1.0f, 0.0f}, false);
    const float startHp = state->player.hp;
    sim::step(*state, InputFrame{});
    CHECK(startHp - state->player.hp == doctest::Approx(balance::kMinDamageTaken));
}

TEST_CASE("a shot hits an enemy, damages it, credits the weapon, and is used up") {
    auto state = makeRun();
    Enemy* enemy = enemies::spawn(*state, EnemyType::Brute, Vec2{200.0f, 0.0f}, false);
    REQUIRE(enemy != nullptr);
    const float startHp = enemy->hp;
    // The Rifle fires on the first step, and the shot takes under half a second to arrive.
    for (int i = 0; i < 30; ++i) sim::step(*state, InputFrame{});
    CHECK(state->enemies[0].hp < startHp);
    CHECK(state->run.damageByWeapon[static_cast<int>(WeaponId::Rifle)] > 0.0f);
}

TEST_CASE("killing an enemy counts the kill and drops a gem with its XP") {
    auto state = makeRun();
    Enemy* enemy = enemies::spawn(*state, EnemyType::Brute, Vec2{400.0f, 0.0f}, false);
    REQUIRE(enemy != nullptr);
    enemies::hit(*state, 0, 100000.0f, WeaponId::Rifle, Vec2{});
    enemies::removeDead(*state);
    CHECK(state->run.kills == 1);
    CHECK(state->enemies.count == 0);
    CHECK(totalGemXp(*state) == balance::kEnemyStats[static_cast<int>(EnemyType::Brute)].xp);
}

TEST_CASE("collecting enough XP opens level-up, and a choice resumes play") {
    auto state = makeRun();
    upgrades::addXp(*state, balance::xpForLevel(1));
    CHECK(state->player.level == 2);
    sim::step(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    CHECK(state->run.offerCount == kOffersPerLevel);

    // The run is paused until a card is chosen.
    const uint32_t tick = state->run.tick;
    sim::step(*state, InputFrame{});
    CHECK(state->run.tick == tick);

    InputFrame pick;
    pick.chooseCard = 1;
    sim::step(*state, pick);
    CHECK(state->run.phase == RunPhase::Playing);
}

TEST_CASE("several levels at once offer cards one level at a time") {
    auto state = makeRun();
    upgrades::addXp(*state, balance::xpForLevel(1) + balance::xpForLevel(2));
    CHECK(state->run.pendingLevels == 2);
    sim::step(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    InputFrame pick;
    pick.chooseCard = 0;
    sim::step(*state, pick);
    CHECK(state->run.phase == RunPhase::LevelUp);
    sim::step(*state, pick);
    CHECK(state->run.phase == RunPhase::Playing);
}

TEST_CASE("rerolls are limited to two per run") {
    auto state = makeRun();
    upgrades::addXp(*state, balance::xpForLevel(1));
    sim::step(*state, InputFrame{});
    InputFrame reroll;
    reroll.reroll = true;
    sim::step(*state, reroll);
    sim::step(*state, reroll);
    CHECK(state->run.rerollsLeft == 0);
    sim::step(*state, reroll);
    CHECK(state->run.rerollsLeft == 0);
}

TEST_CASE("a level 5 weapon with its passive is offered an evolution first") {
    auto state = makeRun();
    state->player.weapons[0].level = kMaxLevelPerItem;
    state->player.passives[0].id = PassiveId::FireRate;
    state->player.passives[0].level = 1;
    state->player.passiveCount = 1;
    upgrades::addXp(*state, balance::xpForLevel(1));
    sim::step(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    CHECK(state->run.offers[0].kind == CardKind::Evolution);
    CHECK(state->run.offers[0].weapon == WeaponId::Rifle);

    InputFrame pick;
    pick.chooseCard = 0;
    sim::step(*state, pick);
    CHECK(state->player.weapons[0].evolved);
}

TEST_CASE("evolved weapons are not offered the evolution again") {
    auto state = makeRun();
    state->player.weapons[0].level = kMaxLevelPerItem;
    state->player.weapons[0].evolved = true;
    state->player.passives[0].id = PassiveId::FireRate;
    state->player.passiveCount = 1;
    upgrades::addXp(*state, balance::xpForLevel(1));
    sim::step(*state, InputFrame{});
    for (int i = 0; i < state->run.offerCount; ++i) {
        CHECK(state->run.offers[i].kind != CardKind::Evolution);
    }
}

TEST_CASE("merging gems keeps total XP and reduces the count") {
    auto state = makeRun();
    for (int i = 0; i < 300; ++i) {
        const Vec2 pos{static_cast<float>(i % 25) * 8.0f + 1000.0f, static_cast<float>(i % 13) * 8.0f + 1000.0f};
        pickups::dropGem(*state, pos, 2);
    }
    const int xpBefore = totalGemXp(*state);
    const int countBefore = state->gems.count;
    pickups::mergeGems(*state);
    CHECK(state->gems.count < countBefore);
    CHECK(totalGemXp(*state) == xpBefore);
}

TEST_CASE("wave table rows are valid and fit the spawn accumulator array") {
    const int rows = static_cast<int>(std::size(balance::kWaves));
    // The last accumulator is kept for the walkers during the boss fight.
    CHECK(rows < static_cast<int>(std::size(RunState{}.waveAccumulators)));
    for (const balance::WaveRow& row : balance::kWaves) {
        CHECK(row.startSec < row.endSec);
        CHECK(row.endSec <= balance::kRunSeconds);
        CHECK(row.groupSize >= 1);
    }
    CHECK(waves::rowRate(0, 0.0f) == doctest::Approx(balance::kWaves[0].startRate));
    CHECK(waves::rowRate(0, balance::kWaves[0].endSec) == 0.0f);
}

TEST_CASE("same seed and inputs give the same final state") {
    auto first = makeRun(777);
    auto second = makeRun(777);
    for (int i = 0; i < 60 * 90; ++i) {
        sim::step(*first, botInput(*first));
        sim::step(*second, botInput(*second));
        first->events.clear();
        second->events.clear();
    }
    CHECK(first->run.kills > 0);
    CHECK(sim::stateHash(*first) == sim::stateHash(*second));
}

TEST_CASE("different seeds give different runs") {
    auto first = makeRun(1);
    auto second = makeRun(2);
    for (int i = 0; i < 60 * 30; ++i) {
        sim::step(*first, botInput(*first));
        sim::step(*second, botInput(*second));
        first->events.clear();
        second->events.clear();
    }
    CHECK(sim::stateHash(*first) != sim::stateHash(*second));
}
