#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Pickups/Pickups.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <iterator>
#include <memory>
#include <utility>
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
    CHECK(length(state->firstPlayer().pos) == doctest::Approx(balance::kPlayerSpeed * kDt).epsilon(0.001));
}

TEST_CASE("an enemy touching the player deals damage, then invulnerability blocks repeats") {
    auto state = makeRun();
    enemies::spawn(*state, EnemyType::Walker, Vec2{1.0f, 0.0f}, false);
    const float startHp = state->firstPlayer().hp;
    sim::step(*state, InputFrame{});
    const float afterFirst = state->firstPlayer().hp;
    CHECK(afterFirst < startHp);
    sim::step(*state, InputFrame{});
    CHECK(state->firstPlayer().hp == afterFirst);
}

TEST_CASE("armor reduces damage but never below the minimum") {
    auto state = makeRun();
    state->firstPlayer().stats.armor = 100.0f;
    enemies::spawn(*state, EnemyType::Walker, Vec2{1.0f, 0.0f}, false);
    const float startHp = state->firstPlayer().hp;
    sim::step(*state, InputFrame{});
    CHECK(startHp - state->firstPlayer().hp == doctest::Approx(balance::kMinDamageTaken));
}

TEST_CASE("a shot is used up when its pierce runs out, and not before") {
    // No weapons, so the only shot is the one placed by hand. Two walkers stand in its
    // path, far enough apart that it cannot touch both at once.
    const auto fire = [](int pierce, float& firstHp, float& secondHp, int& shotsLeft) {
        auto state = makeRun();
        state->firstPlayer().weaponCount = 0;
        state->firstPlayer().maxHp = 1.0e6f;
        state->firstPlayer().hp = 1.0e6f;
        state->firstPlayer().xpToNext = 1 << 30;
        Enemy* first = enemies::spawn(*state, EnemyType::Walker, Vec2{300.0f, 0.0f}, false);
        Enemy* second = enemies::spawn(*state, EnemyType::Walker, Vec2{360.0f, 0.0f}, false);
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        const float startFirst = first->maxHp;
        const float startSecond = second->maxHp;
        Projectile* shot = state->projectiles.add();
        REQUIRE(shot != nullptr);
        shot->pos = Vec2{280.0f, 0.0f};
        shot->prevPos = shot->pos;
        shot->vel = Vec2{600.0f, 0.0f};
        shot->damage = 1.0f;
        shot->radius = balance::kBulletRadius;
        shot->life = 2.0f;
        shot->pierce = static_cast<int16_t>(pierce);
        for (int i = 0; i < 30; ++i) sim::step(*state, InputFrame{});
        firstHp = startFirst - state->enemies[0].hp;
        secondHp = startSecond - state->enemies[1].hp;
        shotsLeft = state->projectiles.count;
    };

    float firstLost = 0.0f;
    float secondLost = 0.0f;
    int shotsLeft = -1;
    fire(0, firstLost, secondLost, shotsLeft);
    CHECK(firstLost > 0.0f);
    CHECK(secondLost == 0.0f);  // It stopped at the first zombie.
    CHECK(shotsLeft == 0);

    fire(1, firstLost, secondLost, shotsLeft);
    CHECK(firstLost > 0.0f);
    CHECK(secondLost > 0.0f);  // It went through the first and hit the second.
    CHECK(shotsLeft == 0);     // And is gone after that second hit.
}

TEST_CASE("a shot hits an enemy, damages it, and credits the weapon") {
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
    enemies::hit(*state, 0, 100000.0f, WeaponId::Rifle, 0, Vec2{});
    enemies::removeDead(*state);
    CHECK(state->run.kills == 1);
    CHECK(state->enemies.count == 0);
    CHECK(totalGemXp(*state) == balance::kEnemyStats[static_cast<int>(EnemyType::Brute)].xp);
}

TEST_CASE("collecting enough XP opens level-up, and a choice resumes play") {
    auto state = makeRun();
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    CHECK(state->firstPlayer().level == 2);
    sim::step(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    CHECK(state->firstPlayer().offerCount == kOffersPerLevel);

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
    upgrades::addXp(*state, 0, balance::xpForLevel(1) + balance::xpForLevel(2));
    CHECK(state->firstPlayer().pendingLevels == 2);
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
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    sim::step(*state, InputFrame{});
    InputFrame reroll;
    reroll.reroll = true;
    sim::step(*state, reroll);
    sim::step(*state, reroll);
    CHECK(state->firstPlayer().rerollsLeft == 0);
    sim::step(*state, reroll);
    CHECK(state->firstPlayer().rerollsLeft == 0);
}

TEST_CASE("a level 5 weapon with its passive is offered an evolution first") {
    auto state = makeRun();
    state->firstPlayer().weapons[0].level = kMaxLevelPerItem;
    state->firstPlayer().passives[0].id = PassiveId::FireRate;
    state->firstPlayer().passives[0].level = 1;
    state->firstPlayer().passiveCount = 1;
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    sim::step(*state, InputFrame{});
    REQUIRE(state->run.phase == RunPhase::LevelUp);
    CHECK(state->firstPlayer().offers[0].kind == CardKind::Evolution);
    CHECK(state->firstPlayer().offers[0].weapon == WeaponId::Rifle);

    InputFrame pick;
    pick.chooseCard = 0;
    sim::step(*state, pick);
    CHECK(state->firstPlayer().weapons[0].evolved);
}

TEST_CASE("evolved weapons are not offered the evolution again") {
    auto state = makeRun();
    state->firstPlayer().weapons[0].level = kMaxLevelPerItem;
    state->firstPlayer().weapons[0].evolved = true;
    state->firstPlayer().passives[0].id = PassiveId::FireRate;
    state->firstPlayer().passiveCount = 1;
    upgrades::addXp(*state, 0, balance::xpForLevel(1));
    sim::step(*state, InputFrame{});
    for (int i = 0; i < state->firstPlayer().offerCount; ++i) {
        CHECK(state->firstPlayer().offers[i].kind != CardKind::Evolution);
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

TEST_CASE("same seed and inputs give the same state at every step, not only at the end") {
    auto first = makeRun(777);
    auto second = makeRun(777);
    int firstMismatch = -1;
    for (int i = 0; i < 60 * 90; ++i) {
        sim::step(*first, botInput(*first));
        sim::step(*second, botInput(*second));
        first->events.clear();
        second->events.clear();
        // Every step is compared. Sampling once a second would miss a difference that is
        // gone again before the next sample, such as a few steps of a wrong value.
        if (firstMismatch < 0 && sim::stateHash(*first) != sim::stateHash(*second)) firstMismatch = i;
    }
    CHECK(first->run.kills > 0);
    CHECK_MESSAGE(firstMismatch == -1, "the two runs parted at step " << firstMismatch);
    CHECK(sim::stateHash(*first) == sim::stateHash(*second));
}

TEST_CASE("the hash changes as the run goes on, so equal hashes mean something") {
    // If the hash were stuck on one value, every determinism check would pass for any run.
    auto state = makeRun(777);
    uint64_t previous = sim::stateHash(*state);
    int unchanged = 0;
    for (int i = 0; i < 600; ++i) {
        sim::step(*state, botInput(*state));
        state->events.clear();
        const uint64_t now = sim::stateHash(*state);
        if (now == previous) ++unchanged;
        previous = now;
    }
    CHECK(unchanged == 0);
}

TEST_CASE("the state hash sees every kind of state that a later step reads") {
    // A run in progress, plus one of everything the pools can hold, so each field has a value.
    auto base = makeRun(31);
    for (int i = 0; i < 60 * 20; ++i) {
        sim::step(*base, botInput(*base));
        base->events.clear();
    }
    weapons::grant(*base, 0, WeaponId::Shotgun);
    base->firstPlayer().passives[0] = PassiveSlot{PassiveId::Damage, 1};
    base->firstPlayer().passiveCount = 1;
    Enemy* enemy = enemies::spawn(*base, EnemyType::Charger, Vec2{500.0f, 100.0f}, false);
    REQUIRE(enemy != nullptr);
    Projectile* shot = base->projectiles.add();
    EnemyBullet* bullet = base->enemyBullets.add();
    Zone* zone = base->zones.add();
    Gem* gem = base->gems.add();
    REQUIRE((shot && bullet && zone && gem));
    pickups::spawnPickup(*base, Vec2{300.0f, 300.0f}, PickupType::Heal);
    shot->life = 1.0f;
    bullet->life = 1.0f;
    zone->life = 1.0f;
    gem->xp = 1;
    const uint64_t original = sim::stateHash(*base);

    // Each change alone must change the hash. None of these moves anything yet, which is
    // exactly the kind of difference that is invisible until a later step.
    const std::vector<std::pair<const char*, void (*)(GameState&)>> changes = {
        {"magazine", [](GameState& s) { s.firstPlayer().weapons[0].magazine += 1; }},
        {"reserve", [](GameState& s) { s.firstPlayer().weapons[0].reserve += 1; }},
        {"weapon cooldown", [](GameState& s) { s.firstPlayer().weapons[0].cooldown += 0.5f; }},
        {"reload", [](GameState& s) { s.firstPlayer().weapons[0].reloadLeft += 0.5f; }},
        {"weapon level", [](GameState& s) { s.firstPlayer().weapons[1].level += 1; }},
        {"evolved", [](GameState& s) { s.firstPlayer().weapons[1].evolved = !s.firstPlayer().weapons[1].evolved; }},
        {"passive level", [](GameState& s) { s.firstPlayer().passives[0].level += 1; }},
        {"player stats", [](GameState& s) { s.firstPlayer().stats.armor += 1.0f; }},
        {"player facing", [](GameState& s) { s.firstPlayer().facing = Vec2{0.0f, 1.0f}; }},
        {"offers", [](GameState& s) { s.firstPlayer().offers[0].newLevel += 1; }},
        {"rerolls", [](GameState& s) { s.firstPlayer().rerollsLeft += 1; }},
        {"damage credit", [](GameState& s) { s.run.damageByWeapon[3] += 1.0f; }},
        {"wave accumulator", [](GameState& s) { s.run.waveAccumulators[5] += 0.25f; }},
        {"blade angle", [](GameState& s) { s.firstPlayer().bladeAngle += 0.1f; }},
        {"next elite", [](GameState& s) { s.run.nextEliteEvent += 1; }},
        {"next drop", [](GameState& s) { s.run.nextPickupDropKills += 1; }},
        {"boss phase", [](GameState& s) { s.run.bossPhase += 1; }},
        {"enemy timer", [](GameState& s) { s.enemies[s.enemies.count - 1].timer += 0.5f; }},
        {"enemy state", [](GameState& s) { s.enemies[s.enemies.count - 1].state += 1; }},
        {"enemy type", [](GameState& s) { s.enemies[s.enemies.count - 1].type = EnemyType::Runner; }},
        {"enemy direction", [](GameState& s) { s.enemies[s.enemies.count - 1].dir = Vec2{0.0f, 1.0f}; }},
        {"enemy knockback", [](GameState& s) { s.enemies[s.enemies.count - 1].knock = Vec2{3.0f, 0.0f}; }},
        {"shot pierce", [](GameState& s) { s.projectiles[s.projectiles.count - 1].pierce += 1; }},
        {"shot damage", [](GameState& s) { s.projectiles[s.projectiles.count - 1].damage += 1.0f; }},
        {"shot velocity", [](GameState& s) { s.projectiles[s.projectiles.count - 1].vel = Vec2{1.0f, 1.0f}; }},
        {"enemy bullet velocity", [](GameState& s) { s.enemyBullets[s.enemyBullets.count - 1].vel = Vec2{1.0f, 1.0f}; }},
        {"zone position", [](GameState& s) { s.zones[s.zones.count - 1].a = Vec2{7.0f, 7.0f}; }},
        {"zone life", [](GameState& s) { s.zones[s.zones.count - 1].life += 0.5f; }},
        {"zone damage", [](GameState& s) { s.zones[s.zones.count - 1].damage += 1.0f; }},
        {"zone applied", [](GameState& s) { s.zones[s.zones.count - 1].applied = !s.zones[s.zones.count - 1].applied; }},
        {"gem attracted", [](GameState& s) { s.gems[s.gems.count - 1].attracted = !s.gems[s.gems.count - 1].attracted; }},
        {"pickup position", [](GameState& s) { s.pickups[s.pickups.count - 1].pos = Vec2{9.0f, 9.0f}; }},
        {"pickup type", [](GameState& s) { s.pickups[s.pickups.count - 1].type = PickupType::Bomb; }},
    };
    for (const auto& change : changes) {
        CAPTURE(change.first);
        auto changed = std::make_unique<GameState>(*base);
        REQUIRE(sim::stateHash(*changed) == original);
        change.second(*changed);
        CHECK(sim::stateHash(*changed) != original);
    }
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
