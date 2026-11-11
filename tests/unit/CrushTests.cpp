#include "doctest.h"

#include "components/Audio/Router/Router.h"
#include "components/Audio/Sfx/Sfx.h"
#include "components/Effects/ScreenShake/ScreenShake.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Enemies.h"
#include "components/Maps/Layout/Layout.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Pickups/Pickups.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using namespace ls;
using maps::CarId;
using maps::MapId;
using maps::Obstacle;

namespace {

constexpr MapId kBothMaps[] = {MapId::City, MapId::Wasteland};

std::vector<Obstacle> carsIn(MapId map, uint64_t seed, int x, int y) {
    Obstacle items[maps::kMaxObstaclesPerTile];
    const int count = maps::obstaclesInTile(map, seed, x, y, items, maps::kMaxObstaclesPerTile);
    return std::vector<Obstacle>(items, items + count);
}

float gapBetween(const Obstacle& car, Vec2 pos) {
    const float gapX = std::max(std::abs(pos.x - car.center.x) - car.half.x, 0.0f);
    const float gapY = std::max(std::abs(pos.y - car.center.y) - car.half.y, 0.0f);
    return std::sqrt(gapX * gapX + gapY * gapY);
}

// A car with nothing else near it, so a test can tell exactly what that one car does.
Obstacle findLoneCar(MapId map, uint64_t seed, float clearance) {
    for (int ring = 1; ring < 40; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                const std::vector<Obstacle> list = carsIn(map, seed, x, y);
                for (const Obstacle& car : list) {
                    if (car.half.x > car.half.y * 2.0f && car.half.x > 90.0f) continue;  // Long ones are harder to walk around.
                    bool alone = true;
                    for (const Obstacle& other : list) {
                        const bool same = other.texelX == car.texelX && other.texelY == car.texelY;
                        if (same) continue;
                        const float gapX = std::abs(other.center.x - car.center.x) - other.half.x - car.half.x;
                        const float gapY = std::abs(other.center.y - car.center.y) - other.half.y - car.half.y;
                        if (gapX < clearance && gapY < clearance) alone = false;
                    }
                    if (alone) return car;
                }
            }
        }
    }
    FAIL("no lone car on this map and seed");
    return Obstacle{};
}

// A run with the window of cars around one car, and nothing moving unless a test says so.
std::unique_ptr<GameState> runAround(MapId map, uint64_t seed, const Obstacle& car, Vec2 playerOffset) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, map);
    state->firstPlayer().weaponCount = 0;
    state->firstPlayer().xpToNext = 1 << 30;
    state->firstPlayer().pos = car.center + playerOffset;
    state->firstPlayer().prevPos = state->firstPlayer().pos;
    obstacles::update(*state);
    return state;
}

// Holds the world still apart from what a test does: no spawns and no damage.
void keepAlive(GameState& state) {
    state.firstPlayer().hp = state.firstPlayer().maxHp;
    state.firstPlayer().pendingLevels = 0;
    state.run.tick = 1;
    state.run.nextEliteEvent = 1 << 20;
    state.events.clear();
}

bool isCrushed(const GameState& state, const Obstacle& car) {
    return maps::CrushedCars{state.run.crushed, state.run.crushedCount}.holds(car.texelX, car.texelY);
}

void crushOnly(GameState& state, const Obstacle& car) {
    obstacles::crushUnder(state, car.center, 1.0f);
    REQUIRE(isCrushed(state, car));
}

// What the cache must hold for this tile, worked out from nothing but the hashed values.
std::vector<Obstacle> freshList(const GameState& state, int x, int y) {
    std::vector<Obstacle> kept;
    for (const Obstacle& car : carsIn(state.run.map, state.run.seed, x, y)) {
        if (!isCrushed(state, car)) kept.push_back(car);
    }
    return kept;
}

void checkCacheIsFresh(const GameState& state) {
    int checked = 0;
    for (const ObstacleTile& tile : state.obstacleCache.windows[0].tiles) {
        if (!tile.valid) continue;
        const std::vector<Obstacle> fresh = freshList(state, tile.tileX, tile.tileY);
        REQUIRE(tile.count == static_cast<int>(fresh.size()));
        for (int i = 0; i < tile.count; ++i) {
            CHECK(tile.items[i].texelX == fresh[static_cast<size_t>(i)].texelX);
            CHECK(tile.items[i].texelY == fresh[static_cast<size_t>(i)].texelY);
        }
        checked++;
    }
    CHECK(checked == kObstacleWindow * kObstacleWindow);
}

// Walks far enough for the whole window of tiles to be filled again, then comes back.
void walkAwayAndBack(GameState& state) {
    const Vec2 home = state.firstPlayer().pos;
    state.firstPlayer().pos = home + Vec2{maps::kChunkWorldSize * 7.0f, maps::kChunkWorldSize * 7.0f};
    obstacles::update(state);
    state.firstPlayer().pos = home;
    obstacles::update(state);
}

// A real boss fight, as far as cars are concerned: the boss at its real speed, a soldier
// circling and walking with the fight, and no way to kill it.
std::unique_ptr<GameState> bossFight(MapId map, uint64_t seed, int steps, std::vector<uint64_t>* hashes = nullptr, int* events = nullptr) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, map);
    state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 1;
    state->run.nextEliteEvent = 1 << 20;
    state->firstPlayer().xpToNext = 1 << 30;
    for (int step = 0; step < steps; ++step) {
        state->firstPlayer().hp = state->firstPlayer().maxHp;
        state->firstPlayer().pendingLevels = 0;
        InputFrame input;
        const float angle = static_cast<float>(step) * 0.004f;
        input.moveX = std::cos(angle);
        input.moveY = std::sin(angle);
        sim::step(*state, input);
        for (const SimEvent& event : state->events) {
            if (events && event.type == EventType::CarCrushed) ++*events;
        }
        state->events.clear();
        if (hashes && step % 60 == 0) hashes->push_back(sim::stateHash(*state));
    }
    return state;
}

}  // namespace

TEST_CASE("the boss crushes the car its body overlaps, and no other") {
    for (const MapId map : kBothMaps) {
        const Obstacle car = findLoneCar(map, 41, 200.0f);
        auto state = runAround(map, 41, car, Vec2{0.0f, car.half.y + 400.0f});
        keepAlive(*state);
        const float radius = balance::kEnemyStats[static_cast<int>(EnemyType::Boss)].radius;
        const float reach = radius - balance::kBossCrushOverlap;

        // Just short of the crush: the boss stands beside the car, and nothing happens yet.
        Enemy* boss = enemies::spawn(*state, EnemyType::Boss, car.center - Vec2{car.half.x + reach + 1.0f, 0.0f}, false);
        REQUIRE(boss);
        boss->speed = 0.0f;
        boss->timer = 1000.0f;
        enemies::update(*state);
        CHECK(state->run.crushedCount == 0);
        CHECK_FALSE(isCrushed(*state, car));

        // Past it: this car goes flat, and every car on the list was really under the boss.
        boss = &state->enemies[0];
        boss->pos = car.center - Vec2{car.half.x + reach - 1.0f, 0.0f};
        const Vec2 bossAt = boss->pos;
        enemies::update(*state);
        CHECK(isCrushed(*state, car));
        CHECK(state->run.crushedCount == 1);
        for (int i = 0; i < state->run.crushedCount; ++i) {
            for (const Obstacle& seen : carsIn(map, 41, maps::tileOf(bossAt.x), maps::tileOf(bossAt.y))) {
                if (maps::isCar(state->run.crushed[i], seen)) CHECK(gapBetween(seen, bossAt) < reach);
            }
        }
    }
}

TEST_CASE("only the boss crushes: an elite Brute standing on a car leaves it whole") {
    for (const MapId map : kBothMaps) {
        const Obstacle car = findLoneCar(map, 43, 200.0f);
        auto state = runAround(map, 43, car, Vec2{0.0f, car.half.y + 400.0f});
        keepAlive(*state);
        for (const EnemyType type : {EnemyType::Brute, EnemyType::Charger, EnemyType::Bloater, EnemyType::Walker}) {
            Enemy* zombie = enemies::spawn(*state, type, car.center, true);
            REQUIRE(zombie);
            zombie->pos = car.center;  // Spawning pushes it out, and this is the worst case.
        }
        for (int step = 0; step < 30; ++step) enemies::update(*state);
        CHECK(state->run.crushedCount == 0);
        CHECK(obstacles::overlapsCar(*state, car.center, 4.0f));
    }
}

TEST_CASE("a crushed car isn't solid for anyone, including after the cache fills again") {
    for (const MapId map : kBothMaps) {
        const Obstacle car = findLoneCar(map, 47, 200.0f);
        auto state = runAround(map, 47, car, Vec2{-car.half.x - 200.0f, 0.0f});
        const float radius = balance::kPlayerRadius;
        REQUIRE(obstacles::overlapsCar(*state, car.center, radius));
        crushOnly(*state, car);

        const auto checkAllClear = [&] {
            Vec2 pos = car.center;
            CHECK_FALSE(obstacles::overlapsCar(*state, pos, radius));
            CHECK_FALSE(obstacles::resolveCircle(*state, pos, radius).hit);
            CHECK(pos.x == car.center.x);
            CHECK(pos.y == car.center.y);

            // The player walks right across it, in one straight line.
            const Vec2 from = car.center - Vec2{car.half.x + 60.0f, 0.0f};
            const Vec2 to = car.center + Vec2{car.half.x + 60.0f, 0.0f};
            const Vec2 swept = obstacles::sweepCircle(*state, from, to, radius);
            CHECK(swept.x == doctest::Approx(to.x));
            CHECK(swept.y == doctest::Approx(to.y));

            // Zombies see nothing to go around, and charges see a clear line.
            const Vec2 wanted{70.0f, 0.0f};
            const Vec2 steered = obstacles::steerAround(*state, from, 8.0f, wanted, to, 1u);
            CHECK(steered.x == wanted.x);
            CHECK(steered.y == wanted.y);
            CHECK_FALSE(obstacles::lineBlocked(*state, from, to, 8.0f));
            CHECK(obstacles::distanceToNearestCar(*state, car.center, 5.0f) == doctest::Approx(5.0f));

            // A gem, a pickup or a mine lands right on it.
            const Vec2 placed = obstacles::placeItem(*state, car.center);
            CHECK(placed.x == car.center.x);
            CHECK(placed.y == car.center.y);
        };
        checkAllClear();
        walkAwayAndBack(*state);
        checkAllClear();
        checkCacheIsFresh(*state);
        CHECK(state->run.crushedCount == 1);
    }
}

TEST_CASE("a real dropped item lands on a crushed car, and beside a whole one") {
    const MapId map = MapId::City;
    const Obstacle car = findLoneCar(map, 53, 200.0f);
    auto state = runAround(map, 53, car, Vec2{-car.half.x - 200.0f, 0.0f});
    pickups::spawnPickup(*state, car.center, PickupType::Heal);
    REQUIRE(state->pickups.count == 1);
    CHECK(obstacles::overlapsCar(*state, car.center, balance::kItemCarRadius));
    CHECK(distance(state->pickups[0].pos, car.center) > 1.0f);
    crushOnly(*state, car);
    pickups::spawnPickup(*state, car.center, PickupType::Heal);
    REQUIRE(state->pickups.count == 2);
    CHECK(state->pickups[1].pos.x == car.center.x);
    CHECK(state->pickups[1].pos.y == car.center.y);
}

TEST_CASE("a car over a tile's edge leaves every list it was in") {
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 61; seed < 70; ++seed) {
            // A car in the lists of two tiles at least.
            Obstacle car;
            bool found = false;
            for (int y = -4; y <= 4 && !found; ++y) {
                for (int x = -4; x <= 4 && !found; ++x) {
                    for (const Obstacle& candidate : carsIn(map, seed, x, y)) {
                        int lists = 0;
                        for (int oy = y - 1; oy <= y + 1; ++oy) {
                            for (int ox = x - 1; ox <= x + 1; ++ox) {
                                for (const Obstacle& seen : carsIn(map, seed, ox, oy)) lists += seen.texelX == candidate.texelX && seen.texelY == candidate.texelY ? 1 : 0;
                            }
                        }
                        if (lists >= 2) {
                            car = candidate;
                            found = true;
                        }
                    }
                }
            }
            REQUIRE(found);
            auto state = runAround(map, seed, car, Vec2{-car.half.x - 100.0f, 0.0f});
            crushOnly(*state, car);
            for (const ObstacleTile& tile : state->obstacleCache.windows[0].tiles) {
                if (!tile.valid) continue;
                for (int i = 0; i < tile.count; ++i) CHECK_FALSE(maps::isCar(CarId{car.texelX, car.texelY}, tile.items[i]));
            }
            checkCacheIsFresh(*state);
        }
    }
}

TEST_CASE("the cache stays what a fresh look would give, whatever order cars were crushed in") {
    for (const MapId map : kBothMaps) {
        // The same cars in two orders. A list that swapped items when removing would differ.
        std::vector<Obstacle> cars;
        for (int y = -2; y <= 2; ++y) {
            for (int x = -2; x <= 2; ++x) {
                for (const Obstacle& car : carsIn(map, 71, x, y)) cars.push_back(car);
            }
        }
        REQUIRE(cars.size() >= 6);
        auto forward = runAround(map, 71, cars[0], Vec2{-cars[0].half.x - 100.0f, 0.0f});
        for (size_t i = 0; i < cars.size(); i += 2) obstacles::crushUnder(*forward, cars[i].center, 1.0f);
        checkCacheIsFresh(*forward);
        auto backward = runAround(map, 71, cars[0], Vec2{-cars[0].half.x - 100.0f, 0.0f});
        for (size_t i = (cars.size() - 1) / 2 + 1; i-- > 0;) obstacles::crushUnder(*backward, cars[i * 2].center, 1.0f);
        checkCacheIsFresh(*backward);
        walkAwayAndBack(*forward);
        checkCacheIsFresh(*forward);
    }
}

TEST_CASE("other cars are still solid after a crush") {
    for (const MapId map : kBothMaps) {
        const Obstacle crushed = findLoneCar(map, 73, 200.0f);
        auto state = runAround(map, 73, crushed, Vec2{-crushed.half.x - 100.0f, 0.0f});
        crushOnly(*state, crushed);
        // Every other car around the player, which is what the cache can answer for.
        int checked = 0;
        const int playerTileX = maps::tileOf(state->firstPlayer().pos.x);
        const int playerTileY = maps::tileOf(state->firstPlayer().pos.y);
        for (int y = playerTileY - 1; y <= playerTileY + 1; ++y) {
            for (int x = playerTileX - 1; x <= playerTileX + 1; ++x) {
                for (const Obstacle& car : carsIn(map, 73, x, y)) {
                    if (maps::isCar(CarId{crushed.texelX, crushed.texelY}, car)) continue;
                    if (maps::tileOf(car.center.x) != x || maps::tileOf(car.center.y) != y) continue;
                    CHECK(obstacles::overlapsCar(*state, car.center, 4.0f));
                    CHECK(obstacles::lineBlocked(*state, car.center - Vec2{car.half.x + 30.0f, 0.0f}, car.center + Vec2{car.half.x + 30.0f, 0.0f}, 4.0f));
                    checked++;
                }
            }
        }
        CHECK(checked > 0);
    }
}

TEST_CASE("a zombie walks straight over a crushed car, and around a whole one") {
    for (const MapId map : kBothMaps) {
        const Obstacle car = findLoneCar(map, 79, 200.0f);
        const auto walkAcross = [&](bool crushed) {
            auto state = runAround(map, 79, car, Vec2{car.half.x + 150.0f, 0.0f});
            Enemy* walker = enemies::spawn(*state, EnemyType::Walker, car.center - Vec2{car.half.x + 120.0f, 0.0f}, false);
            REQUIRE(walker);
            if (crushed) crushOnly(*state, car);
            float widest = 0.0f;
            bool crossedTheMiddle = false;
            for (int step = 0; step < 60 * 8; ++step) {
                keepAlive(*state);
                sim::step(*state, InputFrame{});
                if (state->enemies.count == 0) break;
                const Vec2 at = state->enemies[0].pos;
                widest = std::max(widest, std::abs(at.y - car.center.y));
                crossedTheMiddle = crossedTheMiddle || at.x > car.center.x;
            }
            CHECK(crossedTheMiddle);
            return widest;
        };
        const float over = walkAcross(true);
        const float around = walkAcross(false);
        CAPTURE(over);
        CAPTURE(around);
        CHECK(over < 1.0f);  // Dead straight: nothing moved it sideways.
        CHECK(around > car.half.y);  // The same walk has to leave the line to get around.
    }
}

TEST_CASE("a Charger can start a charge over a crushed car, and doesn't crash on it") {
    for (const MapId map : kBothMaps) {
        const Obstacle car = findLoneCar(map, 83, 200.0f);
        const auto charge = [&](bool crushed) {
            auto state = runAround(map, 83, car, Vec2{0.0f, car.half.y + balance::kPlayerRadius + 2.0f});
            Enemy* charger = enemies::spawn(*state, EnemyType::Charger, car.center - Vec2{0.0f, car.half.y + 60.0f}, false);
            REQUIRE(charger);
            if (crushed) crushOnly(*state, car);
            const bool lineClear = !obstacles::lineBlocked(*state, charger->pos, state->firstPlayer().pos, charger->radius * balance::kZombieCarRadiusShare);
            charger->state = enemies::kChargerDash;
            charger->dir = Vec2{0.0f, 1.0f};
            charger->timer = 2.0f;
            bool crashed = false;
            float furthest = charger->pos.y;
            for (int step = 0; step < 30 && !crashed; ++step) {
                keepAlive(*state);
                state->firstPlayer().invulnerable = 10.0f;
                sim::step(*state, InputFrame{});
                if (state->enemies.count == 0) break;
                crashed = state->enemies[0].state == enemies::kChargerRecover;
                furthest = std::max(furthest, state->enemies[0].pos.y);
            }
            return std::pair<bool, bool>{lineClear, crashed || furthest < car.center.y};
        };
        const auto [clearOver, stoppedOver] = charge(true);
        CHECK(clearOver);
        CHECK_FALSE(stoppedOver);
        const auto [clearWhole, stoppedWhole] = charge(false);
        CHECK_FALSE(clearWhole);
        CHECK(stoppedWhole);
    }
}

TEST_CASE("a new run forgets crushed cars, and so does a change of map") {
    const Obstacle car = findLoneCar(MapId::City, 89, 200.0f);
    auto state = runAround(MapId::City, 89, car, Vec2{-car.half.x - 100.0f, 0.0f});
    crushOnly(*state, car);
    sim::newRun(*state, 89, Difficulty::Normal, MapId::City);
    CHECK(state->run.crushedCount == 0);
    state->firstPlayer().pos = car.center - Vec2{car.half.x + 100.0f, 0.0f};
    obstacles::update(*state);
    CHECK(obstacles::overlapsCar(*state, car.center, 4.0f));
    checkCacheIsFresh(*state);

    crushOnly(*state, car);
    sim::setMap(*state, MapId::Wasteland);
    CHECK(state->run.crushedCount == 0);
}

TEST_CASE("the crushed list is in the state hash") {
    const Obstacle car = findLoneCar(MapId::City, 97, 200.0f);
    auto state = runAround(MapId::City, 97, car, Vec2{-car.half.x - 100.0f, 0.0f});
    const uint64_t whole = sim::stateHash(*state);
    crushOnly(*state, car);
    const uint64_t flat = sim::stateHash(*state);
    CHECK(flat != whole);

    // The same number of crushed cars, but a different car.
    const CarId real = state->run.crushed[0];
    state->run.crushed[0] = CarId{real.texelX + 1, real.texelY};
    CHECK(sim::stateHash(*state) != flat);
    state->run.crushed[0] = CarId{real.texelX, real.texelY + 1};
    CHECK(sim::stateHash(*state) != flat);
    state->run.crushed[0] = real;
    CHECK(sim::stateHash(*state) == flat);
}

TEST_CASE("each crushed car pushes one event, with its place, even in a flood of hits") {
    for (const MapId map : kBothMaps) {
        const Obstacle car = findLoneCar(map, 101, 200.0f);
        auto state = runAround(map, 101, car, Vec2{-car.half.x - 300.0f, 0.0f});
        while (state->events.count < kEventLimitCommon) pushEvent(*state, EventType::WeaponFired, Vec2{});
        while (state->events.count < kEventLimitCommon + 8) state->events.add();
        const int before = state->events.count;
        obstacles::crushUnder(*state, car.center, 1.0f);
        REQUIRE(state->events.count == before + 1);
        const SimEvent& event = state->events[before];
        CHECK(event.type == EventType::CarCrushed);
        CHECK(event.pos.x == car.center.x);
        CHECK(event.pos.y == car.center.y);
        CHECK(event.detail == static_cast<uint8_t>(car.sprite));
        CHECK(event.value == doctest::Approx(distance(car.center, state->firstPlayer().pos)));

        // It can't go flat twice, however long the boss stands on it.
        obstacles::crushUnder(*state, car.center, 1.0f);
        CHECK(state->events.count == before + 1);
        CHECK(state->run.crushedCount == 1);
    }
}

TEST_CASE("in a real fight, the events and the list agree and no car is listed twice") {
    for (const MapId map : kBothMaps) {
        int events = 0;
        auto state = bossFight(map, 103, 60 * 90, nullptr, &events);
        CHECK(state->run.bossSpawned);
        CHECK(state->run.crushedCount > 0);
        CHECK(events == state->run.crushedCount);
        for (int i = 0; i < state->run.crushedCount; ++i) {
            for (int k = i + 1; k < state->run.crushedCount; ++k) {
                CHECK_FALSE((state->run.crushed[i].texelX == state->run.crushed[k].texelX && state->run.crushed[i].texelY == state->run.crushed[k].texelY));
            }
        }
        checkCacheIsFresh(*state);
    }
}

TEST_CASE("a boss fight with crushing plays out the same every time, on both maps") {
    for (const MapId map : kBothMaps) {
        std::vector<uint64_t> first;
        std::vector<uint64_t> second;
        auto a = bossFight(map, 107, 60 * 75, &first);
        auto b = bossFight(map, 107, 60 * 75, &second);
        CHECK(a->run.crushedCount > 0);
        CHECK(first == second);
        CHECK(sim::stateHash(*a) == sim::stateHash(*b));
        REQUIRE(a->run.crushedCount == b->run.crushedCount);
        for (int i = 0; i < a->run.crushedCount; ++i) {
            CHECK(a->run.crushed[i].texelX == b->run.crushed[i].texelX);
            CHECK(a->run.crushed[i].texelY == b->run.crushed[i].texelY);
        }
    }
}

TEST_CASE("when the crushed list is full, a car stays whole and solid and nothing overflows") {
    const Obstacle car = findLoneCar(MapId::City, 109, 200.0f);
    auto state = runAround(MapId::City, 109, car, Vec2{-car.half.x - 100.0f, 0.0f});
    state->run.crushedCount = kMaxCrushedCars;
    for (int i = 0; i < kMaxCrushedCars; ++i) state->run.crushed[i] = CarId{1000000 + i, 1000000};
    const uint64_t before = sim::stateHash(*state);
    obstacles::crushUnder(*state, car.center, 1.0f);
    CHECK(state->run.crushedCount == kMaxCrushedCars);
    CHECK_FALSE(isCrushed(*state, car));
    CHECK(obstacles::overlapsCar(*state, car.center, 4.0f));
    CHECK(state->events.count == 0);
    CHECK(sim::stateHash(*state) == before);
}

TEST_CASE("the fastest boss in a very long fight can't fill the crushed list") {
    // The arithmetic behind kMaxCrushedCars. Balance.h works out that the boss can cover at
    // most 3040 units per 9 second cycle, about 338 units a second. Fights last about 80
    // seconds in the balance runs. This one lasts 15 minutes at that top speed, in a straight
    // line, so it never meets the same car twice and finds as many new cars as anything can.
    constexpr float kFastestBossPerSecond = 3040.0f / 9.0f;
    constexpr int kSteps = 60 * 60 * 15;
    const float reach = balance::kEnemyStats[static_cast<int>(EnemyType::Boss)].radius - balance::kBossCrushOverlap;
    int most = 0;
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 111; seed < 121; ++seed) {
            for (const Vec2 heading : {Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}, Vec2{0.8f, 0.6f}}) {
                auto state = std::make_unique<GameState>();
                sim::newRun(*state, seed, Difficulty::Normal, map);
                Vec2 at = state->firstPlayer().pos;
                for (int step = 0; step < kSteps; ++step) {
                    at += heading * (kFastestBossPerSecond * kDt);
                    state->firstPlayer().pos = at;
                    obstacles::update(*state);
                    obstacles::crushUnder(*state, at, reach);
                    if (step % 256 == 0) state->events.clear();
                }
                most = std::max(most, state->run.crushedCount);
            }
        }
    }
    MESSAGE("the most cars crushed in 15 minutes at top speed is ", most, " of ", kMaxCrushedCars);
    CHECK(most * 2 < kMaxCrushedCars);
}

TEST_CASE("a crush plays one crunch, limited in rate, louder when close and silent when far") {
    audio::router::Router router;
    audio::router::PlayRequest request;
    SimEvent event;
    event.type = EventType::CarCrushed;
    event.value = 0.0f;
    float now = 100.0f;
    REQUIRE(router.route(event, now, request));
    CHECK(request.id == audio::sfx::SfxId::CarCrush);
    const float closeVolume = request.volume;

    // Several cars in a row don't stack up. Once the limit has passed it plays again.
    CHECK_FALSE(router.route(event, now + 0.05f, request));
    now += audio::sfx::info(audio::sfx::SfxId::CarCrush).minInterval + 0.01f;
    event.value = balance::kCarCrushHeardWithin * 0.8f;
    REQUIRE(router.route(event, now, request));
    CHECK(request.volume < closeVolume);

    now += 10.0f;
    event.value = balance::kCarCrushHeardWithin + 1.0f;
    CHECK_FALSE(router.route(event, now, request));
}

TEST_CASE("the crunch is a real sound: long enough, heavy at the start, and never clipping") {
    const audio::synth::Samples crunch = audio::sfx::renderSfx(audio::sfx::SfxId::CarCrush);
    REQUIRE(crunch.size() > static_cast<size_t>(audio::synth::kSampleRate / 4));
    float peak = 0.0f;
    float energyStart = 0.0f;
    float energyEnd = 0.0f;
    const size_t tenth = crunch.size() / 10;
    for (size_t i = 0; i < crunch.size(); ++i) {
        const float sample = crunch[i];
        REQUIRE(std::isfinite(sample));
        peak = std::max(peak, std::abs(sample));
        if (i < tenth * 3) energyStart += sample * sample;
        if (i >= tenth * 7) energyEnd += sample * sample;
    }
    CHECK(peak > 0.3f);
    CHECK(peak <= 1.0f);
    CHECK(energyStart > energyEnd * 4.0f);
}

TEST_CASE("crushes in a row can't build up a big shake, or add to one already going") {
    effects::ScreenShake shake;
    for (int i = 0; i < 20; ++i) shake.raiseTo(0.4f);
    CHECK(shake.trauma() == doctest::Approx(0.4f));
    shake.add(0.9f);
    const float big = shake.trauma();
    shake.raiseTo(0.4f);
    CHECK(shake.trauma() == doctest::Approx(big));
}
