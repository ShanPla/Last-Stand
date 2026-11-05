#include "doctest.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Enemies.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace ls;
using balance::BossAttack;

namespace {

constexpr Difficulty kLevels[] = {Difficulty::Easy, Difficulty::Normal, Difficulty::Hard, Difficulty::Hell};
constexpr float kHpForPhase[3] = {1.0f, 0.5f, 0.2f};

// A run with no weapons, so nothing but the boss's own attacks changes the picture.
std::unique_ptr<GameState> bossRun(Difficulty difficulty, Vec2 bossPos, int phase, uint64_t seed = 21) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, difficulty);
    state->player.weaponCount = 0;
    state->player.pos = Vec2{};
    state->player.prevPos = Vec2{};
    state->player.invulnerable = 0.0f;
    Enemy* boss = enemies::spawn(*state, EnemyType::Boss, bossPos, false);
    REQUIRE(boss != nullptr);
    state->run.bossSpawned = true;
    boss->hp = boss->maxHp * kHpForPhase[phase];
    return state;
}

// The parts of sim::step that run the fight. The player is moved by the test itself.
void stepFight(GameState& state) {
    state.run.tick++;
    obstacles::update(state);
    enemies::update(state);
    weapons::update(state);
    enemies::updateBullets(state);
    state.events.clear();
}

Enemy& bossOf(GameState& state) {
    REQUIRE(state.enemies.count > 0);
    REQUIRE(state.enemies[0].type == EnemyType::Boss);
    return state.enemies[0];
}

bool warningShows(GameState& state) {
    for (const Zone& zone : state.zones) {
        if (zone.hostile) return true;
    }
    return enemies::bossCharging(bossOf(state));
}

int stepsFor(float seconds) { return static_cast<int>(std::floor(seconds / kDt + 0.001f)); }

// Moves the player as a person at plain walking speed would, ignoring cars.
void walkPlayer(GameState& state, Vec2 dir) {
    state.player.pos += normalizedOr(dir, Vec2{}) * balance::kPlayerSpeed * kDt;
    state.player.prevPos = state.player.pos;
}

// Starts one attack, then plays it out with the player either standing still or moving as
// `move` says. Returns the health the player lost.
template <typename Move>
float playAttack(GameState& state, BossAttack attack, float seconds, Move move) {
    const float startHp = state.player.hp;
    enemies::update(state);  // Sets the phase from the boss's health before the attack starts.
    state.zones.clear();
    state.enemyBullets.clear();
    enemies::startBossAttack(state, bossOf(state), attack);
    for (int i = 0; i < stepsFor(seconds); ++i) {
        move(state, i);
        stepFight(state);
    }
    return startHp - state.player.hp;
}

void standStill(GameState&, int) {}

Vec2 perpendicularOf(Vec2 v) { return Vec2{-v.y, v.x}; }

}  // namespace

TEST_CASE("each difficulty has its own move set, Normal is unchanged, and harder sets add attacks") {
    const balance::BossMoves& easy = balance::bossMoves(Difficulty::Easy);
    const balance::BossMoves& normal = balance::bossMoves(Difficulty::Normal);
    const balance::BossMoves& hard = balance::bossMoves(Difficulty::Hard);
    const balance::BossMoves& hell = balance::bossMoves(Difficulty::Hell);

    // Warnings get shorter level by level, but never below the readable limit.
    CHECK(easy.telegraphSeconds > normal.telegraphSeconds);
    CHECK(normal.telegraphSeconds > hard.telegraphSeconds);
    CHECK(hard.telegraphSeconds > hell.telegraphSeconds);
    CHECK(hell.telegraphSeconds >= balance::kBossShortestTelegraph);

    // Normal is the balanced default, so it keeps the numbers it had before move sets existed.
    CHECK(normal.telegraphSeconds == doctest::Approx(1.0f));
    CHECK(normal.circleRadius == doctest::Approx(165.0f));
    CHECK(normal.bulletSpeed == doctest::Approx(210.0f));
    for (int phase = 0; phase < 3; ++phase) {
        static const float kIntervals[3] = {2.8f, 2.2f, 1.6f};
        static const int kCircles[3] = {2, 4, 6};
        static const int kRing[3] = {16, 24, 32};
        static const int kLines[3] = {1, 1, 3};
        CHECK(normal.attackInterval[phase] == doctest::Approx(kIntervals[phase]));
        CHECK(normal.circleCount[phase] == kCircles[phase]);
        CHECK(normal.ringBullets[phase] == kRing[phase]);
        CHECK(normal.lineCount[phase] == kLines[phase]);
        CHECK(normal.dashCount[phase] == 1);
        CHECK(normal.cycles[phase].count == (phase == 0 ? 3 : 4));
    }

    // Easy is gentler in every count, and its bullets are slower.
    CHECK(easy.bulletSpeed < normal.bulletSpeed);
    for (int phase = 0; phase < 3; ++phase) {
        CHECK(easy.circleCount[phase] < normal.circleCount[phase]);
        CHECK(easy.ringBullets[phase] < normal.ringBullets[phase]);
        CHECK(easy.attackInterval[phase] > normal.attackInterval[phase]);
    }

    // Hard keeps every Normal attack and adds one; Hell keeps every Hard attack and adds more.
    const auto usedAnywhere = [](const balance::BossMoves& moves, BossAttack attack) {
        for (int phase = 0; phase < 3; ++phase) {
            if (balance::bossCycleUses(moves, phase, attack)) return true;
        }
        return false;
    };
    int hardExtra = 0;
    int hellExtra = 0;
    for (int a = 0; a < static_cast<int>(BossAttack::Count); ++a) {
        const auto attack = static_cast<BossAttack>(a);
        CAPTURE(a);
        if (usedAnywhere(normal, attack)) CHECK(usedAnywhere(hard, attack));
        if (usedAnywhere(hard, attack)) CHECK(usedAnywhere(hell, attack));
        if (usedAnywhere(hard, attack) && !usedAnywhere(normal, attack)) hardExtra++;
        if (usedAnywhere(hell, attack) && !usedAnywhere(hard, attack)) hellExtra++;
        // Easy only uses what Normal uses.
        if (usedAnywhere(easy, attack)) CHECK(usedAnywhere(normal, attack));
    }
    CHECK(hardExtra >= 1);
    CHECK(hellExtra >= 1);
}

TEST_CASE("the boss follows its own difficulty's cycle, with that difficulty's numbers") {
    for (const Difficulty level : kLevels) {
        const balance::BossMoves& moves = balance::bossMoves(level);
        for (int phase = 0; phase < 3; ++phase) {
            CAPTURE(static_cast<int>(level));
            CAPTURE(phase);
            auto state = bossRun(level, Vec2{-700.0f, 0.0f}, phase);
            const balance::BossCycle& cycle = moves.cycles[phase];
            std::vector<BossAttack> started;
            const int wanted = cycle.count * 2;
            for (int i = 0; i < stepsFor(120.0f) && static_cast<int>(started.size()) < wanted; ++i) {
                Enemy& boss = bossOf(*state);
                const bool walking = boss.state == enemies::kBossWalk;
                const uint8_t stepBefore = state->run.bossStep;
                const BossAttack next = enemies::nextBossAttack(*state);
                const int zonesBefore = state->zones.count;
                const int bulletsBefore = state->enemyBullets.count;
                const bool ringCharging = boss.state == enemies::kBossRingCharge;
                stepFight(*state);
                const Enemy& after = bossOf(*state);
                if (walking && (after.state != enemies::kBossWalk || state->run.bossStep != stepBefore)) {
                    started.push_back(next);
                    const int added = state->zones.count - zonesBefore;
                    if (next == BossAttack::Circles) CHECK(added == moves.circleCount[phase]);
                    if (next == BossAttack::Line) CHECK(added == moves.lineCount[phase]);
                    if (next == BossAttack::Cross) CHECK(added == balance::kBossCrossArms);
                }
                if (ringCharging && after.state != enemies::kBossRingCharge) {
                    CHECK(state->enemyBullets.count - bulletsBefore == moves.ringBullets[phase]);
                }
                for (const Zone& zone : state->zones) {
                    CHECK(zone.maxLife == doctest::Approx(moves.telegraphSeconds));
                    if (zone.kind == ZoneKind::BossLine) CHECK(zone.radius == doctest::Approx(balance::kBossLineHalfWidth));
                }
                for (const EnemyBullet& bullet : state->enemyBullets) {
                    CHECK(length(bullet.vel) == doctest::Approx(moves.bulletSpeed).epsilon(0.001));
                }
                // Keep the boss in this phase and the player alive, so the cycle keeps going.
                Enemy& keep = bossOf(*state);
                keep.hp = keep.maxHp * kHpForPhase[phase];
                state->player.hp = state->player.maxHp;
                // A fixed way off, so ring bullets leave the boss before they reach the player.
                state->player.pos = keep.pos + Vec2{600.0f, 0.0f};
                state->player.prevPos = state->player.pos;
            }
            REQUIRE(static_cast<int>(started.size()) == wanted);
            for (int i = 0; i < wanted; ++i) {
                CAPTURE(i);
                CHECK(started[static_cast<size_t>(i)] == cycle.attacks[i % cycle.count]);
            }
        }
    }
}

TEST_CASE("every attack shows its warning for at least the telegraph time before it can hurt") {
    for (const Difficulty level : kLevels) {
        const balance::BossMoves& moves = balance::bossMoves(level);
        const int telegraphSteps = stepsFor(moves.telegraphSeconds);
        for (int phase = 0; phase < 3; ++phase) {
            const balance::BossCycle& cycle = moves.cycles[phase];
            for (int a = 0; a < cycle.count; ++a) {
                const BossAttack attack = cycle.attacks[a];
                CAPTURE(static_cast<int>(level));
                CAPTURE(phase);
                CAPTURE(static_cast<int>(attack));
                auto state = bossRun(level, Vec2{-500.0f, 0.0f}, phase);
                enemies::update(*state);
                state->zones.clear();
                const float startHp = state->player.hp;
                enemies::startBossAttack(*state, bossOf(*state), attack);

                int firstHarm = -1;
                int warnedSteps = 0;
                int chargeRun = 0;
                int dashes = 0;
                // Until it has hurt and the boss walks again. The next attack waits a full
                // interval, which is longer than any warning, so it can't be mixed in.
                for (int i = 0; i < stepsFor(8.0f); ++i) {
                    if (firstHarm < 0 && warningShows(*state)) warnedSteps++;
                    const uint8_t before = bossOf(*state).state;
                    stepFight(*state);
                    const Enemy& boss = bossOf(*state);
                    // Every dash, the first or a later one, is aimed with its own full warning.
                    if (boss.state == enemies::kBossDashCharge) chargeRun = before == enemies::kBossDashCharge ? chargeRun + 1 : 1;
                    if (before == enemies::kBossDashCharge && boss.state == enemies::kBossDash) {
                        CHECK(chargeRun + 1 >= telegraphSteps);
                        dashes++;
                    }
                    const bool harm = state->enemyBullets.count > 0 || boss.state == enemies::kBossDash || state->player.hp < startHp;
                    if (harm && firstHarm < 0) firstHarm = i + 1;
                    for (const Zone& zone : state->zones) CHECK(zone.maxLife == doctest::Approx(moves.telegraphSeconds));
                    if (firstHarm >= 0 && boss.state == enemies::kBossWalk) break;
                }
                REQUIRE(firstHarm > 0);
                CHECK(firstHarm >= telegraphSteps);
                CHECK(warnedSteps >= telegraphSteps);
                if (attack == BossAttack::Dash) CHECK(dashes == moves.dashCount[phase]);
            }
        }
    }
}

TEST_CASE("the cross is dodged by stepping away from the boss, and hits a player who stands still") {
    for (const Difficulty level : {Difficulty::Hard, Difficulty::Hell}) {
        CAPTURE(static_cast<int>(level));
        const int react = stepsFor(balance::kBossReactionSeconds);
        // Toward, away from and sideways to the boss all run down the middle of a gap.
        for (const Vec2 dir : {Vec2{1.0f, 0.0f}, Vec2{-1.0f, 0.0f}, Vec2{0.0f, 1.0f}}) {
            auto state = bossRun(level, Vec2{-600.0f, 0.0f}, 2);
            const float lost = playAttack(*state, BossAttack::Cross, 1.5f, [&](GameState& s, int i) {
                if (i >= react) walkPlayer(s, dir);
            });
            CHECK(lost == doctest::Approx(0.0f));
        }
        auto still = bossRun(level, Vec2{-600.0f, 0.0f}, 2);
        CHECK(playAttack(*still, BossAttack::Cross, 1.5f, standStill) > 0.0f);
    }
}

TEST_CASE("the trail is dodged by walking on, and hits a player who stands still") {
    const int react = stepsFor(balance::kBossReactionSeconds);
    const balance::BossMoves& hell = balance::bossMoves(Difficulty::Hell);
    // Until the last burst lands, and no longer, so the next attack doesn't start.
    const float seconds = hell.trailInterval * static_cast<float>(hell.trailBursts[2]) + hell.telegraphSeconds + 0.1f;
    REQUIRE(seconds < hell.attackInterval[2] + hell.trailInterval * static_cast<float>(hell.trailBursts[2]));
    auto state = bossRun(Difficulty::Hell, Vec2{-600.0f, 0.0f}, 2);
    int zonesSeen = 0;
    const float lost = playAttack(*state, BossAttack::Trail, seconds, [&](GameState& s, int i) {
        zonesSeen = std::max(zonesSeen, s.zones.count);
        if (i >= react) walkPlayer(s, Vec2{0.0f, 1.0f});
    });
    CHECK(zonesSeen >= 2);
    CHECK(lost == doctest::Approx(0.0f));

    auto still = bossRun(Difficulty::Hell, Vec2{-600.0f, 0.0f}, 2);
    CHECK(playAttack(*still, BossAttack::Trail, seconds, standStill) > 0.0f);
}

TEST_CASE("the spiral is dodged by stepping into a gap and backing away, and hits a player who stands still") {
    for (int phase = 1; phase < 3; ++phase) {
        CAPTURE(phase);
        const balance::BossMoves& hell = balance::bossMoves(Difficulty::Hell);
        const int react = stepsFor(balance::kBossReactionSeconds);
        // The first bullet flies straight at the player. Half a gap to the side is the middle of a gap.
        const float halfGap = 0.5f * balance::spiralTurnRadians(hell, phase);
        const float distanceOut = 400.0f;
        const float sideStep = distanceOut * std::tan(halfGap);
        const int sideSteps = static_cast<int>(std::ceil(sideStep / (balance::kPlayerSpeed * kDt)));
        // Past the last volley, but not so long that the boss catches up with the fleeing player.
        const float seconds = hell.telegraphSeconds + hell.spiralVolleySeconds * static_cast<float>(hell.spiralVolleys) + 1.0f;

        auto state = bossRun(Difficulty::Hell, Vec2{-distanceOut, 0.0f}, phase);
        int mostBullets = 0;
        const float lost = playAttack(*state, BossAttack::Spiral, seconds, [&](GameState& s, int i) {
            mostBullets = std::max(mostBullets, s.enemyBullets.count);
            if (i < react) return;
            if (i < react + sideSteps) {
                walkPlayer(s, Vec2{0.0f, 1.0f});
                return;
            }
            walkPlayer(s, s.player.pos - bossOf(s).pos);
        });
        CHECK(mostBullets >= hell.spiralArms[phase] * 2);
        CHECK(lost == doctest::Approx(0.0f));

        auto still = bossRun(Difficulty::Hell, Vec2{-distanceOut, 0.0f}, phase);
        CHECK(playAttack(*still, BossAttack::Spiral, seconds, standStill) > 0.0f);
    }
}

TEST_CASE("each dash of a run of dashes is dodged by stepping sideways once it is aimed") {
    for (const Difficulty level : {Difficulty::Hard, Difficulty::Hell}) {
        CAPTURE(static_cast<int>(level));
        const balance::BossMoves& moves = balance::bossMoves(level);
        const int react = stepsFor(balance::kBossReactionSeconds);
        const float seconds = static_cast<float>(moves.dashCount[2]) * (moves.telegraphSeconds + balance::kBossDashSeconds) + 0.05f;
        auto state = bossRun(level, Vec2{-450.0f, 0.0f}, 2);
        int charged = 0;
        int dashesSeen = 0;
        uint8_t lastState = enemies::kBossWalk;
        const float lost = playAttack(*state, BossAttack::Dash, seconds, [&](GameState& s, int) {
            const Enemy& boss = bossOf(s);
            if (boss.state == enemies::kBossDash && lastState != enemies::kBossDash) dashesSeen++;
            if (boss.state == enemies::kBossDashCharge) charged = lastState == enemies::kBossDashCharge ? charged + 1 : 1;
            else if (boss.state != enemies::kBossDash) charged = 0;
            lastState = boss.state;
            // Sideways to the aimed line, from the moment the warning is read until the dash ends.
            if (charged >= react) walkPlayer(s, perpendicularOf(boss.dir));
        });
        CHECK(dashesSeen == moves.dashCount[2]);
        CHECK(lost == doctest::Approx(0.0f));

        auto still = bossRun(level, Vec2{-450.0f, 0.0f}, 2);
        CHECK(playAttack(*still, BossAttack::Dash, seconds, standStill) > 0.0f);
    }
}

TEST_CASE("a boss fight on every difficulty is the same for the same seed and inputs") {
    for (const Difficulty level : kLevels) {
        CAPTURE(static_cast<int>(level));
        const auto play = [level](std::vector<uint64_t>& hashes) {
            auto state = std::make_unique<GameState>();
            sim::newRun(*state, 4242, level);
            state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 1;
            state->run.nextEliteEvent = 1000;
            for (int i = 0; i < stepsFor(75.0f); ++i) {
                InputFrame input;
                const float t = static_cast<float>(i) * kDt;
                input.moveX = std::cos(t * 0.7f);
                input.moveY = std::sin(t * 1.3f);
                input.dash = i % 300 == 150;
                if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
                sim::step(*state, input);
                state->events.clear();
                state->player.hp = state->player.maxHp;  // Stay alive, so every phase is seen.
                // Walk the boss through its phases, the same way in both runs.
                for (Enemy& enemy : state->enemies) {
                    if (enemy.type != EnemyType::Boss) continue;
                    if (i == stepsFor(25.0f)) enemy.hp = enemy.maxHp * 0.5f;
                    if (i == stepsFor(50.0f)) enemy.hp = enemy.maxHp * 0.2f;
                }
                hashes.push_back(sim::stateHash(*state));
            }
            CHECK(state->run.bossSpawned);
            CHECK(state->run.bossPhase == 2);
        };
        std::vector<uint64_t> first;
        std::vector<uint64_t> second;
        play(first);
        play(second);
        REQUIRE(first.size() == second.size());
        bool same = true;
        for (size_t i = 0; i < first.size(); ++i) same = same && first[i] == second[i];
        CHECK(same);
    }
}

TEST_CASE("the state hash tells the difficulties apart, and sees the boss's place in its cycle") {
    std::vector<uint64_t> hashes;
    for (const Difficulty level : kLevels) {
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 77, level);
        hashes.push_back(sim::stateHash(*state));
    }
    for (size_t a = 0; a < hashes.size(); ++a) {
        for (size_t b = a + 1; b < hashes.size(); ++b) CHECK(hashes[a] != hashes[b]);
    }

    auto state = bossRun(Difficulty::Hell, Vec2{-600.0f, 0.0f}, 0);
    const uint64_t before = sim::stateHash(*state);
    state->run.bossStep++;
    const uint64_t stepped = sim::stateHash(*state);
    state->run.bossRepeat++;
    CHECK(before != stepped);
    CHECK(stepped != sim::stateHash(*state));
}

TEST_CASE("on Hard and Hell, a player running away still can't leave the boss out of weapon range") {
    for (const Difficulty level : {Difficulty::Hard, Difficulty::Hell}) {
        CAPTURE(static_cast<int>(level));
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 5, level);
        state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 1;
        state->run.nextEliteEvent = 1000;
        sim::step(*state, InputFrame{});
        InputFrame run;
        run.moveX = 1.0f;
        int stepsInRange = 0;
        constexpr int kSteps = 3600;
        for (int i = 0; i < kSteps; ++i) {
            state->player.hp = state->player.maxHp;
            run.chooseCard = state->run.phase == RunPhase::LevelUp ? 0 : -1;
            // The last phase stands still the most, so it is the hardest case for the boss.
            for (Enemy& enemy : state->enemies) {
                if (enemy.type == EnemyType::Boss) enemy.hp = std::min(enemy.hp, enemy.maxHp * 0.2f);
            }
            sim::step(*state, run);
            state->events.clear();
            for (const Enemy& enemy : state->enemies) {
                if (enemy.type == EnemyType::Boss && distance(enemy.pos, state->player.pos) < balance::kWeaponRange) stepsInRange++;
            }
        }
        MESSAGE("boss in weapon range for ", stepsInRange, " of ", kSteps, " steps");
        CHECK(stepsInRange > kSteps * 3 / 4);
    }
}
