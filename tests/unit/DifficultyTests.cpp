#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Player/Player.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> newRun(Difficulty difficulty) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 21, difficulty);
    return state;
}

int enemiesAfter(Difficulty difficulty, int steps) {
    auto state = newRun(difficulty);
    state->run.nextEliteEvent = 1000;
    int spawned = 0;
    for (int i = 0; i < steps; ++i) {
        state->player.hp = state->player.maxHp;
        InputFrame input;
        if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        spawned = static_cast<int>(state->run.nextId);
        sim::step(*state, input);
        state->events.clear();
    }
    return spawned;
}

}  // namespace

TEST_CASE("a new run is Normal unless told otherwise, and Normal changes nothing") {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 1);
    CHECK(state->run.difficulty == Difficulty::Normal);
    const balance::DifficultyTuning& normal = balance::difficultyTuning(Difficulty::Normal);
    CHECK(normal.enemyHp == 1.0f);
    CHECK(normal.enemyDamage == 1.0f);
    CHECK(normal.spawnRate == 1.0f);
}

TEST_CASE("harder levels give zombies more health") {
    auto easy = newRun(Difficulty::Easy);
    auto normal = newRun(Difficulty::Normal);
    auto hard = newRun(Difficulty::Hard);
    const float easyHp = enemies::spawn(*easy, EnemyType::Brute, Vec2{500.0f, 0.0f}, false)->maxHp;
    const float normalHp = enemies::spawn(*normal, EnemyType::Brute, Vec2{500.0f, 0.0f}, false)->maxHp;
    const float hardHp = enemies::spawn(*hard, EnemyType::Brute, Vec2{500.0f, 0.0f}, false)->maxHp;
    CHECK(easyHp < normalHp);
    CHECK(normalHp < hardHp);
}

TEST_CASE("harder levels hurt the player more") {
    auto easy = newRun(Difficulty::Easy);
    auto hard = newRun(Difficulty::Hard);
    player::damage(*easy, 40.0f);
    player::damage(*hard, 40.0f);
    CHECK(easy->player.hp > hard->player.hp);
}

TEST_CASE("harder levels spawn more zombies") {
    const int steps = 60 * 40;
    const int easy = enemiesAfter(Difficulty::Easy, steps);
    const int normal = enemiesAfter(Difficulty::Normal, steps);
    const int hard = enemiesAfter(Difficulty::Hard, steps);
    CHECK(easy < normal);
    CHECK(normal < hard);
}

TEST_CASE("boss health follows difficulty") {
    auto easy = newRun(Difficulty::Easy);
    auto hard = newRun(Difficulty::Hard);
    CHECK(waves::bossHpForBuild(*easy) < waves::bossHpForBuild(*hard));
}

TEST_CASE("the same seed on different levels gives different runs") {
    auto easy = newRun(Difficulty::Easy);
    auto hard = newRun(Difficulty::Hard);
    CHECK(sim::stateHash(*easy) != sim::stateHash(*hard));
}
