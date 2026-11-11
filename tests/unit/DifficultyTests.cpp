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
        state->firstPlayer().hp = state->firstPlayer().maxHp;
        InputFrame input;
        if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        spawned = static_cast<int>(state->run.nextId);
        sim::step(*state, input);
        state->events.clear();
    }
    return spawned;
}

// Stands still and takes the first card, with the player kept alive.
void playSeconds(GameState& state, int seconds) {
    for (int i = 0; i < 60 * seconds; ++i) {
        state.firstPlayer().hp = state.firstPlayer().maxHp;
        InputFrame input;
        if (state.run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        sim::step(state, input);
        state.events.clear();
    }
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
    player::damage(*easy, 0, 40.0f);
    player::damage(*hard, 0, 40.0f);
    CHECK(easy->firstPlayer().hp > hard->firstPlayer().hp);
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
    // Both runs play the same seed and the same input for 40 seconds. The level is then
    // copied across, because the hash includes it and would differ even if the levels
    // changed nothing. What is left to differ is what the levels did to the game.
    auto easy = newRun(Difficulty::Easy);
    auto hard = newRun(Difficulty::Hard);
    playSeconds(*easy, 40);
    playSeconds(*hard, 40);
    hard->run.difficulty = easy->run.difficulty;
    CHECK(sim::stateHash(*easy) != sim::stateHash(*hard));

    // The control: with the same level the two runs match exactly.
    auto again = newRun(Difficulty::Easy);
    auto twin = newRun(Difficulty::Easy);
    playSeconds(*again, 40);
    playSeconds(*twin, 40);
    CHECK(sim::stateHash(*again) == sim::stateHash(*twin));
}

TEST_CASE("Hell is harder than Hard in every number") {
    const balance::DifficultyTuning& hard = balance::difficultyTuning(Difficulty::Hard);
    const balance::DifficultyTuning& hell = balance::difficultyTuning(Difficulty::Hell);
    CHECK(hell.enemyHp > hard.enemyHp);
    CHECK(hell.enemyDamage > hard.enemyDamage);
    // Far more zombies, not a little more: at least half as many again as Hard.
    CHECK(hell.spawnRate > hard.spawnRate * 1.5f);
}

TEST_CASE("Hell spawns more, tougher zombies than Hard, and hits harder") {
    CHECK(enemiesAfter(Difficulty::Hard, 60 * 40) < enemiesAfter(Difficulty::Hell, 60 * 40));
    auto hard = newRun(Difficulty::Hard);
    auto hell = newRun(Difficulty::Hell);
    CHECK(enemies::spawn(*hard, EnemyType::Brute, Vec2{500.0f, 0.0f}, false)->maxHp < enemies::spawn(*hell, EnemyType::Brute, Vec2{500.0f, 0.0f}, false)->maxHp);
    player::damage(*hard, 0, 40.0f);
    player::damage(*hell, 0, 40.0f);
    CHECK(hard->firstPlayer().hp > hell->firstPlayer().hp);
}

TEST_CASE("a full pool simply stops spawning, and the run keeps going") {
    auto hell = newRun(Difficulty::Hell);
    for (int i = 0; i < kMaxEnemies; ++i) CHECK(enemies::spawn(*hell, EnemyType::Walker, Vec2{2000.0f, 0.0f}, false) != nullptr);
    CHECK(enemies::spawn(*hell, EnemyType::Walker, Vec2{2000.0f, 0.0f}, false) == nullptr);
    hell->run.nextEliteEvent = 1000;
    for (int i = 0; i < 60 * 5; ++i) {
        hell->firstPlayer().hp = hell->firstPlayer().maxHp;
        sim::step(*hell, InputFrame{});
        hell->events.clear();
    }
    CHECK(hell->enemies.count <= kMaxEnemies);
}
