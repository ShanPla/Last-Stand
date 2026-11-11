#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cstdint>
#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> newRun() {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 21, Difficulty::Hell);
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};
    return state;
}

// Fills the pool with Walkers, each one a bit farther out than the one before, so the
// farthest is always the last. Returns the id of that last one.
uint32_t fillPool(GameState& state) {
    uint32_t farthest = 0;
    for (int i = 0; i < kMaxEnemies; ++i) {
        Enemy* walker = enemies::spawn(state, EnemyType::Walker, Vec2{500.0f + 2.0f * static_cast<float>(i), 0.0f}, false);
        REQUIRE(walker != nullptr);
        farthest = walker->id;
    }
    return farthest;
}

bool hasId(const GameState& state, uint32_t id) {
    for (const Enemy& enemy : state.enemies) {
        if (enemy.id == id) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("a scheduled elite always gets a place: the farthest ordinary zombie makes room") {
    auto state = newRun();
    const uint32_t farthest = fillPool(*state);
    REQUIRE(state->enemies.count == kMaxEnemies);

    Enemy* elite = enemies::spawn(*state, EnemyType::Brute, Vec2{900.0f, 0.0f}, true);
    REQUIRE(elite != nullptr);
    CHECK(elite->elite);
    CHECK(state->enemies.count == kMaxEnemies);
    CHECK_FALSE(hasId(*state, farthest));
    // Only that one went. Its neighbour one step nearer is still there.
    CHECK(hasId(*state, farthest - 1));
}

TEST_CASE("an ordinary spawn into a full pool is still dropped, and nothing is removed") {
    auto state = newRun();
    fillPool(*state);
    const uint32_t nextId = state->run.nextId;
    CHECK(enemies::spawn(*state, EnemyType::Walker, Vec2{900.0f, 0.0f}, false) == nullptr);
    CHECK(state->enemies.count == kMaxEnemies);
    CHECK(state->run.nextId == nextId);
}

TEST_CASE("the boss also gets a place in a full pool, and elites and the boss are never the ones removed") {
    auto state = newRun();
    fillPool(*state);
    // The farthest zombie of all is an elite, so it must be passed over.
    Enemy* far = enemies::spawn(*state, EnemyType::Brute, Vec2{9000.0f, 0.0f}, true);
    REQUIRE(far != nullptr);
    const uint32_t eliteId = far->id;
    Enemy* second = enemies::spawn(*state, EnemyType::Brute, Vec2{9500.0f, 0.0f}, true);
    REQUIRE(second != nullptr);
    const uint32_t secondId = second->id;

    Enemy* boss = enemies::spawn(*state, EnemyType::Boss, Vec2{700.0f, 0.0f}, false);
    REQUIRE(boss != nullptr);
    CHECK(boss->type == EnemyType::Boss);
    CHECK(state->enemies.count == kMaxEnemies);
    CHECK(hasId(*state, eliteId));
    CHECK(hasId(*state, secondId));
    int bosses = 0;
    for (const Enemy& enemy : state->enemies) bosses += enemy.type == EnemyType::Boss;
    CHECK(bosses == 1);
}

TEST_CASE("when two ordinary zombies are equally far, the lower id goes") {
    auto state = newRun();
    for (int i = 0; i < kMaxEnemies; ++i) {
        // The two farthest sit on opposite sides, at exactly the same distance.
        const Vec2 pos = i == 7 ? Vec2{-5000.0f, 0.0f} : i == 1900 ? Vec2{5000.0f, 0.0f} : Vec2{600.0f, 0.0f};
        REQUIRE(enemies::spawn(*state, EnemyType::Walker, pos, false) != nullptr);
    }
    const uint32_t lowId = state->enemies[7].id;
    const uint32_t highId = state->enemies[1900].id;
    REQUIRE(lowId < highId);
    REQUIRE(enemies::spawn(*state, EnemyType::Brute, Vec2{800.0f, 0.0f}, true) != nullptr);
    CHECK_FALSE(hasId(*state, lowId));
    CHECK(hasId(*state, highId));
}

TEST_CASE("the same full-pool run gives the same state twice") {
    uint64_t hashes[2];
    for (uint64_t& hash : hashes) {
        auto state = newRun();
        fillPool(*state);
        for (int i = 0; i < 20; ++i) enemies::spawn(*state, EnemyType::Brute, Vec2{900.0f + static_cast<float>(i), 0.0f}, true);
        for (int i = 0; i < 120; ++i) {
            state->firstPlayer().hp = state->firstPlayer().maxHp;
            sim::step(*state, InputFrame{});
            state->events.clear();
        }
        hash = sim::stateHash(*state);
    }
    CHECK(hashes[0] == hashes[1]);
}

// A run that never fills the pool must come out exactly as it did before this rule existed.
// The number was recorded from the code before the change.
TEST_CASE("play below the cap is unchanged by the pool-full rule") {
    for (const Difficulty difficulty : {Difficulty::Normal, Difficulty::Hell}) {
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 4242, difficulty);
        uint64_t hash = 0;
        int peak = 0;
        for (int i = 0; i < 60 * 150; ++i) {
            state->firstPlayer().hp = state->firstPlayer().maxHp;
            InputFrame input;
            input.moveX = (i / 240) % 2 == 0 ? 1.0f : -0.6f;
            input.moveY = (i / 360) % 2 == 0 ? 0.4f : -0.8f;
            if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
            sim::step(*state, input);
            state->events.clear();
            if (state->enemies.count > peak) peak = state->enemies.count;
            if (i % 600 == 0) hash = hash * 1099511628211ull + sim::stateHash(*state);
        }
        hash = hash * 1099511628211ull + sim::stateHash(*state);
        CHECK(peak < kMaxEnemies);
        std::printf("GOLDEN difficulty=%d hash=%llu peak=%d\n", static_cast<int>(difficulty), static_cast<unsigned long long>(hash), peak);
    }
}
