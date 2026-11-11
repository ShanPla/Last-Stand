#include "doctest.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Enemies.h"
#include "components/Enemies/Runner/Runner.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Pickups/Pickups.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
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

int countEvents(const GameState& state, EventType type) {
    int total = 0;
    for (const SimEvent& event : state.events) {
        if (event.type == type) total++;
    }
    return total;
}

int countType(const GameState& state, EnemyType type) {
    int total = 0;
    for (const Enemy& enemy : state.enemies) {
        if (enemy.type == type) total++;
    }
    return total;
}

// Kills the enemy at this index and runs the death handling.
void kill(GameState& state, int index) {
    enemies::hit(state, index, 1.0e9f, WeaponId::Rifle, 0, Vec2{});
    enemies::removeDead(state);
}

// makeRun plays Normal, so the boss uses Normal's move set.
const balance::BossMoves& kNormalBoss = balance::bossMoves(Difficulty::Normal);

Enemy* spawnBoss(GameState& state, Vec2 pos) {
    Enemy* boss = enemies::spawn(state, EnemyType::Boss, pos, false);
    REQUIRE(boss != nullptr);
    state.run.bossSpawned = true;
    state.run.bossHpFraction = 1.0f;
    return boss;
}

int totalGemXp(const GameState& state) {
    int total = 0;
    for (const Gem& gem : state.gems) total += gem.xp;
    return total;
}

}  // namespace

// ---------------------------------------------------------------------------
// Basic movers
// ---------------------------------------------------------------------------

TEST_CASE("walker, brute, bloater and crawler walk straight at the player") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};
    const EnemyType types[] = {EnemyType::Walker, EnemyType::Brute, EnemyType::Bloater, EnemyType::Crawler};
    for (EnemyType type : types) {
        state->enemies.clear();
        Enemy* enemy = enemies::spawn(*state, type, Vec2{500.0f, 0.0f}, false);
        REQUIRE(enemy != nullptr);
        const float speed = enemy->speed;
        enemies::update(*state);
        CHECK(state->enemies[0].pos.x == doctest::Approx(500.0f - speed * kDt).epsilon(0.001));
        CHECK(state->enemies[0].pos.y == doctest::Approx(0.0f).epsilon(0.001));
    }
}

TEST_CASE("runner keeps its speed, weaves sideways, and weaves out of step with other runners") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};
    enemies::spawn(*state, EnemyType::Runner, Vec2{600.0f, 0.0f}, false);
    enemies::spawn(*state, EnemyType::Runner, Vec2{600.0f, 0.0f}, false);
    float maxSideways = 0.0f;
    bool differs = false;
    for (int i = 0; i < 120; ++i) {
        state->run.tick++;
        // Velocity is read straight from the behaviour, with no separation or knockback.
        const Vec2 first = enemies::runnerVelocity(*state, state->enemies[0]);
        const Vec2 second = enemies::runnerVelocity(*state, state->enemies[1]);
        CHECK(length(first) == doctest::Approx(state->enemies[0].speed).epsilon(0.001));
        maxSideways = std::max(maxSideways, std::fabs(first.y));
        // Same spot, different ids, so any difference is down to the id.
        if (std::fabs(first.y - second.y) > 5.0f) differs = true;
    }
    CHECK(maxSideways > 5.0f);
    CHECK(differs);
}

// ---------------------------------------------------------------------------
// Charger
// ---------------------------------------------------------------------------

TEST_CASE("charger chases, telegraphs with a locked aim, dashes, recovers, then chases again") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};
    REQUIRE(enemies::spawn(*state, EnemyType::Charger, Vec2{300.0f, 0.0f}, false) != nullptr);

    // Far away it just chases, and the state stays at the start value.
    state->enemies[0].pos = Vec2{900.0f, 0.0f};
    enemies::update(*state);
    CHECK(state->enemies[0].state == enemies::kChargerChase);
    CHECK(state->enemies[0].pos.x < 900.0f);
    state->enemies[0].pos = Vec2{300.0f, 0.0f};

    enemies::update(*state);
    REQUIRE(state->enemies[0].state == enemies::kChargerTelegraph);
    const Vec2 lockedDir = state->enemies[0].dir;
    CHECK(lockedDir.x == doctest::Approx(-1.0f));
    CHECK(lockedDir.y == doctest::Approx(0.0f));

    // The player moves away. The dash must still go along the locked direction.
    state->firstPlayer().pos = Vec2{0.0f, 200.0f};
    const Vec2 telegraphPos = state->enemies[0].pos;
    int telegraphSteps = 0;
    int dashSteps = 0;
    int recoverSteps = 0;
    uint8_t previous = enemies::kChargerTelegraph;
    bool backToChase = false;
    for (int i = 0; i < 400 && !backToChase; ++i) {
        enemies::update(*state);
        const Enemy& charger = state->enemies[0];
        // States only ever move in the order Telegraph, Dash, Recover, Chase.
        CHECK(((charger.state == previous) || (charger.state == previous + 1) ||
               (previous == enemies::kChargerRecover && charger.state == enemies::kChargerChase)));
        previous = charger.state;
        switch (charger.state) {
            case enemies::kChargerTelegraph:
                telegraphSteps++;
                CHECK(charger.pos.x == doctest::Approx(telegraphPos.x));  // Stands still.
                break;
            case enemies::kChargerDash:
                dashSteps++;
                CHECK(charger.dir.x == doctest::Approx(lockedDir.x));
                CHECK(charger.pos.y == doctest::Approx(0.0f).epsilon(0.001));
                break;
            case enemies::kChargerRecover: recoverSteps++; break;
            default: backToChase = true; break;
        }
    }
    CHECK(backToChase);
    // The first update already counted one telegraph step, so allow a step either way.
    const float stepSlack = 2.0f * kDt;
    CHECK(static_cast<float>(telegraphSteps + 1) * kDt == doctest::Approx(balance::kChargerTelegraphSeconds).epsilon(0.05));
    CHECK(static_cast<float>(dashSteps) * kDt == doctest::Approx(balance::kChargerDashSeconds).epsilon(stepSlack / balance::kChargerDashSeconds));
    CHECK(static_cast<float>(recoverSteps) * kDt == doctest::Approx(balance::kChargerRecoverSeconds).epsilon(stepSlack / balance::kChargerRecoverSeconds));
    // The dash covers about dash speed times dash time, and the charger never turned toward the player.
    CHECK(state->enemies[0].pos.x < telegraphPos.x - 0.9f * balance::kChargerDashSpeed * balance::kChargerDashSeconds);
}

// ---------------------------------------------------------------------------
// Spitter
// ---------------------------------------------------------------------------

TEST_CASE("spitter fires bullets at the player on its interval") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};
    REQUIRE(enemies::spawn(*state, EnemyType::Spitter, Vec2{500.0f, 0.0f}, false) != nullptr);
    // Bullets are not moved here, so each one is still at the spot where it was fired.
    const int steps = static_cast<int>(10.0f / kDt);
    for (int i = 0; i < steps; ++i) enemies::update(*state);
    const int expected = static_cast<int>(10.0f / balance::kSpitterFireInterval);
    CHECK(state->enemyBullets.count >= expected);
    CHECK(state->enemyBullets.count <= expected + 1);
    for (const EnemyBullet& bullet : state->enemyBullets) {
        CHECK(length(bullet.vel) == doctest::Approx(balance::kSpitterBulletSpeed).epsilon(0.001));
        CHECK(bullet.damage == doctest::Approx(balance::kSpitterBulletDamage));
        const Vec2 toPlayer = normalizedOr(state->firstPlayer().pos - bullet.pos, Vec2{});
        CHECK(dot(toPlayer, normalizedOr(bullet.vel, Vec2{})) > 0.999f);
    }
}

TEST_CASE("spitter closes in to its keep distance, backs away when too close, and then holds the range") {
    for (const float startDistance : {800.0f, 120.0f}) {
        auto state = makeRun();
        state->firstPlayer().pos = Vec2{0.0f, 0.0f};
        REQUIRE(enemies::spawn(*state, EnemyType::Spitter, Vec2{startDistance, 0.0f}, false) != nullptr);
        for (int i = 0; i < 60 * 20; ++i) {
            enemies::update(*state);
            state->enemyBullets.clear();
            state->firstPlayer().hp = state->firstPlayer().maxHp;
        }
        const float dist = length(state->enemies[0].pos - state->firstPlayer().pos);
        CHECK(dist > balance::kSpitterKeepDistance - balance::kSpitterDistanceBand - 5.0f);
        // Circling slowly drifts outward, so the upper limit is looser.
        CHECK(dist < balance::kSpitterKeepDistance + balance::kSpitterDistanceBand + 60.0f);
    }
}

// ---------------------------------------------------------------------------
// Deaths and drops
// ---------------------------------------------------------------------------

TEST_CASE("a dead bloater spawns crawlers around its corpse, and an elite spawns twice as many") {
    auto state = makeRun();
    const Vec2 spot{400.0f, 300.0f};
    enemies::spawn(*state, EnemyType::Bloater, spot, false);
    kill(*state, 0);
    CHECK(state->run.kills == 1);
    CHECK(state->enemies.count == balance::kCrawlersPerBloater);
    for (const Enemy& enemy : state->enemies) {
        CHECK(enemy.type == EnemyType::Crawler);
        CHECK(distance(enemy.pos, spot) == doctest::Approx(balance::kBloaterCrawlerSpread).epsilon(0.01));
    }

    state->enemies.clear();
    enemies::spawn(*state, EnemyType::Bloater, spot, true);
    kill(*state, 0);
    CHECK(state->enemies.count == 2 * balance::kCrawlersPerBloater);
}

TEST_CASE("crawlers from a bloater do not split again, and other types spawn nothing") {
    auto state = makeRun();
    enemies::spawn(*state, EnemyType::Walker, Vec2{100.0f, 0.0f}, false);
    kill(*state, 0);
    CHECK(state->enemies.count == 0);
    enemies::spawn(*state, EnemyType::Crawler, Vec2{100.0f, 0.0f}, false);
    kill(*state, 0);
    CHECK(state->enemies.count == 0);
}

TEST_CASE("an elite always drops a pickup and a large gem, and pushes EliteKilled") {
    auto state = makeRun();
    enemies::spawn(*state, EnemyType::Brute, Vec2{200.0f, 0.0f}, true);
    kill(*state, 0);
    CHECK(state->pickups.count == 1);
    CHECK(totalGemXp(*state) == balance::kEnemyStats[static_cast<int>(EnemyType::Brute)].xp * balance::kElite_XpMult);
    CHECK(countEvents(*state, EventType::EliteKilled) == 1);
    CHECK(countEvents(*state, EventType::EnemyKilled) == 0);

    // A normal enemy on the first kill drops no pickup.
    state->pickups.clear();
    enemies::spawn(*state, EnemyType::Walker, Vec2{200.0f, 0.0f}, false);
    kill(*state, 0);
    CHECK(state->pickups.count == 0);
}

TEST_CASE("elite drops can be any pickup type, ammo included") {
    auto state = makeRun();
    bool seen[static_cast<int>(PickupType::Count)] = {};
    for (int i = 0; i < 200; ++i) {
        state->pickups.clear();
        pickups::onEnemyKilled(*state, Vec2{}, true);
        REQUIRE(state->pickups.count == 1);
        seen[static_cast<int>(state->pickups[0].type)] = true;
    }
    for (const bool type : seen) CHECK(type);
}

TEST_CASE("ammo crates drop every N kills, and a pickup drops on its own counter") {
    auto state = makeRun();
    // Move the random drop far away so only the ammo counter is tested here.
    state->run.nextPickupDropKills = 1000000;
    int crates = 0;
    for (int kill = 1; kill <= 2 * balance::kKillsPerAmmoCrate; ++kill) {
        state->run.kills = kill;
        const int before = state->pickups.count;
        pickups::onEnemyKilled(*state, Vec2{}, false);
        if (state->pickups.count > before) {
            crates++;
            CHECK(state->pickups[state->pickups.count - 1].type == PickupType::Ammo);
            CHECK(kill % balance::kKillsPerAmmoCrate == 0);
        }
    }
    CHECK(crates == 2);

    // The random drop: none before the counter, then exactly one of Heal, Magnet or Bomb.
    state = makeRun();
    for (int kill = 1; kill < balance::kKillsPerPickupDrop; ++kill) {
        state->run.kills = kill;
        if (kill % balance::kKillsPerAmmoCrate == 0) continue;  // Crates are a separate drop.
        pickups::onEnemyKilled(*state, Vec2{}, false);
    }
    CHECK(state->pickups.count == 0);
    state->run.kills = balance::kKillsPerPickupDrop;
    pickups::onEnemyKilled(*state, Vec2{}, false);
    REQUIRE(state->pickups.count == 1);
    CHECK(state->pickups[0].type != PickupType::Ammo);
    CHECK(state->run.nextPickupDropKills == 2 * balance::kKillsPerPickupDrop);
}

TEST_CASE("a full pickup pool drops the oldest pickup to make room for a new one") {
    auto state = makeRun();
    // Each pickup sits at its own x, far from the player, so they can be told apart.
    for (int i = 0; i < kMaxPickups; ++i) {
        pickups::spawnPickup(*state, Vec2{5000.0f + static_cast<float>(i), 5000.0f}, PickupType::Heal);
    }
    REQUIRE(state->pickups.count == kMaxPickups);
    pickups::onEnemyKilled(*state, Vec2{9000.0f, 9000.0f}, true);
    CHECK(state->pickups.count == kMaxPickups);
    float oldestX = 1.0e9f;
    float newestX = -1.0e9f;
    for (const Pickup& pickup : state->pickups) {
        oldestX = std::min(oldestX, pickup.pos.x);
        newestX = std::max(newestX, pickup.pos.x);
    }
    CHECK(oldestX == doctest::Approx(5001.0f));  // The pickup at x = 5000 was dropped.
    CHECK(newestX >= 9000.0f);                    // The new drop is there.
}

TEST_CASE("a full pickup pool always loses the oldest pickup, however many drops follow") {
    // One eviction can look right even when the pool is wrong. Removing slot 0 moves the
    // last pickup into it, so after a few more drops the pool throws away recent pickups
    // and keeps old ones. So this follows many drops and, after each one, requires that the
    // pool holds exactly the newest kMaxPickups pickups that were ever dropped.
    auto state = makeRun();
    std::vector<float> dropped;  // The x of every pickup, in the order dropped.
    const auto drop = [&](PickupType type) {
        const float x = 5000.0f + static_cast<float>(dropped.size());
        dropped.push_back(x);
        pickups::spawnPickup(*state, Vec2{x, 5000.0f}, type);
    };
    for (int i = 0; i < kMaxPickups; ++i) drop(PickupType::Heal);
    REQUIRE(state->pickups.count == kMaxPickups);

    int wrongAfter = -1;
    for (int extra = 1; extra <= 3 * kMaxPickups; ++extra) {
        // The type is varied so a pickup cannot be told from another by position alone.
        drop(static_cast<PickupType>(extra % static_cast<int>(PickupType::Count)));
        std::vector<float> held;
        for (const Pickup& pickup : state->pickups) held.push_back(pickup.pos.x);
        std::sort(held.begin(), held.end());
        const std::vector<float> newest(dropped.end() - kMaxPickups, dropped.end());  // Already in order.
        if (wrongAfter < 0 && (state->pickups.count != kMaxPickups || held != newest)) wrongAfter = extra;
    }
    CHECK_MESSAGE(wrongAfter == -1, "after " << wrongAfter << " extra drops the pool no longer holds the newest " << kMaxPickups);
}

// ---------------------------------------------------------------------------
// Waves
// ---------------------------------------------------------------------------

TEST_CASE("the wave table spawns each type at the right time, and the boss waits for 10:00") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};

    float firstSeen[static_cast<int>(EnemyType::Count)];
    float firstAllowed[static_cast<int>(EnemyType::Count)];
    for (float& value : firstSeen) value = -1.0f;
    for (float& value : firstAllowed) value = 1.0e9f;
    for (const balance::WaveRow& row : balance::kWaves) {
        float& earliest = firstAllowed[static_cast<int>(row.type)];
        if (row.startSec < earliest) earliest = row.startSec;
    }
    int walkersFirstMinute = 0;
    int elitesSeen = 0;

    // Spawned enemies are dropped each step, so the pool never fills and hides a spawn.
    const uint32_t lastTick = static_cast<uint32_t>((balance::kRunSeconds - 1.0f) / kDt);
    for (uint32_t tick = 1; tick <= lastTick; ++tick) {
        state->run.tick = tick;
        waves::update(*state);
        state->events.clear();
        const float now = state->timeSeconds();
        for (const Enemy& enemy : state->enemies) {
            const int type = static_cast<int>(enemy.type);
            // Elites come from their own event list, so only normal enemies show the table timing.
            if (!enemy.elite && firstSeen[type] < 0.0f) firstSeen[type] = now;
            if (enemy.elite) elitesSeen++;
            if (enemy.type == EnemyType::Walker && !enemy.elite && now <= 60.0f) walkersFirstMinute++;
        }
        state->enemies.clear();
    }

    for (int type = 0; type < static_cast<int>(EnemyType::Count); ++type) {
        if (firstAllowed[type] > 1.0e8f) {
            CHECK(firstSeen[type] < 0.0f);  // Crawlers and the boss never come from the table.
            continue;
        }
        CHECK(firstSeen[type] >= firstAllowed[type]);
        CHECK(firstSeen[type] <= firstAllowed[type] + 12.0f);
    }
    // The first row alone gives about the area under its rate line, 60 s at 1.0 to 2.2 per second.
    CHECK(walkersFirstMinute == doctest::Approx(96.0f).epsilon(0.06));
    CHECK(elitesSeen == static_cast<int>(std::size(balance::kEliteEvents)));
    CHECK_FALSE(state->run.bossSpawned);
}

TEST_CASE("the boss spawns once at 10:00 with an event, even if the enemy pool is full") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 0.0f};
    while (state->enemies.count < kMaxEnemies) {
        REQUIRE(enemies::spawn(*state, EnemyType::Walker, Vec2{2000.0f, 0.0f}, false) != nullptr);
    }
    state->run.nextEliteEvent = static_cast<int>(std::size(balance::kEliteEvents));  // Skip elites.

    int bossEvents = 0;
    const uint32_t startTick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 5;
    for (uint32_t tick = startTick; tick < startTick + 600; ++tick) {
        state->run.tick = tick;
        const bool before = state->run.bossSpawned;
        waves::update(*state);
        bossEvents += countEvents(*state, EventType::BossSpawned);
        state->events.clear();
        if (state->timeSeconds() < balance::kRunSeconds - 0.01f) CHECK_FALSE(state->run.bossSpawned);
        if (before) CHECK(countType(*state, EnemyType::Boss) == 1);
    }
    CHECK(state->run.bossSpawned);
    CHECK(bossEvents == 1);
    CHECK(countType(*state, EnemyType::Boss) == 1);
    CHECK(state->enemies.count <= kMaxEnemies);
    // The normal waves stay off from here on.
    for (int row = 0; row < static_cast<int>(std::size(balance::kWaves)); ++row) {
        CHECK(waves::rowRate(row, balance::kRunSeconds + 1.0f) == 0.0f);
    }
}

// ---------------------------------------------------------------------------
// Boss
// ---------------------------------------------------------------------------

TEST_CASE("boss phase follows its HP, copies the HP fraction, and sends one event per change") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{3000.0f, 0.0f};  // Far away, so no contact damage.
    Enemy* boss = spawnBoss(*state, Vec2{0.0f, 0.0f});
    const float maxHp = boss->maxHp;

    const struct { float hpFraction; int phase; } steps[] = {
        {1.0f, 0}, {balance::kBossPhase2HpFraction + 0.01f, 0}, {balance::kBossPhase2HpFraction - 0.01f, 1},
        {balance::kBossPhase3HpFraction + 0.01f, 1}, {balance::kBossPhase3HpFraction - 0.01f, 2}, {0.05f, 2},
    };
    int phaseEvents = 0;
    for (const auto& step : steps) {
        state->enemies[0].hp = maxHp * step.hpFraction;
        enemies::update(*state);
        phaseEvents += countEvents(*state, EventType::BossPhaseChanged);
        state->events.clear();
        state->zones.clear();
        state->enemyBullets.clear();
        CHECK(state->run.bossPhase == step.phase);
        CHECK(state->run.bossHpFraction == doctest::Approx(step.hpFraction));
    }
    CHECK(phaseEvents == 2);

    // A hit that skips a phase still sends an event for each phase passed.
    state = makeRun();
    state->firstPlayer().pos = Vec2{3000.0f, 0.0f};
    spawnBoss(*state, Vec2{0.0f, 0.0f});
    state->enemies[0].hp = state->enemies[0].maxHp * 0.1f;
    enemies::update(*state);
    CHECK(state->run.bossPhase == 2);
    CHECK(countEvents(*state, EventType::BossPhaseChanged) == 2);
}

TEST_CASE("boss circles: the first lands on the player, the rest nearby, all hostile telegraphs") {
    for (int phase = 0; phase < 3; ++phase) {
        auto state = makeRun();
        state->firstPlayer().pos = Vec2{500.0f, 200.0f};
        Enemy* boss = spawnBoss(*state, Vec2{0.0f, 0.0f});
        state->run.bossPhase = static_cast<uint8_t>(phase);
        boss->state = enemies::kBossWalk;
        state->run.bossStep = 0;  // Circles.
        boss->timer = 0.0f;
        enemies::update(*state);
        REQUIRE(state->zones.count == kNormalBoss.circleCount[phase]);
        CHECK(state->zones[0].a.x == doctest::Approx(500.0f));
        CHECK(state->zones[0].a.y == doctest::Approx(200.0f));
        for (const Zone& zone : state->zones) {
            CHECK(zone.kind == ZoneKind::BossCircle);
            CHECK(zone.hostile);
            CHECK(zone.radius == doctest::Approx(kNormalBoss.circleRadius));
            CHECK(zone.life == doctest::Approx(kNormalBoss.telegraphSeconds));
            CHECK(zone.maxLife == doctest::Approx(kNormalBoss.telegraphSeconds));
            CHECK(zone.damage == doctest::Approx(kNormalBoss.circleDamage));
            CHECK(distance(zone.a, state->firstPlayer().pos) <= balance::kBossCircleScatter + 0.01f);
        }
        CHECK(enemies::nextBossAttack(*state) == balance::BossAttack::Line);
    }
}

TEST_CASE("boss line points from the boss at the player, with two more lines in the last phase") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 600.0f};
    Enemy* boss = spawnBoss(*state, Vec2{0.0f, 0.0f});
    boss->state = enemies::kBossWalk;
    state->run.bossStep = 1;  // Line.
    boss->timer = 0.0f;
    enemies::update(*state);
    REQUIRE(state->zones.count == 1);
    const Zone& line = state->zones[0];
    CHECK(line.kind == ZoneKind::BossLine);
    CHECK(line.hostile);
    CHECK(line.a.x == doctest::Approx(0.0f));
    CHECK(line.a.y == doctest::Approx(0.0f));
    CHECK(line.b.x == doctest::Approx(line.a.x).epsilon(0.001));
    CHECK(line.b.y == doctest::Approx(line.a.y + balance::kBossLineLength));
    CHECK(line.radius == doctest::Approx(balance::kBossLineHalfWidth));
    CHECK(line.damage == doctest::Approx(kNormalBoss.lineDamage));
    CHECK(line.life == doctest::Approx(kNormalBoss.telegraphSeconds));

    state->zones.clear();
    state->run.bossPhase = 2;
    state->enemies[0].state = enemies::kBossWalk;
    state->run.bossStep = 1;
    state->enemies[0].timer = 0.0f;
    state->enemies[0].hp = state->enemies[0].maxHp * 0.1f;
    enemies::update(*state);
    REQUIRE(state->zones.count == 3);
    // The extra two are turned half a radian each way from the first.
    const Vec2 first = normalizedOr(state->zones[0].b - state->zones[0].a, Vec2{});
    const Vec2 second = normalizedOr(state->zones[1].b - state->zones[1].a, Vec2{});
    CHECK(dot(first, second) == doctest::Approx(std::cos(balance::kBossSecondLineRadians)).epsilon(0.001));
}

TEST_CASE("boss bullet ring stands still to charge, then fires an even ring") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{3000.0f, 0.0f};
    Enemy* boss = spawnBoss(*state, Vec2{0.0f, 0.0f});
    boss->state = enemies::kBossWalk;
    state->run.bossStep = 2;  // Ring.
    boss->timer = 0.0f;

    enemies::update(*state);
    REQUIRE(state->enemies[0].state == enemies::kBossRingCharge);
    const Vec2 startPos = state->enemies[0].pos;
    int chargeSteps = 0;
    while (state->enemies[0].state == enemies::kBossRingCharge && chargeSteps < 200) {
        CHECK(state->enemyBullets.count == 0);
        CHECK(state->enemies[0].pos.x == doctest::Approx(startPos.x));
        enemies::update(*state);
        chargeSteps++;
    }
    CHECK(static_cast<float>(chargeSteps) * kDt == doctest::Approx(kNormalBoss.telegraphSeconds).epsilon(0.05));
    REQUIRE(state->enemyBullets.count == kNormalBoss.ringBullets[0]);

    Vec2 sum{};
    for (const EnemyBullet& bullet : state->enemyBullets) {
        CHECK(length(bullet.vel) == doctest::Approx(kNormalBoss.bulletSpeed).epsilon(0.001));
        CHECK(bullet.damage == doctest::Approx(kNormalBoss.bulletDamage));
        sum += bullet.vel;
    }
    // Evenly spaced directions cancel out.
    CHECK(length(sum) < 0.01f * kNormalBoss.bulletSpeed * static_cast<float>(kNormalBoss.ringBullets[0]));
    CHECK(state->enemies[0].state != enemies::kBossRingCharge);
}

TEST_CASE("boss dash telegraphs standing still, then dashes along a locked direction for the dash time") {
    auto state = makeRun();
    state->firstPlayer().pos = Vec2{0.0f, 2000.0f};
    Enemy* boss = spawnBoss(*state, Vec2{0.0f, 0.0f});
    state->enemies[0].hp = boss->maxHp * 0.5f;  // Phase 1.
    boss->state = enemies::kBossWalk;
    state->run.bossStep = 3;  // Dash, which only phase 1 and later have.
    boss->timer = 0.0f;

    enemies::update(*state);
    REQUIRE(state->enemies[0].state == enemies::kBossDashCharge);
    const Vec2 lockedDir = state->enemies[0].dir;
    CHECK(lockedDir.y == doctest::Approx(1.0f));
    state->firstPlayer().pos = Vec2{2000.0f, 0.0f};  // Moving away must not change the aim.

    int chargeSteps = 0;
    while (state->enemies[0].state == enemies::kBossDashCharge && chargeSteps < 200) {
        CHECK(state->enemies[0].pos.y == doctest::Approx(0.0f));
        enemies::update(*state);
        chargeSteps++;
    }
    CHECK(static_cast<float>(chargeSteps) * kDt == doctest::Approx(kNormalBoss.telegraphSeconds).epsilon(0.05));
    REQUIRE(state->enemies[0].state == enemies::kBossDash);

    const float startY = state->enemies[0].pos.y;
    int dashSteps = 0;
    while (state->enemies[0].state == enemies::kBossDash && dashSteps < 200) {
        enemies::update(*state);
        dashSteps++;
    }
    CHECK(static_cast<float>(dashSteps) * kDt == doctest::Approx(balance::kBossDashSeconds).epsilon(0.05));
    CHECK(state->enemies[0].pos.y - startY == doctest::Approx(balance::kBossDashSpeed * balance::kBossDashSeconds).epsilon(0.06));
    CHECK(state->enemies[0].pos.x == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("the boss cycles through every attack, and only dashes from phase 1") {
    for (int phase = 0; phase < 3; ++phase) {
        auto state = makeRun();
        state->firstPlayer().pos = Vec2{0.0f, 0.0f};
        Enemy* boss = spawnBoss(*state, Vec2{1500.0f, 0.0f});
        static const float hpForPhase[3] = {1.0f, 0.5f, 0.2f};
        boss->hp = boss->maxHp * hpForPhase[phase];

        bool sawCircle = false;
        bool sawLine = false;
        bool sawRing = false;
        bool sawDash = false;
        int ringCount = 0;
        int lineCount = 0;
        int circleCount = 0;
        const int steps = static_cast<int>(40.0f / kDt);
        for (int i = 0; i < steps; ++i) {
            enemies::update(*state);
            for (const Zone& zone : state->zones) {
                if (zone.kind == ZoneKind::BossCircle) { sawCircle = true; circleCount = std::max(circleCount, state->zones.count); }
                if (zone.kind == ZoneKind::BossLine) { sawLine = true; lineCount = std::max(lineCount, state->zones.count); }
            }
            if (state->enemyBullets.count >= kNormalBoss.ringBullets[phase]) {
                sawRing = true;
                ringCount = state->enemyBullets.count;
            }
            if (state->enemies[0].state == enemies::kBossDash) sawDash = true;
            state->zones.clear();
            state->enemyBullets.clear();
            state->firstPlayer().hp = state->firstPlayer().maxHp;  // Stay alive so the boss keeps cycling.
            state->events.clear();
        }
        CHECK(state->run.bossPhase == phase);
        CHECK(sawCircle);
        CHECK(sawLine);
        CHECK(sawRing);
        CHECK(ringCount == kNormalBoss.ringBullets[phase]);
        CHECK(circleCount == kNormalBoss.circleCount[phase]);
        CHECK(lineCount == (phase == 2 ? 3 : 1));
        CHECK(sawDash == (phase >= 1));
    }
}

TEST_CASE("killing the boss sends BossKilled and wins the run") {
    auto state = makeRun();
    spawnBoss(*state, Vec2{300.0f, 0.0f});
    enemies::hit(*state, 0, 1.0e9f, WeaponId::Rifle, 0, Vec2{500.0f, 0.0f});
    enemies::removeDead(*state);
    CHECK(state->run.phase == RunPhase::Won);
    CHECK(countEvents(*state, EventType::BossKilled) == 1);
    CHECK(state->enemies.count == 0);
    // The run is over, so stepping does nothing more.
    const uint32_t tick = state->run.tick;
    sim::step(*state, InputFrame{});
    CHECK(state->run.tick == tick);
}

TEST_CASE("the boss takes no knockback") {
    auto state = makeRun();
    spawnBoss(*state, Vec2{300.0f, 0.0f});
    enemies::hit(*state, 0, 1.0f, WeaponId::Rifle, 0, Vec2{900.0f, 0.0f});
    CHECK(state->enemies[0].knock.x == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Pickups
// ---------------------------------------------------------------------------

TEST_CASE("a pickup is collected only inside the collect radius, and sends an event") {
    auto state = makeRun();
    state->firstPlayer().hp = 50.0f;
    state->pickups.add()->pos = Vec2{balance::kPickupCollectRadius + 20.0f, 0.0f};
    pickups::update(*state);
    CHECK(state->pickups.count == 1);

    state->pickups[0].pos = Vec2{balance::kPickupCollectRadius - 4.0f, 0.0f};
    state->pickups[0].type = PickupType::Heal;
    pickups::update(*state);
    CHECK(state->pickups.count == 0);
    REQUIRE(countEvents(*state, EventType::PickupCollected) == 1);
    CHECK(state->events[0].detail == static_cast<uint8_t>(PickupType::Heal));
}

TEST_CASE("heal pickup restores a share of max HP and never goes over it") {
    auto state = makeRun();
    state->firstPlayer().hp = 40.0f;
    Pickup* heal = state->pickups.add();
    heal->type = PickupType::Heal;
    pickups::update(*state);
    CHECK(state->firstPlayer().hp == doctest::Approx(40.0f + state->firstPlayer().maxHp * balance::kHealPickupFraction));

    state->firstPlayer().hp = state->firstPlayer().maxHp - 1.0f;
    state->pickups.add()->type = PickupType::Heal;
    pickups::update(*state);
    CHECK(state->firstPlayer().hp == doctest::Approx(state->firstPlayer().maxHp));
}

TEST_CASE("magnet pickup attracts every gem") {
    auto state = makeRun();
    for (int i = 0; i < 20; ++i) pickups::dropGem(*state, Vec2{1000.0f + 50.0f * static_cast<float>(i), 900.0f}, 1);
    state->pickups.add()->type = PickupType::Magnet;
    pickups::update(*state);
    CHECK(state->gems.count == 20);
    for (const Gem& gem : state->gems) CHECK(gem.attracted);
}

TEST_CASE("bomb damages zombies in range with no weapon credit, and leaves far ones alone") {
    auto state = makeRun();
    enemies::spawn(*state, EnemyType::Brute, Vec2{200.0f, 0.0f}, false);
    enemies::spawn(*state, EnemyType::Walker, Vec2{-300.0f, 100.0f}, false);
    enemies::spawn(*state, EnemyType::Brute, Vec2{balance::kBombRadius + 200.0f, 0.0f}, false);
    const float farHp = state->enemies[2].hp;
    state->pickups.add()->type = PickupType::Bomb;
    pickups::update(*state);

    CHECK(countEvents(*state, EventType::BombBlast) == 1);
    CHECK(state->enemies[0].hp <= 0.0f);
    CHECK(state->enemies[1].hp <= 0.0f);
    CHECK(state->enemies[2].hp == farHp);
    for (const float damage : state->run.damageByWeapon) CHECK(damage == 0.0f);

    enemies::removeDead(*state);
    CHECK(state->enemies.count == 1);
    CHECK(state->run.kills == 2);
}

TEST_CASE("bomb takes only a small share of the boss's HP") {
    auto state = makeRun();
    spawnBoss(*state, Vec2{200.0f, 0.0f});
    const float maxHp = state->enemies[0].maxHp;
    state->pickups.add()->type = PickupType::Bomb;
    pickups::update(*state);
    CHECK(maxHp - state->enemies[0].hp == doctest::Approx(maxHp * balance::kBombBossMaxHpFraction));
    CHECK(state->enemies[0].hp > 0.0f);
}

TEST_CASE("ammo pickup is collected and removed") {
    auto state = makeRun();
    state->pickups.add()->type = PickupType::Ammo;
    pickups::update(*state);
    CHECK(state->pickups.count == 0);
    REQUIRE(countEvents(*state, EventType::PickupCollected) == 1);
    CHECK(state->events[0].detail == static_cast<uint8_t>(PickupType::Ammo));
}

// ---------------------------------------------------------------------------
// Gems
// ---------------------------------------------------------------------------

TEST_CASE("gem merge keeps total XP, merges only within one cell, and leaves attracted gems alone") {
    auto state = makeRun();
    const float cell = balance::kGemMergeCellSize;
    // Three gems in map cell (0, 0).
    pickups::dropGem(*state, Vec2{10.0f, 10.0f}, 1);
    pickups::dropGem(*state, Vec2{20.0f, 20.0f}, 2);
    pickups::dropGem(*state, Vec2{30.0f, 30.0f}, 3);
    // Cell (17, -31) is far away but would collide in a naive small hash table.
    pickups::dropGem(*state, Vec2{17.0f * cell + 10.0f, -31.0f * cell + 10.0f}, 5);
    // An attracted gem in cell (0, 0) must stay as it is.
    pickups::dropGem(*state, Vec2{40.0f, 40.0f}, 7);
    state->gems[4].attracted = true;
    const int xpBefore = totalGemXp(*state);

    pickups::mergeGems(*state);
    CHECK(state->gems.count == 3);
    CHECK(totalGemXp(*state) == xpBefore);
    int farGems = 0;
    for (const Gem& gem : state->gems) {
        if (gem.pos.x > 1000.0f) {
            farGems++;
            CHECK(gem.xp == 5);
        }
    }
    CHECK(farGems == 1);
}

TEST_CASE("merging a full gem pool is fast, keeps total XP, and leaves one gem per cell") {
    auto state = makeRun();
    int cells = 0;
    bool occupied[40][40] = {};
    for (int i = 0; i < kMaxGems; ++i) {
        const int cx = state->rng.below(40);
        const int cy = state->rng.below(40);
        pickups::dropGem(*state, Vec2{(static_cast<float>(cx) + 0.5f) * balance::kGemMergeCellSize,
                                      (static_cast<float>(cy) + 0.5f) * balance::kGemMergeCellSize}, 1 + i % 3);
        if (!occupied[cx][cy]) {
            occupied[cx][cy] = true;
            cells++;
        }
    }
    REQUIRE(state->gems.count == kMaxGems);
    const int xpBefore = totalGemXp(*state);
    pickups::mergeGems(*state);
    CHECK(state->gems.count == cells);
    CHECK(totalGemXp(*state) == xpBefore);
}
