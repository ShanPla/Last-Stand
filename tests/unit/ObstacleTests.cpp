#include "doctest.h"

#include "components/Enemies/Bloater/Bloater.h"
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
    state->player.weaponCount = 0;
    state->player.xpToNext = 1 << 30;
    for (int ring = 1; ring < 40; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                const std::vector<Obstacle> list = listOf(map, seed, x, y);
                if (list.empty()) continue;
                *car = list[0];
                // Beside the car and clear of it, so the first step starts from a legal place.
                state->player.pos = car->center - Vec2{car->half.x + balance::kPlayerRadius + 40.0f, 0.0f};
                state->player.prevPos = state->player.pos;
                obstacles::update(*state);
                obstacles::resolveCircle(*state, state->player.pos, balance::kPlayerRadius);
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
    state.player.pos = spot;
    obstacles::update(state);
    while (obstacles::overlapsCar(state, state.player.pos, balance::kPlayerRadius)) state.player.pos += away * 4.0f;
    state.player.prevPos = state.player.pos;
}

// Holds the world still apart from what a test does: no spawns and no damage.
void keepAlive(GameState& state) {
    state.player.hp = state.player.maxHp;
    state.run.pendingLevels = 0;
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
        state->player.weaponCount = 0;
        for (int step = 0; step < 600; ++step) {
            // Long jumps, so tiles leave and re-enter the window in every direction.
            state->player.pos = Vec2{std::sin(static_cast<float>(step) * 0.3f) * 5000.0f, std::cos(static_cast<float>(step) * 0.17f) * 4000.0f};
            obstacles::update(*state);
            const ObstacleCache& cache = state->obstacleCache;
            for (const ObstacleTile& tile : cache.tiles) {
                REQUIRE(tile.valid);
                CHECK(std::abs(tile.tileX - cache.centerX) <= kObstacleWindow / 2);
                CHECK(std::abs(tile.tileY - cache.centerY) <= kObstacleWindow / 2);
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
                        const Vec2 toCar = normalizedOr(start - state->player.pos, Vec2{1.0f, 0.0f});
                        InputFrame input;
                        input.moveX = toCar.x * strength;
                        input.moveY = toCar.y * strength;
                        sim::step(*state, input);
                        CHECK_FALSE(obstacles::overlapsCar(*state, state->player.pos, balance::kPlayerRadius - 0.5f));
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
    const float startX = state->player.pos.x;
    for (int step = 0; step < 20; ++step) {
        keepAlive(*state);
        state->enemies.clear();
        InputFrame input;
        input.moveX = 0.7f;
        input.moveY = 0.7f;
        sim::step(*state, input);
    }
    const float expected = 0.7f * balance::kPlayerSpeed * kDt * 20.0f;
    CHECK(state->player.pos.x - startX > expected * 0.9f);
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
                CHECK_FALSE(obstacles::overlapsCar(*state, state->player.pos, balance::kPlayerRadius - 0.5f));
            }
            CHECK_MESSAGE(state->player.pos.y < car.center.y, "the dash must not have crossed the car");
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
        state->player.hp = state->player.maxHp * 0.5f;
        for (int step = 0; step < 240 && state->pickups.count > 0; ++step) {
            state->enemies.clear();
            state->player.hp = std::min(state->player.hp, state->player.maxHp * 0.5f);
            const Vec2 straight = normalizedOr(dropped - state->player.pos, Vec2{1.0f, 0.0f});
            const Vec2 toward = obstacles::steerAround(*state, state->player.pos, balance::kPlayerRadius, straight, dropped, 1u);
            InputFrame input;
            input.moveX = toward.x;
            input.moveY = toward.y;
            sim::step(*state, input);
            state->events.clear();
            state->run.pendingLevels = 0;
        }
        CHECK(state->pickups.count == 0);
    }
}

TEST_CASE("a mine is never laid inside a car") {
    for (const MapId map : kBothMaps) {
        Obstacle car;
        auto state = runBeside(map, 3, &car);
        standClear(*state, car.center - Vec2{0.0f, car.half.y + balance::kPlayerRadius + 1.0f}, Vec2{0.0f, -1.0f});
        state->player.facing = Vec2{0.0f, -1.0f};  // So behind him is the car.
        weapons::grant(*state, WeaponId::Landmines);
        enemies::spawn(*state, EnemyType::Walker, state->player.pos - Vec2{0.0f, 200.0f}, false);
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
            state->player.xpToNext = 1 << 30;
            state->run.tick = 60 * 240;  // A thick part of the wave table.
            for (int step = 0; step < 3000; ++step) {
                state->player.hp = state->player.maxHp;
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
    state->player.pos = car.center + Vec2{car.half.x + 200.0f, 0.0f};
    state->player.prevPos = state->player.pos;
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
        state->player.pos = car.center + Vec2{0.0f, car.half.y + balance::kPlayerRadius + 2.0f};
        state->player.prevPos = state->player.pos;
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
                    if (Enemy* enemy = enemies::spawn(*state, type, state->player.pos + fromAngle(angle) * 400.0f, false)) ids.push_back(enemy->id);
                }
            }
            int waiting = static_cast<int>(ids.size());
            for (int step = 0; step < 60 * 60 && waiting > 0; ++step) {
                keepAlive(*state);
                state->player.invulnerable = 1.0f;
                sim::step(*state, InputFrame{});
                // Whoever arrives leaves, so a crowd around the player can't block the rest. So do
                // zombies from the wave table, which aren't part of this test.
                for (int i = state->enemies.count - 1; i >= 0; --i) {
                    const Enemy& enemy = state->enemies[i];
                    const auto found = std::find(ids.begin(), ids.end(), enemy.id);
                    if (found == ids.end()) {
                        state->enemies.removeAt(i);
                    } else if (distance(enemy.pos, state->player.pos) <= enemy.radius + balance::kPlayerRadius + 1.0f) {
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
            state->player.hp = state->player.maxHp;
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
            state->player.pos = lerp(corners[leg], corners[leg + 1], static_cast<float>(i) / 200.0f);
            obstacles::update(*state);
            CHECK_FALSE(obstacles::overlapsCar(*state, state->player.pos, 20.0f));
        }
    }
}
