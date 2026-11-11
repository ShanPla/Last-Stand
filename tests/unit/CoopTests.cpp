#include "doctest.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Maps/Layout/Layout.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Pickups/Pickups.h"
#include "components/Player/Player.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <iterator>
#include <memory>

// Co-op: two to four players in one run, each with their own build, sharing the zombies.

using namespace ls;

namespace {

std::unique_ptr<GameState> coopRun(int players, uint64_t seed = 9, maps::MapId map = maps::MapId::City) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, map, RunMode::Story, players);
    return state;
}

// No waves, elites or timed crates, so only what a test puts in the world is there.
void hush(GameState& state) {
    for (float& owed : state.run.waveAccumulators) owed = 0.0f;
    state.run.nextEliteEvent = 1 << 20;
    state.events.clear();
}

// Steps with every player standing still, unless inputs are given, and nothing new spawning.
void stepQuiet(GameState& state, int steps = 1, const InputFrame* inputs = nullptr) {
    InputFrame idle[kMaxPlayers];
    for (int i = 0; i < steps; ++i) {
        hush(state);
        sim::step(state, inputs ? inputs : idle, state.playerCount());
    }
}

// No weapons and no level-ups, so nothing changes but what a test does.
void disarm(GameState& state) {
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        state.players[slot].weaponCount = 0;
        state.players[slot].xpToNext = 1 << 30;
    }
}

// A place with no car within 300 units, so tests can put players and zombies anywhere near it.
Vec2 openGround(GameState& state) {
    for (int k = 0; k < 400; ++k) {
        const Vec2 candidate{static_cast<float>(k) * 173.0f, static_cast<float>(k % 7) * 211.0f};
        for (int slot = 0; slot < state.playerCount(); ++slot) state.players[slot].pos = candidate;
        obstacles::update(state);
        bool clear = true;
        for (float y = -300.0f; y <= 300.0f && clear; y += 25.0f) {
            for (float x = -300.0f; x <= 300.0f && clear; x += 25.0f) clear = !obstacles::overlapsCar(state, candidate + Vec2{x, y}, 20.0f);
        }
        if (clear) return candidate;
    }
    FAIL("no open ground found");
    return Vec2{};
}

void place(GameState& state, int slot, Vec2 pos) {
    state.players[slot].pos = pos;
    state.players[slot].prevPos = pos;
}

int countEvents(const GameState& state, EventType type, int player) {
    int found = 0;
    for (const SimEvent& event : state.events) found += event.type == type && event.player == player ? 1 : 0;
    return found;
}

}  // namespace

TEST_CASE("a co-op run has its own player count, and every player starts with their own build") {
    for (const int players : {1, 2, 3, 4}) {
        auto state = coopRun(players);
        CHECK(state->playerCount() == players);
        CHECK(state->isCoop() == (players > 1));
        for (int slot = 0; slot < players; ++slot) {
            const Player& player = state->players[slot];
            CHECK(player.hp == player.maxHp);
            CHECK(player.weaponCount == 1);
            CHECK(player.weapons[0].id == WeaponId::Rifle);
            CHECK(player.rerollsLeft == balance::kStartingRerolls);
            CHECK_FALSE(player.downed);
            CHECK_FALSE(obstacles::overlapsCar(*state, player.pos, balance::kPlayerRadius));
        }
        // A team starts close together, but not on top of each other.
        for (int a = 0; a < players; ++a) {
            for (int b = a + 1; b < players; ++b) CHECK(distance(state->players[a].pos, state->players[b].pos) > 1.0f);
        }
    }
    auto tooMany = coopRun(9);
    CHECK(tooMany->playerCount() == kMaxPlayers);
}

TEST_CASE("each player moves by their own input, and a player with no input given stands still") {
    auto state = coopRun(3);
    disarm(*state);
    openGround(*state);
    const Vec2 start1 = state->players[1].pos;
    const Vec2 start2 = state->players[2].pos;
    InputFrame inputs[2];
    inputs[0].moveX = 1.0f;
    inputs[1].moveY = -1.0f;
    for (int i = 0; i < 30; ++i) {
        hush(*state);
        sim::step(*state, inputs, 2);  // Only two inputs for three players.
    }
    CHECK(state->players[1].pos.y < start1.y - 50.0f);
    CHECK(state->players[2].pos.x == start2.x);
    CHECK(state->players[2].pos.y == start2.y);
}

TEST_CASE("zombies chase the nearest standing player, and equal distances go to the lower slot") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground + Vec2{-250.0f, 0.0f});
    place(*state, 1, ground + Vec2{250.0f, 0.0f});
    obstacles::update(*state);

    // The rule itself, with exact numbers.
    CHECK(nearestStanding(*state, ground + Vec2{-100.0f, 40.0f}) == 0);
    CHECK(nearestStanding(*state, ground + Vec2{100.0f, 40.0f}) == 1);
    CHECK(nearestStanding(*state, ground + Vec2{0.0f, 123.0f}) == 0);  // A tie.

    Enemy* nearZero = enemies::spawn(*state, EnemyType::Walker, ground + Vec2{-200.0f, 120.0f}, false);
    Enemy* nearOne = enemies::spawn(*state, EnemyType::Brute, ground + Vec2{210.0f, -120.0f}, false);
    REQUIRE(nearZero);
    REQUIRE(nearOne);
    CHECK(nearZero->target == 0);
    CHECK(nearOne->target == 1);
    const uint32_t oneId = nearOne->id;
    const float before = distance(nearOne->pos, state->players[1].pos);
    stepQuiet(*state, 30);
    for (const Enemy& enemy : state->enemies) {
        if (enemy.id != oneId) continue;
        CHECK(enemy.target == 1);
        CHECK(distance(enemy.pos, state->players[1].pos) < before - 5.0f);
    }
}

TEST_CASE("a zombie looks again for the nearest player every few steps, and at once when its target goes down") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{1100.0f, 0.0f});
    obstacles::update(*state);
    Enemy* zombie = enemies::spawn(*state, EnemyType::Walker, ground + Vec2{280.0f, 200.0f}, false);
    REQUIRE(zombie);
    zombie->maxHp = zombie->hp = 1.0e6f;
    const uint32_t id = zombie->id;
    const auto find = [&]() -> Enemy& {
        for (Enemy& enemy : state->enemies) {
            if (enemy.id == id) return enemy;
        }
        FAIL("the zombie is gone");
        return state->enemies[0];
    };
    CHECK(find().target == 0);

    // Player 1 walks up close behind the zombie. Within one look-again period it switches.
    place(*state, 1, find().pos + Vec2{60.0f, 0.0f});
    stepQuiet(*state, static_cast<int>(balance::kZombieRetargetSteps));
    CHECK(find().target == 1);

    // Player 1 goes down: the zombie turns back to player 0 on the very next step.
    state->players[1].hp = 0.0f;
    stepQuiet(*state);
    REQUIRE(state->players[1].downed);
    stepQuiet(*state);
    CHECK(find().target == 0);
    // And it walks away from the downed player toward the standing one.
    const float before = distance(find().pos, state->players[0].pos);
    stepQuiet(*state, 20);
    CHECK(distance(find().pos, state->players[0].pos) < before - 5.0f);
    CHECK(state->players[1].hp == 0.0f);  // Nobody can hurt a downed player.
}

TEST_CASE("a zombie hurts every player it touches, not only the one it chases") {
    auto state = coopRun(3);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{30.0f, 0.0f});
    place(*state, 2, ground + Vec2{600.0f, 0.0f});
    obstacles::update(*state);
    // Between players 0 and 1, touching both, so it can chase only one of them.
    Enemy* zombie = enemies::spawn(*state, EnemyType::Brute, ground + Vec2{15.0f, 0.0f}, false);
    REQUIRE(zombie);
    stepQuiet(*state);
    CHECK(state->players[0].hp < state->players[0].maxHp);
    CHECK(state->players[1].hp < state->players[1].maxHp);
    CHECK(countEvents(*state, EventType::PlayerHit, 0) == 1);
    CHECK(countEvents(*state, EventType::PlayerHit, 1) == 1);
    CHECK(state->players[2].hp == state->players[2].maxHp);
}

TEST_CASE("a player at 0 HP is downed, and the run ends only when the whole team is down at once") {
    auto state = coopRun(3);
    disarm(*state);
    state->players[1].hp = 0.0f;
    stepQuiet(*state);
    CHECK(state->players[1].downed);
    CHECK(countEvents(*state, EventType::PlayerDowned, 1) == 1);
    CHECK(state->run.phase == RunPhase::Playing);

    state->players[2].hp = 0.0f;
    stepQuiet(*state);
    CHECK(state->players[2].downed);
    CHECK(state->run.phase == RunPhase::Playing);  // Player 0 still stands.

    state->players[0].hp = -3.0f;
    stepQuiet(*state);
    CHECK(state->run.phase == RunPhase::Dead);
    bool died = false;
    for (const SimEvent& event : state->events) died = died || event.type == EventType::PlayerDied;
    CHECK(died);

    // Two going down in the same step ends a two-player run.
    auto pair = coopRun(2);
    pair->players[0].hp = 0.0f;
    pair->players[1].hp = 0.0f;
    stepQuiet(*pair);
    CHECK(pair->run.phase == RunPhase::Dead);
}

TEST_CASE("a downed player can't move, fire, collect or be hurt") {
    auto state = coopRun(2);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{1500.0f, 0.0f});
    obstacles::update(*state);
    state->players[0].hp = 0.0f;
    stepQuiet(*state);
    REQUIRE(state->players[0].downed);
    const Vec2 lying = state->players[0].pos;

    enemies::spawn(*state, EnemyType::Walker, lying + Vec2{150.0f, 0.0f}, false);
    pickups::dropGem(*state, lying, 50);
    pickups::spawnPickup(*state, lying + Vec2{0.0f, 5.0f}, PickupType::Heal);
    InputFrame inputs[2];
    inputs[0].moveX = 1.0f;
    inputs[0].dash = true;
    stepQuiet(*state, 20, inputs);
    CHECK(state->players[0].pos.x == lying.x);
    CHECK(state->players[0].pos.y == lying.y);
    CHECK(state->players[0].xp == 0);
    CHECK(state->players[0].hp == 0.0f);
    CHECK(state->pickups.count == 1);
    CHECK(countEvents(*state, EventType::WeaponFired, 0) == 0);
    for (const Projectile& shot : state->projectiles) CHECK(shot.owner != 0);
    // Enemy bullets fly over a downed player.
    enemies::fireBullet(*state, lying - Vec2{30.0f, 0.0f}, Vec2{600.0f, 0.0f}, 10.0f);
    stepQuiet(*state, 10);
    CHECK(state->players[0].hp == 0.0f);
}

TEST_CASE("a teammate standing close for 3 seconds of contact time revives a downed player") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 1, ground);
    place(*state, 0, ground + Vec2{900.0f, 0.0f});
    obstacles::update(*state);
    state->players[1].hp = 0.0f;
    stepQuiet(*state);
    REQUIRE(state->players[1].downed);

    // Just outside the radius: no progress at all.
    place(*state, 0, ground + Vec2{balance::kReviveRadius + 4.0f, 0.0f});
    stepQuiet(*state, 240);
    CHECK(state->players[1].downed);
    CHECK(state->players[1].reviveProgress == 0.0f);

    // Inside it, for a little under half the time, then away, then back. The time adds up.
    const int needed = static_cast<int>(std::lround(balance::kReviveSeconds / kDt));
    place(*state, 0, ground + Vec2{balance::kReviveRadius - 4.0f, 0.0f});
    stepQuiet(*state, needed / 2);
    CHECK(state->players[1].reviveProgress == doctest::Approx(balance::kReviveSeconds / 2.0f).epsilon(0.02));
    place(*state, 0, ground + Vec2{600.0f, 0.0f});
    stepQuiet(*state, 100);
    CHECK(state->players[1].downed);
    place(*state, 0, ground + Vec2{0.0f, balance::kReviveRadius - 4.0f});
    stepQuiet(*state, needed - needed / 2 - 1);
    CHECK(state->players[1].downed);  // One step short.
    stepQuiet(*state);
    CHECK_FALSE(state->players[1].downed);
    CHECK(countEvents(*state, EventType::PlayerRevived, 1) == 1);
    CHECK(state->players[1].hp == doctest::Approx(state->players[1].maxHp * balance::kReviveHpShare));
    CHECK(state->players[1].invulnerable > 0.0f);
    CHECK(state->players[1].reviveProgress == 0.0f);

    // Back up, they move and fight again.
    InputFrame inputs[2];
    inputs[1].moveX = 1.0f;
    const float x = state->players[1].pos.x;
    stepQuiet(*state, 10, inputs);
    CHECK(state->players[1].pos.x > x + 10.0f);
}

TEST_CASE("co-op level-up doesn't pause: cards open, weapons hold fire, and the safety lasts at most 5 seconds") {
    auto state = coopRun(2);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{120.0f, 0.0f});
    obstacles::update(*state);
    // A tough zombie in range of both, so both would fire.
    Enemy* dummy = enemies::spawn(*state, EnemyType::Brute, ground + Vec2{60.0f, 300.0f}, false);
    REQUIRE(dummy);
    dummy->maxHp = dummy->hp = 1.0e9f;
    dummy->speed = 0.0f;
    dummy->contactDamage = 0.0f;

    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    const uint32_t tick = state->run.tick;
    stepQuiet(*state);
    CHECK(state->run.phase == RunPhase::Playing);
    CHECK(state->run.tick == tick + 1);
    REQUIRE(state->players[0].cardsOpen);
    CHECK_FALSE(state->players[1].cardsOpen);
    CHECK(state->players[0].offerCount > 0);
    CHECK(state->players[0].cardShield == doctest::Approx(balance::kCoopCardShieldSeconds));
    CHECK(countEvents(*state, EventType::LevelUp, 0) == 1);

    // The run goes on: time passes, player 1 fires, player 0 holds fire and can't be hurt.
    int firedByZero = 0;
    int firedByOne = 0;
    for (int i = 0; i < 120; ++i) {
        stepQuiet(*state);
        firedByZero += countEvents(*state, EventType::WeaponFired, 0);
        firedByOne += countEvents(*state, EventType::WeaponFired, 1);
        player::damage(*state, 0, 30.0f);
    }
    CHECK(state->run.tick == tick + 121);
    CHECK(firedByZero == 0);
    CHECK(firedByOne > 0);
    CHECK(state->players[0].hp == state->players[0].maxHp);
    for (const Projectile& shot : state->projectiles) CHECK(shot.owner == 1);

    // After 5 seconds with the cards still open, the safety is gone.
    const int shieldSteps = static_cast<int>(std::lround(balance::kCoopCardShieldSeconds / kDt));
    stepQuiet(*state, shieldSteps - 121);
    player::damage(*state, 0, 30.0f);
    CHECK(state->players[0].hp == state->players[0].maxHp);  // The last moment of the 5 seconds.
    stepQuiet(*state);
    REQUIRE(state->players[0].cardsOpen);
    CHECK(state->players[0].cardShield == 0.0f);
    player::damage(*state, 0, 30.0f);
    CHECK(state->players[0].hp < state->players[0].maxHp);

    // Taking a card closes them, and the weapons fire again.
    InputFrame inputs[2];
    inputs[0].chooseCard = 0;
    stepQuiet(*state, 1, inputs);
    CHECK_FALSE(state->players[0].cardsOpen);
    CHECK(state->players[0].pendingLevels == 0);
    int firedAfter = 0;
    for (int i = 0; i < 120; ++i) {
        stepQuiet(*state);
        firedAfter += countEvents(*state, EventType::WeaponFired, 0);
    }
    CHECK(firedAfter > 0);
}

TEST_CASE("co-op: several levels at once share one card safety, and a reroll is per player") {
    auto state = coopRun(2);
    disarm(*state);
    state->players[1].xpToNext = balance::xpForLevel(1);
    upgrades::addXp(*state, 1, balance::xpForLevel(1) + balance::xpForLevel(2) + balance::xpForLevel(3));
    stepQuiet(*state);
    REQUIRE(state->players[1].cardsOpen);
    CHECK(state->players[1].pendingLevels == 3);
    stepQuiet(*state, 60);
    const float left = state->players[1].cardShield;
    CHECK(left == doctest::Approx(balance::kCoopCardShieldSeconds - 1.0f).epsilon(0.01));

    InputFrame inputs[2];
    inputs[1].reroll = true;
    stepQuiet(*state, 1, inputs);
    CHECK(state->players[1].rerollsLeft == balance::kStartingRerolls - 1);
    CHECK(state->players[0].rerollsLeft == balance::kStartingRerolls);
    inputs[1] = InputFrame{};
    inputs[1].chooseCard = 0;
    stepQuiet(*state, 1, inputs);
    CHECK(state->players[1].cardsOpen);  // The next level's cards follow at once.
    CHECK(state->players[1].pendingLevels == 2);
    // The safety carried on from where it was, so a pile of levels can't stretch it.
    CHECK(state->players[1].cardShield < left);
    // A reroll or card from a player without cards does nothing.
    InputFrame stray[2];
    stray[0].reroll = true;
    stray[0].chooseCard = 1;
    stepQuiet(*state, 1, stray);
    CHECK(state->players[0].rerollsLeft == balance::kStartingRerolls);
    CHECK(state->players[0].weaponCount == 0);
}

TEST_CASE("single-player level-up still pauses the run, exactly as before co-op") {
    auto state = coopRun(1);
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    stepQuiet(*state);
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    CHECK_FALSE(state->firstPlayer().cardsOpen);
    const uint32_t tick = state->run.tick;
    stepQuiet(*state, 30);
    CHECK(state->run.tick == tick);
    InputFrame pick;
    pick.chooseCard = 0;
    sim::step(*state, pick);
    CHECK(state->run.phase == RunPhase::Playing);
}

TEST_CASE("gems give XP to whoever collects them, and are pulled by the nearest player in range") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{800.0f, 0.0f});
    obstacles::update(*state);
    pickups::dropGem(*state, state->players[1].pos + Vec2{30.0f, 0.0f}, 3);
    stepQuiet(*state, 30);
    CHECK(state->players[1].xp == 3);
    CHECK(state->players[0].xp == 0);
    CHECK(state->gems.count == 0);

    // Two players near one gem: the one in range and nearer pulls it in and gets it.
    place(*state, 1, ground + Vec2{200.0f, 0.0f});
    pickups::dropGem(*state, ground + Vec2{140.0f, 0.0f}, 4);
    stepQuiet(*state, 60);
    CHECK(state->players[1].xp == 7);
    CHECK(state->players[0].xp == 0);
}

TEST_CASE("heal and ammo go to the player who picks them up") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{700.0f, 0.0f});
    obstacles::update(*state);
    for (int slot = 0; slot < 2; ++slot) {
        state->players[slot].hp = 20.0f;
        weapons::grant(*state, slot, WeaponId::Smg);
        state->players[slot].weapons[0].reserve = 0;
    }
    pickups::spawnPickup(*state, state->players[1].pos, PickupType::Heal);
    pickups::spawnPickup(*state, state->players[1].pos + Vec2{0.0f, 4.0f}, PickupType::Ammo);
    // The guns must not fire, so the reserves only change by the crate.
    state->players[0].cardsOpen = true;
    state->players[1].cardsOpen = true;
    stepQuiet(*state);
    CHECK(state->players[1].hp > 20.0f);
    CHECK(state->players[0].hp == 20.0f);
    CHECK(state->players[1].weapons[0].reserve > 0);
    CHECK(state->players[0].weapons[0].reserve == 0);
    CHECK(countEvents(*state, EventType::PickupCollected, 1) == 2);
}

TEST_CASE("magnet and bomb help the whole team") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{2000.0f, 0.0f});
    obstacles::update(*state);

    // A magnet picked up by player 0 pulls every gem, and each flies to the nearest player.
    pickups::dropGem(*state, state->players[0].pos + Vec2{400.0f, 0.0f}, 5);
    pickups::dropGem(*state, state->players[1].pos + Vec2{-400.0f, 0.0f}, 6);
    pickups::spawnPickup(*state, state->players[0].pos, PickupType::Magnet);
    stepQuiet(*state);
    for (const Gem& gem : state->gems) CHECK(gem.attracted);
    stepQuiet(*state, 120);
    CHECK(state->gems.count == 0);
    CHECK(state->players[0].xp == 5);
    CHECK(state->players[1].xp == 6);

    // A bomb picked up by player 0 also clears the zombies around player 1, far out of its reach.
    const Vec2 far = state->players[1].pos + Vec2{0.0f, 300.0f};
    REQUIRE(distance(far, state->players[0].pos) > balance::kBombRadius * 2.0f);
    Enemy* zombie = enemies::spawn(*state, EnemyType::Walker, far, false);
    REQUIRE(zombie);
    const int kills = state->run.kills;
    pickups::spawnPickup(*state, state->players[0].pos, PickupType::Bomb);
    stepQuiet(*state);
    CHECK(state->run.kills == kills + 1);
}

TEST_CASE("more players bring more zombies and a tougher boss, by the factors in the balance table") {
    // Spawn rate: one step of the wave table at the same moment, solo and in co-op.
    auto solo = coopRun(1);
    for (const int players : {2, 3, 4}) {
        CAPTURE(players);
        auto team = coopRun(players);
        for (GameState* state : {solo.get(), team.get()}) {
            state->run.tick = 90 * 60;
            for (float& owed : state->run.waveAccumulators) owed = 0.0f;
            waves::update(*state);
        }
        int rows = 0;
        for (size_t i = 0; i + 1 < std::size(solo->run.waveAccumulators); ++i) {
            const float one = solo->run.waveAccumulators[i];
            if (one <= 0.0f) continue;
            ++rows;
            CHECK(team->run.waveAccumulators[i] == doctest::Approx(one * balance::coopSpawnRate(players)));
        }
        CHECK(rows > 0);
        // Boss health: every player has the same build here, so only the team factor differs.
        CHECK(waves::bossHpForBuild(*team) == doctest::Approx(waves::bossHpForBuild(*solo) * balance::coopBossHp(players)));
        // Zombie health.
        Enemy* mine = enemies::spawn(*team, EnemyType::Brute, Vec2{900.0f, 0.0f}, false);
        Enemy* theirs = enemies::spawn(*solo, EnemyType::Brute, Vec2{900.0f, 0.0f}, false);
        REQUIRE(mine);
        REQUIRE(theirs);
        CHECK(mine->maxHp == doctest::Approx(theirs->maxHp * balance::coopZombieHp(players)));
        solo->enemies.clear();
    }
    CHECK(balance::coopSpawnRate(2) == doctest::Approx(1.6f));
    CHECK(balance::coopSpawnRate(4) == doctest::Approx(2.8f));
}

TEST_CASE("the boss fits itself to the team's average build") {
    // A built-up player: three more guns, all at the top level. Strong enough to lift the boss
    // above its lowest health.
    const auto buildUp = [](GameState& state, int slot) {
        for (const WeaponId id : {WeaponId::Smg, WeaponId::Sniper, WeaponId::Rockets}) weapons::grant(state, slot, id);
        for (int i = 0; i < state.players[slot].weaponCount; ++i) state.players[slot].weapons[i].level = kMaxLevelPerItem;
    };
    auto weak = coopRun(2);
    auto mixed = coopRun(2);
    buildUp(*mixed, 1);
    auto strong = coopRun(2);
    buildUp(*strong, 0);
    buildUp(*strong, 1);
    CHECK(waves::bossHpForBuild(*weak) < waves::bossHpForBuild(*mixed));
    CHECK(waves::bossHpForBuild(*mixed) < waves::bossHpForBuild(*strong));
}

TEST_CASE("boss attacks aim at the boss's target, and a boss zone hurts every player inside it") {
    auto state = coopRun(2);
    disarm(*state);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground + Vec2{-700.0f, 0.0f});
    place(*state, 1, ground + Vec2{300.0f, 0.0f});
    obstacles::update(*state);
    Enemy* boss = enemies::spawn(*state, EnemyType::Boss, ground + Vec2{500.0f, 0.0f}, false);
    REQUIRE(boss);
    CHECK(boss->target == 1);
    enemies::startBossAttack(*state, *boss, balance::BossAttack::Circles);
    REQUIRE(state->zones.count > 0);
    // The first circle lands right on the target.
    CHECK(state->zones[0].a.x == state->players[1].pos.x);
    CHECK(state->zones[0].a.y == state->players[1].pos.y);

    // A circle under both players hurts both when it goes off.
    state->zones.clear();
    place(*state, 0, ground + Vec2{-20.0f, 0.0f});
    place(*state, 1, ground + Vec2{20.0f, 0.0f});
    Zone* zone = state->zones.add();
    zone->kind = ZoneKind::BossCircle;
    zone->a = zone->b = ground;
    zone->radius = 100.0f;
    zone->life = kDt;
    zone->damage = 10.0f;
    zone->hostile = true;
    state->enemies.clear();
    stepQuiet(*state);
    CHECK(state->players[0].hp < state->players[0].maxHp);
    CHECK(state->players[1].hp < state->players[1].maxHp);
}

TEST_CASE("events about one player name that player's slot") {
    auto state = coopRun(2);
    const Vec2 ground = openGround(*state);
    place(*state, 0, ground);
    place(*state, 1, ground + Vec2{600.0f, 0.0f});
    obstacles::update(*state);
    Enemy* target = enemies::spawn(*state, EnemyType::Brute, state->players[1].pos + Vec2{0.0f, 200.0f}, false);
    REQUIRE(target);
    target->maxHp = target->hp = 1.0e9f;
    state->players[0].weaponCount = 0;  // Only player 1 can fire.
    int named = 0;
    for (int i = 0; i < 60; ++i) {
        stepQuiet(*state);
        for (const SimEvent& event : state->events) {
            if (event.type != EventType::WeaponFired) continue;
            CHECK(event.player == 1);
            ++named;
        }
    }
    CHECK(named > 0);
    // Damage is credited to the team and to the player who dealt it.
    const int rifle = static_cast<int>(WeaponId::Rifle);
    CHECK(state->players[1].damageByWeapon[rifle] > 0.0f);
    CHECK(state->players[0].damageByWeapon[rifle] == 0.0f);
    CHECK(state->run.damageByWeapon[rifle] == state->players[1].damageByWeapon[rifle]);
}

TEST_CASE("every player has the cars around them, however far apart they are") {
    for (const maps::MapId map : {maps::MapId::City, maps::MapId::Wasteland}) {
        auto state = coopRun(2, 41, map);
        // Player 1 goes 30 tiles away, far outside player 0's window.
        place(*state, 1, Vec2{maps::kChunkWorldSize * 30.5f, maps::kChunkWorldSize * 30.5f});
        obstacles::update(*state);
        // Every car of every tile in player 1's window is solid.
        const int centerX = maps::tileOf(state->players[1].pos.x);
        const int centerY = maps::tileOf(state->players[1].pos.y);
        int cars = 0;
        for (int y = centerY - kObstacleWindow / 2; y <= centerY + kObstacleWindow / 2; ++y) {
            for (int x = centerX - kObstacleWindow / 2; x <= centerX + kObstacleWindow / 2; ++x) {
                maps::Obstacle items[maps::kMaxObstaclesPerTile];
                const int count = maps::obstaclesInTile(map, 41, x, y, items, maps::kMaxObstaclesPerTile);
                for (int i = 0; i < count; ++i) {
                    if (maps::tileOf(items[i].center.x) != x || maps::tileOf(items[i].center.y) != y) continue;
                    CHECK(obstacles::overlapsCar(*state, items[i].center, 1.0f));
                    ++cars;
                }
            }
        }
        CHECK(cars > 0);
        // Each window holds exactly what a fresh look at the map gives.
        for (int p = 0; p < 2; ++p) {
            for (const ObstacleTile& tile : state->obstacleCache.windows[p].tiles) {
                REQUIRE(tile.valid);
                maps::Obstacle items[maps::kMaxObstaclesPerTile];
                const int count = maps::obstaclesInTile(map, 41, tile.tileX, tile.tileY, items, maps::kMaxObstaclesPerTile);
                CHECK(tile.count == count);
            }
        }
        // With only one player, a place that far away has no solid cars.
        auto solo = coopRun(1, 41, map);
        int solid = 0;
        maps::Obstacle items[maps::kMaxObstaclesPerTile];
        const int count = maps::obstaclesInTile(map, 41, centerX, centerY, items, maps::kMaxObstaclesPerTile);
        for (int i = 0; i < count; ++i) solid += obstacles::overlapsCar(*solo, items[i].center, 1.0f) ? 1 : 0;
        CHECK(solid == 0);
    }
}

TEST_CASE("a crushed car is flat in every player's window") {
    auto state = coopRun(2, 41, maps::MapId::Wasteland);
    // Both players stand in the same place, so their windows hold the same tiles.
    place(*state, 1, state->players[0].pos);
    obstacles::update(*state);
    const int tileX = maps::tileOf(state->players[0].pos.x);
    const int tileY = maps::tileOf(state->players[0].pos.y);
    maps::Obstacle car{};
    bool found = false;
    for (int r = 0; r <= 2 && !found; ++r) {
        for (int y = tileY - r; y <= tileY + r && !found; ++y) {
            for (int x = tileX - r; x <= tileX + r && !found; ++x) {
                maps::Obstacle items[maps::kMaxObstaclesPerTile];
                const int count = maps::obstaclesInTile(maps::MapId::Wasteland, 41, x, y, items, maps::kMaxObstaclesPerTile);
                for (int i = 0; i < count && !found; ++i) {
                    if (maps::tileOf(items[i].center.x) == x && maps::tileOf(items[i].center.y) == y) {
                        car = items[i];
                        found = true;
                    }
                }
            }
        }
    }
    REQUIRE(found);
    REQUIRE(obstacles::overlapsCar(*state, car.center, 1.0f));
    obstacles::crushUnder(*state, car.center, 1.0f);
    CHECK_FALSE(obstacles::overlapsCar(*state, car.center, 1.0f));
    for (const ObstacleWindow& window : state->obstacleCache.windows) {
        for (const ObstacleTile& tile : window.tiles) {
            if (!tile.valid) continue;
            for (int i = 0; i < tile.count; ++i) CHECK_FALSE(maps::isCar(maps::CarId{car.texelX, car.texelY}, tile.items[i]));
        }
    }
}

TEST_CASE("in a single-player run the hash ignores every co-op field, so solo fingerprints stay the same") {
    auto state = coopRun(1);
    for (int i = 0; i < 600; ++i) {
        InputFrame input;
        input.moveX = std::cos(static_cast<float>(i) * 0.01f);
        input.moveY = std::sin(static_cast<float>(i) * 0.01f);
        if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        sim::step(*state, input);
    }
    // At least one of each, so every co-op field below has an item to sit on.
    enemies::spawn(*state, EnemyType::Walker, Vec2{900.0f, 0.0f}, false);
    state->projectiles.add()->life = 1.0f;
    state->zones.add()->life = 1.0f;
    const uint64_t before = sim::stateHash(*state);
    Player& player = state->firstPlayer();
    player.damageByWeapon[0] += 50.0f;
    player.reviveProgress = 1.0f;
    player.cardShield = 2.0f;
    player.downed = true;
    player.cardsOpen = true;
    state->enemies[0].target = 1;
    state->projectiles[0].owner = 1;
    state->players[1].hp = 3.0f;  // A slot that isn't in the run.
    state->zones[0].owner = 1;
    CHECK(sim::stateHash(*state) == before);

    // The same fields do count in a co-op run.
    auto team = coopRun(2);
    stepQuiet(*team, 60);
    const uint64_t teamBefore = sim::stateHash(*team);
    team->players[1].hp -= 1.0f;
    CHECK(sim::stateHash(*team) != teamBefore);
    team->players[1].hp += 1.0f;
    CHECK(sim::stateHash(*team) == teamBefore);
    team->players[0].cardShield = 1.0f;
    CHECK(sim::stateHash(*team) != teamBefore);
}
