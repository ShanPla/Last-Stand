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

// --- Event list ------------------------------------------------------------------

TEST_CASE("a flood of frequent events can't crowd out the ones that matter") {
    auto state = makeRun();
    for (int i = 0; i < kMaxEvents * 2; ++i) pushEvent(*state, EventType::EnemyHit, Vec2{});
    CHECK(state->events.count == kEventLimitHits);
    pushEvent(*state, EventType::EnemyKilled, Vec2{});
    CHECK(countEvents(*state, EventType::EnemyKilled) == 1);

    for (int i = 0; i < kMaxEvents * 2; ++i) pushEvent(*state, EventType::EnemyKilled, Vec2{});
    CHECK(state->events.count == kEventLimitCommon);

    const EventType rare[] = {EventType::LevelUp,    EventType::PickupCollected, EventType::BombBlast,
                              EventType::BossKilled, EventType::EliteKilled,     EventType::PlayerDied};
    for (const EventType type : rare) {
        pushEvent(*state, type, Vec2{});
        CHECK(countEvents(*state, type) == 1);
    }
}

TEST_CASE("the event limits leave room for every tier") {
    static_assert(kEventLimitHits < kEventLimitCommon);
    static_assert(kEventLimitCommon < kMaxEvents);
    CHECK(eventLimit(EventType::PlayerDied) == kMaxEvents);
}

namespace {

// A bomb at the player's feet, and more zombies around than the event list has slots.
std::unique_ptr<GameState> bombInHugeCrowd() {
    auto state = makeRun();
    state->player.weaponCount = 0;  // So only the bomb makes hits.
    for (int i = 0; i < kMaxEvents + 300; ++i) {
        const Vec2 pos = fromAngle(static_cast<float>(i) * 0.7f) * (200.0f + static_cast<float>(i % 400));
        REQUIRE(enemies::spawn(*state, EnemyType::Walker, pos, false));
    }
    pickups::spawnPickup(*state, state->player.pos, PickupType::Bomb);
    return state;
}

}  // namespace

TEST_CASE("a bomb in a huge crowd keeps its blast, pickup, kill and level-up events") {
    auto state = bombInHugeCrowd();
    // A gem at the player's feet brings a level-up in the same step.
    pickups::dropGem(*state, state->player.pos, balance::xpForLevel(1));

    sim::step(*state, InputFrame{});

    CHECK(state->run.kills >= kMaxEvents);  // There really was a flood.
    CHECK(countEvents(*state, EventType::BombBlast) == 1);
    CHECK(countEvents(*state, EventType::PickupCollected) == 1);
    CHECK(countEvents(*state, EventType::EnemyKilled) > 0);
    CHECK(countEvents(*state, EventType::LevelUp) == 1);
}

TEST_CASE("a death later in a frame that opened with a bomb still sends its event") {
    // The app clears the list once a frame, and a frame can run several steps.
    auto state = bombInHugeCrowd();
    sim::step(*state, InputFrame{});
    REQUIRE(countEvents(*state, EventType::BombBlast) == 1);

    // That many kills overflow the gem pool into a level-up. Skip it to carry on playing.
    state->run.phase = RunPhase::Playing;
    state->run.pendingLevels = 0;
    state->player.hp = 1.0f;
    state->player.invulnerable = 0.0f;
    REQUIRE(enemies::spawn(*state, EnemyType::Walker, state->player.pos + Vec2{5.0f, 0.0f}, false));
    sim::step(*state, InputFrame{});

    REQUIRE(state->run.phase == RunPhase::Dead);
    CHECK(countEvents(*state, EventType::PlayerDied) == 1);
}

// --- The boss can't be outrun ----------------------------------------------------

TEST_CASE("the fastest possible player can't outrun the boss in any phase") {
    const float hpFractions[] = {1.0f, 0.5f, 0.2f};
    for (const float hpFraction : hpFractions) {
        auto state = makeRun();
        state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 1;
        state->run.nextEliteEvent = 1000;
        // Speed passive at its top level and no weapons, so nothing kills the boss or
        // gives XP that would stop the run for a level-up.
        state->player.weaponCount = 0;
        state->player.passives[0] = PassiveSlot{PassiveId::MoveSpeed, kMaxLevelPerItem};
        state->player.passiveCount = 1;
        upgrades::recomputeStats(*state);
        sim::step(*state, InputFrame{});
        REQUIRE(findBoss(*state) != nullptr);

        InputFrame flee;
        flee.moveX = 1.0f;
        flee.dash = true;  // Ignored while on cooldown, so this dashes whenever it can.

        const int steps = 180 * 60;
        float widestEarly = 0.0f;
        float widestLate = 0.0f;
        int stepsInRange = 0;
        for (int i = 0; i < steps; ++i) {
            state->player.hp = state->player.maxHp;
            // Holding the boss's HP keeps it in the phase under test. Phase only goes up.
            for (Enemy& enemy : state->enemies) {
                if (enemy.type == EnemyType::Boss) enemy.hp = enemy.maxHp * hpFraction;
            }
            sim::step(*state, flee);
            state->events.clear();
            const Enemy* boss = findBoss(*state);
            REQUIRE(boss != nullptr);
            const float gap = distance(boss->pos, state->player.pos);
            if (gap < balance::kWeaponRange) stepsInRange++;
            if (i >= steps / 6) {  // The boss's walk in from its spawn point doesn't count.
                float& widest = i < steps * 7 / 12 ? widestEarly : widestLate;
                widest = std::max(widest, gap);
            }
        }
        MESSAGE("phase at HP ", hpFraction, ": widest gap ", widestEarly, " then ", widestLate, ", in range ", stepsInRange, " of ", steps);
        // The gap goes up and down with each attack, but must not grow over time. So the
        // boss keeps coming back into range, and running can't stall the fight forever.
        CHECK(widestLate <= widestEarly + 50.0f);
        CHECK(widestLate < balance::kWeaponRange * 1.6f);
        CHECK(stepsInRange > steps / 4);
    }
}

// --- Numbers that moved to the balance file --------------------------------------

TEST_CASE("the numbers moved to the balance file are the ones in use") {
    SUBCASE("player hit flash") {
        auto state = makeRun();
        pickups::spawnPickup(*state, Vec2{9000.0f, 0.0f}, PickupType::Heal);
        REQUIRE(enemies::spawn(*state, EnemyType::Walker, state->player.pos + Vec2{5.0f, 0.0f}, false));
        enemies::update(*state);
        CHECK(state->player.hitFlash == doctest::Approx(balance::kPlayerHitFlashSeconds));
    }
    SUBCASE("elite bloater crawlers") {
        auto state = makeRun();
        Enemy* bloater = enemies::spawn(*state, EnemyType::Bloater, Vec2{500.0f, 0.0f}, true);
        REQUIRE(bloater != nullptr);
        bloater->hp = 0.0f;
        enemies::removeDead(*state);
        CHECK(state->enemies.count == balance::kCrawlersPerBloater * balance::kElite_CrawlerMult);
    }
    SUBCASE("gem collect distance") {
        const float edge = balance::kPlayerRadius + balance::kGemCollectMargin;
        auto state = makeRun();
        pickups::dropGem(*state, state->player.pos + Vec2{edge + 1.0f, 0.0f}, 1);
        pickups::update(*state);
        CHECK(state->gems.count == 1);
        auto inside = makeRun();
        pickups::dropGem(*inside, inside->player.pos + Vec2{edge - 1.0f, 0.0f}, 1);
        pickups::update(*inside);
        CHECK(inside->gems.count == 0);
    }
    SUBCASE("gem merge interval") {
        auto state = makeRun();
        const int gems = balance::kGemMergeStartCount + 1;
        for (int i = 0; i < gems; ++i) pickups::dropGem(*state, Vec2{5000.0f, 5000.0f}, 1);
        state->run.tick = balance::kGemMergeIntervalSteps + 1;
        pickups::update(*state);
        CHECK(state->gems.count == gems);
        state->run.tick = balance::kGemMergeIntervalSteps * 2;
        pickups::update(*state);
        CHECK(state->gems.count == 1);
    }
}
