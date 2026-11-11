#include "doctest.h"

#include "components/Enemies/Bloater/Bloater.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Enemies/Enemies.h"
#include "components/Maps/Layout/Layout.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Pickups/Pickups.h"
#include "components/Weapons/Weapons.h"
#include "components/WorldView/DashPath.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace ls;
using maps::MapId;
using maps::Obstacle;

namespace {

constexpr MapId kBothMaps[] = {MapId::City, MapId::Wasteland};

std::vector<Obstacle> listOf(MapId map, uint64_t seed, int x, int y) {
    Obstacle items[maps::kMaxObstaclesPerTile];
    const int count = maps::obstaclesInTile(map, seed, x, y, items, maps::kMaxObstaclesPerTile);
    return std::vector<Obstacle>(items, items + count);
}

bool same(const std::vector<Obstacle>& a, const std::vector<Obstacle>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].texelX != b[i].texelX || a[i].texelY != b[i].texelY || a[i].sprite != b[i].sprite || a[i].center.x != b[i].center.x || a[i].center.y != b[i].center.y ||
            a[i].half.x != b[i].half.x || a[i].half.y != b[i].half.y) {
            return false;
        }
    }
    return true;
}

// A run whose cache holds the area around a real car, with nothing else happening.
std::unique_ptr<GameState> runBeside(MapId map, uint64_t seed, Obstacle* car) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, map);
    state->firstPlayer().weaponCount = 0;
    state->firstPlayer().xpToNext = 1 << 30;
    for (int ring = 1; ring < 40; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                const std::vector<Obstacle> list = listOf(map, seed, x, y);
                if (list.empty()) continue;
                *car = list[0];
                // Beside the car and clear of it, so the first step starts from a legal place.
                state->firstPlayer().pos = car->center - Vec2{car->half.x + balance::kPlayerRadius + 40.0f, 0.0f};
                state->firstPlayer().prevPos = state->firstPlayer().pos;
                obstacles::update(*state);
                obstacles::resolveCircle(*state, state->firstPlayer().pos, balance::kPlayerRadius);
                return state;
            }
        }
    }
    FAIL("no car found on this map and seed");
    return state;
}

// Stands the player at this spot, or as near as it can along a direction if other cars are
// in the way, since wrecks in a pileup can overlap.
void standClear(GameState& state, Vec2 spot, Vec2 away) {
    state.firstPlayer().pos = spot;
    obstacles::update(state);
    while (obstacles::overlapsCar(state, state.firstPlayer().pos, balance::kPlayerRadius)) state.firstPlayer().pos += away * 4.0f;
    state.firstPlayer().prevPos = state.firstPlayer().pos;
}

// Holds the world still apart from what a test does: no spawns and no damage.
void keepAlive(GameState& state) {
    state.firstPlayer().hp = state.firstPlayer().maxHp;
    state.firstPlayer().pendingLevels = 0;
    state.run.tick = 1;  // The first moments of a run spawn next to nothing.
    state.run.nextEliteEvent = 1 << 20;
    state.events.clear();
}

float carGapOf(const Obstacle& car, Vec2 pos) {
    const float gapX = std::abs(pos.x - car.center.x) - car.half.x;
    const float gapY = std::abs(pos.y - car.center.y) - car.half.y;
    return std::max(gapX, gapY);
}

}  // namespace

TEST_CASE("cars are decided by the map, the seed and the tile, and nothing else") {
    for (const MapId map : kBothMaps) {
        int differing = 0;
        int total = 0;
        for (int y = -4; y <= 4; ++y) {
            for (int x = -4; x <= 4; ++x) {
                const std::vector<Obstacle> first = listOf(map, 11, x, y);
                CHECK(same(first, listOf(map, 11, x, y)));
                total += static_cast<int>(first.size());
                if (!same(first, listOf(map, 12, x, y))) ++differing;
            }
        }
        CHECK_MESSAGE(total > 0, "a map with no cars at all can't test anything");
        CHECK_MESSAGE(differing > 0, "another seed must put the cars elsewhere");
    }
    // The same place on the other map is a different list.
    int differing = 0;
    for (int x = -6; x <= 6; ++x) {
        if (!same(listOf(MapId::City, 11, x, 1), listOf(MapId::Wasteland, 11, x, 1))) ++differing;
    }
    CHECK(differing > 0);
}

TEST_CASE("a tile's list never runs out of room, so the limit can't change what is solid") {
    int busiest = 0;
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 1; seed <= 200; ++seed) {
            for (int i = 0; i < 10; ++i) {
                const int x = static_cast<int>(seed * 31 % 200) - 100 + i * 3;
                const int y = static_cast<int>(seed * 17 % 200) - 100 + i * 5;
                busiest = std::max(busiest, static_cast<int>(listOf(map, seed, x, y).size()));
            }
        }
    }
    MESSAGE("busiest tile holds ", busiest, " of ", maps::kMaxObstaclesPerTile);
    CHECK(busiest < maps::kMaxObstaclesPerTile);
}

TEST_CASE("no car stands near the start of a run") {
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 1; seed <= 200; ++seed) {
            for (int y = -1; y <= 0; ++y) {
                for (int x = -1; x <= 0; ++x) {
                    for (const Obstacle& car : listOf(map, seed, x, y)) {
                        const float gapX = std::max(std::abs(car.center.x) - car.half.x, 0.0f);
                        const float gapY = std::max(std::abs(car.center.y) - car.half.y, 0.0f);
                        CHECK(gapX * gapX + gapY * gapY >= balance::kCarClearOfStart * balance::kCarClearOfStart);
                    }
                }
            }
        }
    }
}

TEST_CASE("after walking around, the cache always equals a fresh computation") {
    for (const MapId map : kBothMaps) {
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 77, Difficulty::Normal, map);
        state->firstPlayer().weaponCount = 0;
        for (int step = 0; step < 600; ++step) {
            // Long jumps, so tiles leave and re-enter the window in every direction.
            state->firstPlayer().pos = Vec2{std::sin(static_cast<float>(step) * 0.3f) * 5000.0f, std::cos(static_cast<float>(step) * 0.17f) * 4000.0f};
            obstacles::update(*state);
            const ObstacleCache& cache = state->obstacleCache;
            for (const ObstacleTile& tile : cache.windows[0].tiles) {
                REQUIRE(tile.valid);
                CHECK(std::abs(tile.tileX - cache.windows[0].centerX) <= kObstacleWindow / 2);
                CHECK(std::abs(tile.tileY - cache.windows[0].centerY) <= kObstacleWindow / 2);
                const std::vector<Obstacle> held(tile.items, tile.items + tile.count);
                CHECK(same(held, listOf(map, 77, tile.tileX, tile.tileY)));
            }
        }
    }
}

TEST_CASE("resolving a circle moves it out by the nearest side, and leaves it clear") {
    Obstacle car;
    auto state = runBeside(MapId::City, 5, &car);
    const float radius = 14.0f;
    // A point just inside each side comes out through that side.
    const struct {
        Vec2 at;
        Vec2 way;
    } cases[] = {
        {car.center + Vec2{car.half.x - 2.0f, 0.0f}, Vec2{1.0f, 0.0f}},
        {car.center - Vec2{car.half.x - 2.0f, 0.0f}, Vec2{-1.0f, 0.0f}},
        {car.center + Vec2{0.0f, car.half.y - 2.0f}, Vec2{0.0f, 1.0f}},
        {car.center - Vec2{0.0f, car.half.y - 2.0f}, Vec2{0.0f, -1.0f}},
    };
    for (const auto& c : cases) {
        Vec2 pos = c.at;
        const obstacles::Push push = obstacles::resolveCircle(*state, pos, radius);
        CHECK(push.hit);
        CHECK(dot(pos - c.at, c.way) > 0.0f);
        CHECK_FALSE(obstacles::overlapsCar(*state, pos, radius - 0.01f));
    }
    // A circle already clear is left alone.
    Vec2 clear = car.center + Vec2{car.half.x + radius + 30.0f, 0.0f};
    const Vec2 before = clear;
    if (!obstacles::overlapsCar(*state, clear, radius)) {
        CHECK_FALSE(obstacles::resolveCircle(*state, clear, radius).hit);
        CHECK(clear.x == before.x);
        CHECK(clear.y == before.y);
    }
}

TEST_CASE("a circle sliding past a corner keeps moving along, and does not snag") {
    Obstacle car;
    auto state = runBeside(MapId::City, 5, &car);
    const float radius = 14.0f;
    // Walking along the car's top side and pressing into it a little, past the corner.
    Vec2 pos = car.center + Vec2{-car.half.x - radius - 4.0f, -car.half.y - radius + 1.0f};
    const float startX = pos.x;
    const float speedPerStep = 3.8f;
    int steps = 0;
    while (pos.x < car.center.x + car.half.x + radius + 4.0f && steps < 400) {
        pos = obstacles::sweepCircle(*state, pos, pos + Vec2{speedPerStep, 0.6f}, radius);
        ++steps;
    }
    const float travelled = pos.x - startX;
    // No slower than three quarters of full speed, all the way along.
    CHECK(travelled / static_cast<float>(steps) > speedPerStep * 0.75f);
}

TEST_CASE("the player can't walk into a car from any direction, at any stick strength") {
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Obstacle car;
            auto state = runBeside(map, seed, &car);
            const Vec2 start = car.center;
            for (const float strength : {0.3f, 1.0f}) {
                for (int angle = 0; angle < 16; ++angle) {
                    const Vec2 from = fromAngle(kTau * static_cast<float>(angle) / 16.0f);
                    standClear(*state, start + Vec2{from.x * (car.half.x + 120.0f), from.y * (car.half.y + 120.0f)}, from);
                    for (int step = 0; step < 90; ++step) {
                        keepAlive(*state);
                        state->enemies.clear();
                        const Vec2 toCar = normalizedOr(start - state->firstPlayer().pos, Vec2{1.0f, 0.0f});
                        InputFrame input;
                        input.moveX = toCar.x * strength;
                        input.moveY = toCar.y * strength;
                        sim::step(*state, input);
                        CHECK_FALSE(obstacles::overlapsCar(*state, state->firstPlayer().pos, balance::kPlayerRadius - 0.5f));
                    }
                }
            }
        }
    }
}

TEST_CASE("walking at a car at an angle slides along it, so a stick never feels stuck") {
    Obstacle car;
    auto state = runBeside(MapId::City, 5, &car);
    standClear(*state, car.center + Vec2{-car.half.x * 0.6f, -car.half.y - balance::kPlayerRadius - 1.0f}, Vec2{0.0f, -1.0f});
    const float startX = state->firstPlayer().pos.x;
    for (int step = 0; step < 20; ++step) {
        keepAlive(*state);
        state->enemies.clear();
        InputFrame input;
        input.moveX = 0.7f;
        input.moveY = 0.7f;
        sim::step(*state, input);
    }
    const float expected = 0.7f * balance::kPlayerSpeed * kDt * 20.0f;
    CHECK(state->firstPlayer().pos.x - startX > expected * 0.9f);
}

TEST_CASE("a dash stops or slides at a car, and never ends inside or beyond it") {
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Obstacle car;
            auto state = runBeside(map, seed, &car);
            // Head on at the car's long side, from close enough that a dash would cross it.
            standClear(*state, car.center - Vec2{0.0f, car.half.y + balance::kPlayerRadius + 20.0f}, Vec2{0.0f, -1.0f});
            InputFrame input;
            input.moveY = 1.0f;
            input.dash = true;
            for (int step = 0; step < 20; ++step) {
                keepAlive(*state);
                state->enemies.clear();
                sim::step(*state, input);
                input.dash = false;
                CHECK_FALSE(obstacles::overlapsCar(*state, state->firstPlayer().pos, balance::kPlayerRadius - 0.5f));
            }
            CHECK_MESSAGE(state->firstPlayer().pos.y < car.center.y, "the dash must not have crossed the car");
        }
    }
}

TEST_CASE("nothing appears inside a car") {
    for (const MapId map : kBothMaps) {
        Obstacle car;
        auto state = runBeside(map, 3, &car);
        Enemy* enemy = enemies::spawn(*state, EnemyType::Walker, car.center, false);
        REQUIRE(enemy);
        CHECK(carGapOf(car, enemy->pos) >= enemy->radius * balance::kZombieCarRadiusShare - 0.01f);
        CHECK(enemy->prevPos.x == enemy->pos.x);

        pickups::dropGem(*state, car.center, 1);
        REQUIRE(state->gems.count == 1);
        CHECK(carGapOf(car, state->gems[0].pos) >= 0.0f);

        pickups::spawnPickup(*state, car.center, PickupType::Heal);
        REQUIRE(state->pickups.count == 1);
        const Vec2 dropped = state->pickups[0].pos;
        CHECK(carGapOf(car, dropped) >= 0.0f);

        // And what lands beside a car can be collected, by walking around to it.
        state->firstPlayer().hp = state->firstPlayer().maxHp * 0.5f;
        for (int step = 0; step < 240 && state->pickups.count > 0; ++step) {
            state->enemies.clear();
            state->firstPlayer().hp = std::min(state->firstPlayer().hp, state->firstPlayer().maxHp * 0.5f);
            const Vec2 straight = normalizedOr(dropped - state->firstPlayer().pos, Vec2{1.0f, 0.0f});
            const Vec2 toward = obstacles::steerAround(*state, state->firstPlayer().pos, balance::kPlayerRadius, straight, dropped, 1u);
            InputFrame input;
            input.moveX = toward.x;
            input.moveY = toward.y;
            sim::step(*state, input);
            state->events.clear();
            state->firstPlayer().pendingLevels = 0;
        }
        CHECK(state->pickups.count == 0);
    }
}

TEST_CASE("a mine is never laid inside a car") {
    for (const MapId map : kBothMaps) {
        Obstacle car;
        auto state = runBeside(map, 3, &car);
        standClear(*state, car.center - Vec2{0.0f, car.half.y + balance::kPlayerRadius + 1.0f}, Vec2{0.0f, -1.0f});
        state->firstPlayer().facing = Vec2{0.0f, -1.0f};  // So behind him is the car.
        weapons::grant(*state, 0, WeaponId::Landmines);
        enemies::spawn(*state, EnemyType::Walker, state->firstPlayer().pos - Vec2{0.0f, 200.0f}, false);
        for (int step = 0; step < 200; ++step) {
            keepAlive(*state);
            sim::step(*state, InputFrame{});
            for (const Projectile& shot : state->projectiles) {
                if (shot.kind == ProjectileKind::Mine) CHECK(carGapOf(car, shot.pos) >= 0.0f);
            }
        }
    }
}

TEST_CASE("no zombie is ever inside a car, however it got where it is") {
    // Over a busy stretch of a real run: spawned, recycled, pushed by the crowd, knocked back,
    // and burst out of a Bloater. The check uses the same size as the collision.
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 1; seed <= 3; ++seed) {
            auto state = std::make_unique<GameState>();
            sim::newRun(*state, seed, Difficulty::Normal, map);
            state->firstPlayer().xpToNext = 1 << 30;
            state->run.tick = 60 * 240;  // A thick part of the wave table.
            for (int step = 0; step < 3000; ++step) {
                state->firstPlayer().hp = state->firstPlayer().maxHp;
                const float angle = static_cast<float>(step) * 0.004f;
                InputFrame input;
                input.moveX = std::cos(angle);
                input.moveY = std::sin(angle);
                sim::step(*state, input);
                state->events.clear();
                if (step % 10 != 0) continue;
                for (const Enemy& enemy : state->enemies) {
                    if (enemy.type == EnemyType::Boss) continue;
                    if (obstacles::overlapsCar(*state, enemy.pos, enemy.radius * balance::kZombieCarRadiusShare - 0.5f)) {
                        FAIL_CHECK("zombie inside a car at step ", step);
                    }
                }
            }
        }
    }
}

TEST_CASE("the boss walks straight through cars") {
    Obstacle car;
    auto state = runBeside(MapId::City, 5, &car);
    Enemy* boss = enemies::spawn(*state, EnemyType::Boss, car.center, false);
    REQUIRE(boss);
    CHECK(boss->pos.x == car.center.x);
    CHECK(boss->pos.y == car.center.y);
    const uint32_t id = boss->id;
    state->firstPlayer().pos = car.center + Vec2{car.half.x + 200.0f, 0.0f};
    state->firstPlayer().prevPos = state->firstPlayer().pos;
    for (int step = 0; step < 2; ++step) {
        keepAlive(*state);
        sim::step(*state, InputFrame{});
    }
    for (const Enemy& enemy : state->enemies) {
        if (enemy.id == id) CHECK(std::abs(enemy.pos.x - car.center.x) < 10.0f);
    }
}

TEST_CASE("a dashing Charger crashes into a car, and is stunned") {
    for (const MapId map : kBothMaps) {
        Obstacle car;
        auto state = runBeside(map, 9, &car);
        // The player stands on the far side, so the locked dash runs straight into the car.
        state->firstPlayer().pos = car.center + Vec2{0.0f, car.half.y + balance::kPlayerRadius + 2.0f};
        state->firstPlayer().prevPos = state->firstPlayer().pos;
        Enemy* charger = enemies::spawn(*state, EnemyType::Charger, car.center - Vec2{0.0f, car.half.y + 60.0f}, false);
        REQUIRE(charger);
        const uint32_t id = charger->id;
        charger->state = enemies::kChargerDash;
        charger->dir = Vec2{0.0f, 1.0f};
        charger->timer = balance::kChargerDashSeconds;
        bool crashed = false;
        for (int step = 0; step < 40 && !crashed; ++step) {
            keepAlive(*state);
            sim::step(*state, InputFrame{});
            for (const Enemy& enemy : state->enemies) {
                if (enemy.id != id) continue;
                if (enemy.state == enemies::kChargerRecover) {
                    crashed = true;
                    CHECK(enemy.timer > balance::kChargerCrashSeconds - 0.1f);
                    CHECK(enemy.knock.y < 0.0f);
                    CHECK(enemy.pos.y < car.center.y);
                }
            }
        }
        CHECK(crashed);
    }
}

TEST_CASE("zombies go around cars and reach a player standing still") {
    // Spitters keep their distance on purpose, so they aren't in this list.
    constexpr EnemyType kWalkers[] = {EnemyType::Walker, EnemyType::Runner, EnemyType::Brute, EnemyType::Bloater, EnemyType::Crawler, EnemyType::Charger};
    constexpr int kEachType = 3;
    int zombies = 0;
    int arrived = 0;
    float slowest = 0.0f;
    for (const MapId map : kBothMaps) {
        for (uint64_t seed = 1; seed <= 50; ++seed) {
            Obstacle car;
            auto state = runBeside(map, seed, &car);
            // Hard up against the car, so most of the crowd has it in the way.
            standClear(*state, car.center + Vec2{0.0f, car.half.y + balance::kPlayerRadius + 1.0f}, Vec2{0.0f, 1.0f});
            std::vector<uint32_t> ids;
            int placed = 0;
            for (const EnemyType type : kWalkers) {
                for (int i = 0; i < kEachType; ++i) {
                    const float angle = kTau * static_cast<float>(placed++) / static_cast<float>(kEachType * 6) + static_cast<float>(seed);
                    if (Enemy* enemy = enemies::spawn(*state, type, state->firstPlayer().pos + fromAngle(angle) * 400.0f, false)) ids.push_back(enemy->id);
                }
            }
            int waiting = static_cast<int>(ids.size());
            for (int step = 0; step < 60 * 60 && waiting > 0; ++step) {
                keepAlive(*state);
                state->firstPlayer().invulnerable = 1.0f;
                sim::step(*state, InputFrame{});
                // Whoever arrives leaves, so a crowd around the player can't block the rest. So do
                // zombies from the wave table, which aren't part of this test.
                for (int i = state->enemies.count - 1; i >= 0; --i) {
                    const Enemy& enemy = state->enemies[i];
                    const auto found = std::find(ids.begin(), ids.end(), enemy.id);
                    if (found == ids.end()) {
                        state->enemies.removeAt(i);
                    } else if (distance(enemy.pos, state->firstPlayer().pos) <= enemy.radius + balance::kPlayerRadius + 1.0f) {
                        state->enemies.removeAt(i);
                        --waiting;
                        slowest = std::max(slowest, static_cast<float>(step) * kDt);
                    }
                }
            }
            zombies += static_cast<int>(ids.size());
            arrived += static_cast<int>(ids.size()) - waiting;
            CHECK_MESSAGE(waiting == 0, "map ", static_cast<int>(map), " seed ", seed, ": ", waiting, " zombies never arrived");
        }
    }
    MESSAGE(arrived, " of ", zombies, " zombies arrived, the slowest after ", slowest, " s");
}

TEST_CASE("a zombie caught among cars for far too long is moved past the car") {
    Obstacle car;
    auto state = runBeside(MapId::City, 5, &car);
    // The player is on the far side, so the zombie is going around, and it has been at it
    // for as long as the limit allows.
    standClear(*state, car.center + Vec2{0.0f, car.half.y + balance::kPlayerRadius + 60.0f}, Vec2{0.0f, 1.0f});
    Enemy* enemy = enemies::spawn(*state, EnemyType::Walker, car.center - Vec2{0.0f, car.half.y + 12.0f}, false);
    REQUIRE(enemy);
    const uint32_t id = enemy->id;
    enemy->stuck = balance::kZombieStuckSteps;
    keepAlive(*state);
    sim::step(*state, InputFrame{});
    bool moved = false;
    for (const SimEvent& event : state->events) moved = moved || event.type == EventType::ZombieUnstuck;
    CHECK(moved);
    for (const Enemy& zombie : state->enemies) {
        if (zombie.id != id) continue;
        CHECK(zombie.stuck == 0);
        // The corner on the player's side, which is past the car's middle.
        CHECK(zombie.pos.y > car.center.y);
        CHECK_FALSE(obstacles::overlapsCar(*state, zombie.pos, zombie.radius * balance::kZombieCarRadiusShare - 0.5f));
    }
}

TEST_CASE("same seed, inputs and map give the same run over cars, and another map differs") {
    const auto play = [](MapId map) {
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 4242, Difficulty::Normal, map);
        for (int step = 0; step < 5000; ++step) {
            InputFrame input;
            const float angle = static_cast<float>(step) * 0.0021f;
            input.moveX = std::cos(angle);
            input.moveY = std::sin(angle);
            input.dash = step % 300 == 0;
            if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
            state->firstPlayer().hp = state->firstPlayer().maxHp;
            sim::step(*state, input);
            state->events.clear();
        }
        return sim::stateHash(*state);
    };
    CHECK(play(MapId::City) == play(MapId::City));
    CHECK(play(MapId::Wasteland) == play(MapId::Wasteland));
    CHECK(play(MapId::City) != play(MapId::Wasteland));
}

TEST_CASE("a Charger with a car in the way walks around it instead of charging into it") {
    Obstacle car;
    auto state = runBeside(MapId::City, 9, &car);
    standClear(*state, car.center + Vec2{0.0f, car.half.y + balance::kPlayerRadius + 2.0f}, Vec2{0.0f, 1.0f});
    Enemy* charger = enemies::spawn(*state, EnemyType::Charger, car.center - Vec2{0.0f, car.half.y + 40.0f}, false);
    REQUIRE(charger);
    const uint32_t id = charger->id;
    keepAlive(*state);
    sim::step(*state, InputFrame{});
    for (const Enemy& enemy : state->enemies) {
        if (enemy.id == id) CHECK(enemy.state == enemies::kChargerChase);
    }
}

TEST_CASE("the browser test's long walk on seed 4 meets no car") {
    // tests/e2e/world.test.mjs holds one key at a time along these three lines, and it can only
    // get where it is going if nothing stands in the way.
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 4);
    const Vec2 corners[] = {{0.0f, 0.0f}, {800.0f, 0.0f}, {800.0f, 800.0f}, {-200.0f, 800.0f}};
    for (int leg = 0; leg < 3; ++leg) {
        for (int i = 0; i <= 200; ++i) {
            state->firstPlayer().pos = lerp(corners[leg], corners[leg + 1], static_cast<float>(i) / 200.0f);
            obstacles::update(*state);
            CHECK_FALSE(obstacles::overlapsCar(*state, state->firstPlayer().pos, 20.0f));
        }
    }
}

// --- Wreck piles -----------------------------------------------------------------

namespace {

constexpr EnemyType kAllWalkers[] = {EnemyType::Walker, EnemyType::Runner, EnemyType::Brute, EnemyType::Bloater, EnemyType::Crawler, EnemyType::Charger};

// A run with nothing in it yet, and the player at this spot.
std::unique_ptr<GameState> runAt(MapId map, uint64_t seed, Vec2 spot) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, map);
    state->firstPlayer().weaponCount = 0;
    state->firstPlayer().xpToNext = 1 << 30;
    state->firstPlayer().pos = spot;
    state->firstPlayer().prevPos = spot;
    obstacles::update(*state);
    return state;
}

// Spots where the player fits, right beside two wrecks standing close together. That is where
// a zombie is most likely to get pinned. maxGap is how far apart the wrecks may be.
std::vector<Vec2> spotsAmongWrecks(MapId map, uint64_t seed, float maxGap, int wanted) {
    std::vector<Vec2> spots;
    auto state = runAt(map, seed, Vec2{});
    for (int y = -30; y <= 30; y += 3) {
        for (int x = -30; x <= 30; x += 3) {
            const std::vector<Obstacle> cars = listOf(map, seed, x, y);
            for (size_t a = 0; a < cars.size(); ++a) {
                for (size_t b = a + 1; b < cars.size(); ++b) {
                    const float gapX = std::abs(cars[a].center.x - cars[b].center.x) - cars[a].half.x - cars[b].half.x;
                    const float gapY = std::abs(cars[a].center.y - cars[b].center.y) - cars[a].half.y - cars[b].half.y;
                    if (std::max(gapX, gapY) > maxGap) continue;
                    const Vec2 middle = (cars[a].center + cars[b].center) * 0.5f;
                    state->firstPlayer().pos = middle;
                    obstacles::update(*state);
                    for (float dy = -60.0f; dy <= 60.0f; dy += 30.0f) {
                        for (float dx = -60.0f; dx <= 60.0f; dx += 30.0f) {
                            const Vec2 spot = middle + Vec2{dx, dy};
                            if (maps::tileOf(spot.x) != x || maps::tileOf(spot.y) != y) continue;
                            if (obstacles::overlapsCar(*state, spot, balance::kPlayerRadius)) continue;
                            if (obstacles::distanceToNearestCar(*state, spot, 40.0f) >= 30.0f) continue;
                            spots.push_back(spot);
                            if (static_cast<int>(spots.size()) >= wanted) return spots;
                        }
                    }
                }
            }
        }
    }
    return spots;
}

// False when the player stands in a slot so tight that this zombie can't get close enough to
// touch him from anywhere. Then not arriving is no fault of the walking.
bool hasRoomToTouch(const GameState& state, Vec2 playerPos, EnemyType type) {
    const float size = balance::kEnemyStats[static_cast<int>(type)].radius;
    const float reach = size + balance::kPlayerRadius;
    for (float dy = -reach; dy <= reach; dy += 1.0f) {
        for (float dx = -reach; dx <= reach; dx += 1.0f) {
            if (dx * dx + dy * dy > reach * reach) continue;
            if (!obstacles::overlapsCar(state, playerPos + Vec2{dx, dy}, size * balance::kZombieCarRadiusShare - 0.25f)) return true;
        }
    }
    return false;
}

struct ArrivalResult {
    int zombies = 0;
    int stillAway = 0;  // Never arrived, though they had room to.
    int noRoom = 0;     // Never arrived, and had no room to.
    int rescues = 0;
};

// The player stands still with 24 zombies of 6 types placed around him. Whoever arrives leaves,
// so a crowd around the player can't block the rest. So do zombies from the wave table.
ArrivalResult zombiesArriving(MapId map, uint64_t seed, Vec2 spot) {
    auto state = runAt(map, seed, spot);
    struct Waiting {
        uint32_t id;
        EnemyType type;
    };
    std::vector<Waiting> waiting;
    int placed = 0;
    for (const EnemyType type : kAllWalkers) {
        for (int i = 0; i < 4; ++i) {
            const float angle = kTau * static_cast<float>(placed++) / 24.0f;
            if (Enemy* enemy = enemies::spawn(*state, type, spot + fromAngle(angle) * 420.0f, false)) waiting.push_back({enemy->id, type});
        }
    }
    ArrivalResult result;
    result.zombies = static_cast<int>(waiting.size());
    for (int step = 0; step < 120 * 60 && !waiting.empty(); ++step) {
        keepAlive(*state);
        state->firstPlayer().invulnerable = 1.0f;
        sim::step(*state, InputFrame{});
        for (const SimEvent& event : state->events) {
            if (event.type == EventType::ZombieUnstuck) {
                ++result.rescues;
                CHECK_FALSE(obstacles::overlapsCar(*state, event.pos, 5.0f));
            }
        }
        for (int i = state->enemies.count - 1; i >= 0; --i) {
            const Enemy& enemy = state->enemies[i];
            const auto found = std::find_if(waiting.begin(), waiting.end(), [&](const Waiting& w) { return w.id == enemy.id; });
            if (found == waiting.end()) {
                state->enemies.removeAt(i);
            } else if (distance(enemy.pos, spot) <= enemy.radius + balance::kPlayerRadius + 1.0f) {
                waiting.erase(found);
                state->enemies.removeAt(i);
            }
        }
    }
    for (const Waiting& w : waiting) {
        if (hasRoomToTouch(*state, spot, w.type)) ++result.stillAway;
        else ++result.noRoom;
    }
    return result;
}

// True when the player can walk from here to open ground, checked on a fine grid. This is the
// slow and exact answer, which the game's own quick test is held to.
bool playerWalksOutFrom(const GameState& state, Vec2 from) {
    constexpr float kCell = 4.0f;
    constexpr int kSpan = 62;  // About 250 units each way, past any pile.
    std::vector<char> seen((2 * kSpan + 1) * (2 * kSpan + 1), 0);
    std::vector<std::pair<int, int>> open{{0, 0}};
    while (!open.empty()) {
        const auto [x, y] = open.back();
        open.pop_back();
        if (x == -kSpan || x == kSpan || y == -kSpan || y == kSpan) return true;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                char& mark = seen[(x + dx + kSpan) + (y + dy + kSpan) * (2 * kSpan + 1)];
                if (mark) continue;
                mark = 1;
                const Vec2 at = from + Vec2{static_cast<float>(x + dx) * kCell, static_cast<float>(y + dy) * kCell};
                if (!obstacles::overlapsCar(state, at, balance::kPlayerRadius)) open.push_back({x + dx, y + dy});
            }
        }
    }
    return false;
}

// True when the player can walk to somewhere within collect range of an item here.
bool collectable(const GameState& state, Vec2 item) {
    const float reach = balance::kPickupCollectRadius;
    for (float dy = -reach; dy <= reach; dy += 4.0f) {
        for (float dx = -reach; dx <= reach; dx += 4.0f) {
            if (dx * dx + dy * dy > reach * reach) continue;
            const Vec2 at = item + Vec2{dx, dy};
            if (!obstacles::overlapsCar(state, at, balance::kPlayerRadius) && playerWalksOutFrom(state, at)) return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("zombies pinned in a wreck pile still arrive, as long as there is room to touch the player") {
    ArrivalResult total;
    const auto add = [&](MapId map, uint64_t seed, Vec2 spot) {
        const ArrivalResult one = zombiesArriving(map, seed, spot);
        CHECK_MESSAGE(one.stillAway == 0, "map ", static_cast<int>(map), " seed ", seed, " at ", spot.x, ", ", spot.y, ": ", one.stillAway, " zombies never arrived");
        total.zombies += one.zombies;
        total.stillAway += one.stillAway;
        total.noRoom += one.noRoom;
        total.rescues += one.rescues;
    };
    // A Runner used to stand at (7131.7, -17419.3) here forever without counting as stuck.
    add(MapId::Wasteland, 1, Vec2{7140.7f, -17485.4f});
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        for (const Vec2 spot : spotsAmongWrecks(MapId::Wasteland, seed, 30.0f, 5)) add(MapId::Wasteland, seed, spot);
    }
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        for (const Vec2 spot : spotsAmongWrecks(MapId::City, seed, 200.0f, 2)) add(MapId::City, seed, spot);
    }
    MESSAGE(total.zombies, " zombies, ", total.noRoom, " with no room to touch the player, ", total.rescues, " rescues");
    CHECK(total.rescues > 0);
}

TEST_CASE("a rescue lands clear of every car, and closer to the player") {
    int found = 0;
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        for (const Vec2 target : spotsAmongWrecks(MapId::Wasteland, seed, 30.0f, 8)) {
            auto state = runAt(MapId::Wasteland, seed, target);
            for (int i = 0; i < 12; ++i) {
                for (const EnemyType type : kAllWalkers) {
                    const float size = balance::kEnemyStats[static_cast<int>(type)].radius;
                    const float radius = size * balance::kZombieCarRadiusShare;
                    Vec2 from = target + fromAngle(kTau * static_cast<float>(i) / 12.0f) * 120.0f;
                    obstacles::resolveCircle(*state, from, radius);
                    Vec2 spot;
                    if (!obstacles::rescueSpot(*state, from, radius, target, size + balance::kPlayerRadius, &spot)) continue;
                    ++found;
                    CHECK_FALSE(obstacles::overlapsCar(*state, spot, radius - 0.5f));
                    CHECK(distance(spot, target) < distance(from, target));
                }
            }
        }
    }
    CHECK(found > 0);
}

TEST_CASE("nobody is rescued for keeping its distance, or for chasing a player who runs") {
    SUBCASE("a Spitter circling with a car between it and the player") {
        Obstacle car;
        auto state = runBeside(MapId::City, 5, &car);
        const Vec2 side{0.0f, 1.0f};
        standClear(*state, car.center + side * (car.half.y + balance::kPlayerRadius + 1.0f), side);
        Enemy* spitter = enemies::spawn(*state, EnemyType::Spitter, state->firstPlayer().pos - side * balance::kSpitterKeepDistance, false);
        REQUIRE(spitter);
        const uint32_t id = spitter->id;
        // Held in place, so it never gets closer and the car stays in the way. All that keeps
        // it from counting as stuck is that it isn't trying to come closer.
        const Vec2 heldAt = spitter->pos;
        REQUIRE(obstacles::lineBlocked(*state, heldAt, state->firstPlayer().pos, spitter->radius * balance::kZombieCarRadiusShare));
        for (int step = 0; step < 60 * 30; ++step) {
            for (Enemy& enemy : state->enemies) {
                if (enemy.id == id) enemy.pos = heldAt;
            }
            keepAlive(*state);
            state->firstPlayer().invulnerable = 1.0f;
            sim::step(*state, InputFrame{});
            for (const SimEvent& event : state->events) CHECK(event.type != EventType::ZombieUnstuck);
            for (int i = state->enemies.count - 1; i >= 0; --i) {
                if (state->enemies[i].id != id) state->enemies.removeAt(i);
            }
            state->enemyBullets.clear();
        }
    }
    SUBCASE("a pack running after the player through the cars") {
        for (const MapId map : kBothMaps) {
            auto state = runAt(map, 21, Vec2{});
            for (int i = 0; i < 40; ++i) {
                enemies::spawn(*state, kAllWalkers[i % 6], fromAngle(kTau * static_cast<float>(i) / 40.0f) * 150.0f, false);
            }
            std::vector<uint32_t> ids;
            for (const Enemy& enemy : state->enemies) ids.push_back(enemy.id);
            int rescues = 0;
            for (int step = 0; step < 60 * 40; ++step) {
                keepAlive(*state);
                state->firstPlayer().invulnerable = 1.0f;
                InputFrame input;
                input.moveX = std::cos(static_cast<float>(step) * 0.002f);
                input.moveY = std::sin(static_cast<float>(step) * 0.002f);
                sim::step(*state, input);
                for (const SimEvent& event : state->events) rescues += event.type == EventType::ZombieUnstuck ? 1 : 0;
                for (int i = state->enemies.count - 1; i >= 0; --i) {
                    if (std::find(ids.begin(), ids.end(), state->enemies[i].id) == ids.end()) state->enemies.removeAt(i);
                }
            }
            // Out of 40 zombies over 40 seconds. Now and then a car really does hold one up.
            MESSAGE("map ", static_cast<int>(map), ": ", rescues, " rescues of a pack chasing the player");
            CHECK(rescues <= 4);
        }
    }
}

TEST_CASE("a zombie pinned behind a car waits for a queue, but not forever") {
    Obstacle car;
    auto state = runBeside(MapId::City, 5, &car);
    // The player is past the car's left corner, so the way there leads left along the car.
    standClear(*state, car.center + Vec2{-car.half.x - 10.0f, car.half.y + balance::kPlayerRadius + 60.0f}, Vec2{0.0f, 1.0f});
    Enemy* first = enemies::spawn(*state, EnemyType::Walker, car.center - Vec2{0.0f, car.half.y + 12.0f}, false);
    REQUIRE(first);
    const uint32_t id = first->id;
    const Vec2 pinnedAt = first->pos;
    Enemy* second = enemies::spawn(*state, EnemyType::Walker, pinnedAt - Vec2{first->radius * 2.0f - 1.0f, 0.0f}, false);
    REQUIRE(second);
    const uint32_t otherId = second->id;
    const Vec2 otherAt = second->pos;
    // Both are held in place, one in the way of the other, so nobody can get anywhere.
    bool queued = false;
    int rescuedAt = -1;
    for (int step = 0; step < 60 * 60 && rescuedAt < 0; ++step) {
        for (Enemy& enemy : state->enemies) {
            if (enemy.id == id) enemy.pos = pinnedAt;
            if (enemy.id == otherId) {
                enemy.pos = otherAt;
                enemy.stuck = 0;  // It has nobody to wait for, and this test is about the other one.
            }
        }
        keepAlive(*state);
        sim::step(*state, InputFrame{});
        for (const SimEvent& event : state->events) {
            if (event.type == EventType::ZombieUnstuck && rescuedAt < 0) rescuedAt = step;
        }
        for (int i = state->enemies.count - 1; i >= 0; --i) {
            const Enemy& enemy = state->enemies[i];
            if (enemy.id == id) queued = queued || enemy.stuckQueued > 0;
            else if (enemy.id != otherId) state->enemies.removeAt(i);
        }
    }
    CHECK(queued);
    // Waiting doesn't count, up to the limit. After that the rescue comes as usual.
    const int waited = balance::kZombieQueueChecks * balance::kZombieStuckCheckSteps;
    CHECK(rescuedAt >= waited);
    CHECK(rescuedAt <= waited + balance::kZombieStuckSteps + balance::kZombieStuckCheckSteps);
}

TEST_CASE("anything dropped on a car lands where the player can collect it") {
    struct Target {
        MapId map;
        int drops;
    };
    for (const Target target : {Target{MapId::Wasteland, 3000}, Target{MapId::City, 600}}) {
        int drops = 0;
        for (uint64_t seed = 1; seed < 200 && drops < target.drops; ++seed) {
            auto state = runAt(target.map, seed, Vec2{});
            for (int y = -30; y <= 30 && drops < target.drops; y += 3) {
                for (int x = -30; x <= 30 && drops < target.drops; x += 3) {
                    const std::vector<Obstacle> cars = listOf(target.map, seed, x, y);
                    if (cars.empty()) continue;
                    state->firstPlayer().pos = Vec2{(static_cast<float>(x) + 0.5f) * maps::kChunkWorldSize, (static_cast<float>(y) + 0.5f) * maps::kChunkWorldSize};
                    obstacles::update(*state);
                    for (const Obstacle& car : cars) {
                        state->pickups.clear();
                        state->gems.clear();
                        pickups::spawnPickup(*state, car.center, PickupType::Heal);
                        pickups::dropGem(*state, car.center, 1);
                        REQUIRE(state->pickups.count == 1);
                        REQUIRE(state->gems.count == 1);
                        for (const Vec2 item : {state->pickups[0].pos, state->gems[0].pos}) {
                            ++drops;
                            CHECK_FALSE(obstacles::overlapsCar(*state, item, 1.0f));
                            CHECK_MESSAGE(collectable(*state, item), "map ", static_cast<int>(target.map), " seed ", seed, " dropped at ", car.center.x, ", ", car.center.y);
                        }
                    }
                }
            }
        }
        CHECK(drops >= target.drops);
    }
}

TEST_CASE("mines laid in a wreck pile land clear of every car") {
    int mines = 0;
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        for (const Vec2 spot : spotsAmongWrecks(MapId::Wasteland, seed, 30.0f, 4)) {
            auto state = runAt(MapId::Wasteland, seed, spot);
            weapons::grant(*state, 0, WeaponId::Landmines);
            enemies::spawn(*state, EnemyType::Walker, spot + Vec2{200.0f, 0.0f}, false);
            for (int step = 0; step < 150; ++step) {
                keepAlive(*state);
                state->firstPlayer().invulnerable = 1.0f;
                sim::step(*state, InputFrame{});
                for (const Projectile& shot : state->projectiles) {
                    if (shot.kind != ProjectileKind::Mine) continue;
                    ++mines;
                    CHECK_FALSE(obstacles::overlapsCar(*state, shot.pos, 1.0f));
                }
            }
        }
    }
    CHECK(mines > 0);
}

// --- Dash afterimage -------------------------------------------------------------

namespace {

constexpr float kGhostSpacing = 28.0f;

// Plays a whole dash and records it the same way the drawing code does.
worldview::DashPath recordDash(GameState& state, Vec2 direction) {
    worldview::DashPath path;
    for (int step = 0; step < 30; ++step) {
        keepAlive(state);
        InputFrame input;
        input.moveX = direction.x;
        input.moveY = direction.y;
        input.dash = step == 0;
        sim::step(state, input);
        if (state.firstPlayer().dashLeft <= 0.0f) break;
        if (path.empty()) path.add(state.firstPlayer().prevPos);
        path.add(state.firstPlayer().pos);
    }
    return path;
}

}  // namespace

TEST_CASE("the dash afterimage follows where the soldier really went") {
    SUBCASE("in the open the copies trail straight behind him") {
        auto state = runAt(MapId::City, 1, Vec2{});
        const Vec2 start = state->firstPlayer().pos;
        const Vec2 direction{1.0f, 0.0f};
        const worldview::DashPath path = recordDash(*state, direction);
        const Vec2 at = state->firstPlayer().pos;
        Vec2 ghost;
        REQUIRE(path.pointBack(at, kGhostSpacing, &ghost));
        CHECK(std::abs(ghost.x - (at.x - kGhostSpacing)) < 8.0f);
        CHECK(ghost.y == doctest::Approx(start.y));
        // No further back than where the dash began.
        CHECK_FALSE(path.pointBack(at, distance(at, start) + 12.0f, &ghost));
    }
    SUBCASE("a dash into a car goes nowhere, so there are no copies") {
        for (const MapId map : kBothMaps) {
            Obstacle car;
            auto state = runBeside(map, 3, &car);
            const Vec2 side{0.0f, -1.0f};
            standClear(*state, car.center + side * (car.half.y + balance::kPlayerRadius + 1.0f), side);
            const Vec2 before = state->firstPlayer().pos;
            const worldview::DashPath path = recordDash(*state, side * -1.0f);
            // Only check when nothing else let him slip past the car.
            REQUIRE(distance(state->firstPlayer().pos, before) < 2.0f);
            Vec2 ghost;
            CHECK_FALSE(path.pointBack(state->firstPlayer().pos, kGhostSpacing, &ghost));
        }
    }
    SUBCASE("sliding along a car, the copies stay on his path and out of the car") {
        Obstacle car;
        auto state = runBeside(MapId::City, 3, &car);
        const Vec2 side{0.0f, -1.0f};
        standClear(*state, car.center + side * (car.half.y + balance::kPlayerRadius + 1.0f) + Vec2{-car.half.x * 0.5f, 0.0f}, side);
        const worldview::DashPath path = recordDash(*state, normalizedOr(Vec2{1.0f, 1.0f}, Vec2{}));
        int ghosts = 0;
        for (int i = 1; i <= 4; ++i) {
            Vec2 ghost;
            if (!path.pointBack(state->firstPlayer().pos, static_cast<float>(i) * kGhostSpacing, &ghost)) continue;
            ++ghosts;
            CHECK_FALSE(obstacles::overlapsCar(*state, ghost, balance::kPlayerRadius - 1.0f));
        }
        CHECK(ghosts > 0);
    }
}
