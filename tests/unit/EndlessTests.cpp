#include "doctest.h"

#include "components/Enemies/Waves/Waves.h"
#include "core/Save.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace ls;

namespace {

constexpr uint32_t ticksAt(float seconds) { return static_cast<uint32_t>(seconds * 60.0f + 0.5f); }

Enemy* findBoss(GameState& state) {
    for (Enemy& enemy : state.enemies) {
        if (enemy.type == EnemyType::Boss) return &enemy;
    }
    return nullptr;
}

bool sawEvent(const GameState& state, EventType type) {
    for (const SimEvent& event : state.events) {
        if (event.type == type) return true;
    }
    return false;
}

// A fixed walk in a slow circle that always takes the first card. Health is topped up, so
// the run lives long enough to reach every part of the waves and the boss. A shield also
// keeps the player safe from one huge late hit, which topping up can't.
void playSteps(GameState& state, int steps, bool shield = false) {
    for (int i = 0; i < steps && state.run.phase != RunPhase::Won && state.run.phase != RunPhase::Dead; ++i) {
        InputFrame input;
        const float angle = static_cast<float>(state.run.tick) * 0.004f;
        input.moveX = std::cos(angle);
        input.moveY = std::sin(angle);
        if (state.run.phase == RunPhase::LevelUp) input.chooseCard = 0;
        sim::step(state, input);
        state.firstPlayer().hp = state.firstPlayer().maxHp;
        if (shield) state.firstPlayer().invulnerable = 1.0f;
        state.events.clear();
    }
}

// Plays on until the boss is out, then returns it.
Enemy* playUntilBoss(GameState& state, int maxSteps, bool shield = false) {
    for (int i = 0; i < maxSteps && !state.run.bossSpawned; ++i) playSteps(state, 1, shield);
    return findBoss(state);
}

// Kills the boss in the next step, the same way a last hit would: removeDead sees no HP.
void killBoss(GameState& state) {
    Enemy* boss = findBoss(state);
    REQUIRE(boss != nullptr);
    boss->hp = 0.0f;
    sim::step(state, InputFrame{});
}

std::unique_ptr<GameState> endlessRun(uint64_t seed, Difficulty difficulty = Difficulty::Normal) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, difficulty, maps::kDefaultMap, RunMode::Endless);
    return state;
}

struct StoryPoints {
    uint64_t early;     // Three minutes of waves.
    uint64_t lateWave;  // The last minute of waves, after a jump in time.
    uint64_t boss;      // Twenty seconds into the boss fight.
    uint64_t end;       // Just after the boss falls.
    RunPhase phase;
};

StoryPoints playStory(uint64_t seed, Difficulty difficulty, maps::MapId map) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, difficulty, map);
    StoryPoints points{};
    playSteps(*state, 180 * 60);
    points.early = sim::stateHash(*state);
    state->run.tick = 540 * 60;
    playSteps(*state, 60 * 60 + 30);
    points.lateWave = sim::stateHash(*state);
    playSteps(*state, 20 * 60);
    points.boss = sim::stateHash(*state);
    for (Enemy& enemy : state->enemies) {
        if (enemy.type == EnemyType::Boss) enemy.hp = 1.0f;
    }
    playSteps(*state, 10 * 60);
    points.end = sim::stateHash(*state);
    points.phase = state->run.phase;
    return points;
}

}  // namespace

TEST_CASE("a Story run is exactly what it was before Endless existed") {
    // These fingerprints were recorded with the game as it was just before Endless was added.
    // The same seed, input, map and difficulty must still give the very same run, through the
    // waves, the boss fight and the win. If a change to Story is meant, record new values;
    // the failure message prints them. A change to the boss's row of kEnemyStats changes the
    // last two of each row.
    struct Case {
        uint64_t seed;
        Difficulty difficulty;
        maps::MapId map;
        StoryPoints expected;
    };
    const Case cases[] = {
        {11, Difficulty::Normal, maps::MapId::City, {0x9c73f74d83c4e04ULL, 0x7f03e938e31c948cULL, 0x9a6a5c0c4fbefeaeULL, 0xe139cdd493597eabULL, RunPhase::Won}},
        // Seed 12 was recorded again after elites and the boss could always take a place in a full
        // enemy pool: its late Hell waves fill the pool, so that run changes from there on.
        {12, Difficulty::Hell, maps::MapId::Wasteland, {0xf56f805ef6448456ULL, 0xc348dfea259ff6f5ULL, 0x6d162940de3a7874ULL, 0x8f9aedf9fcb85bf5ULL, RunPhase::Won}},
        {13, Difficulty::Easy, maps::MapId::City, {0xb00e82f946b795cbULL, 0xc7671f417a637f8ULL, 0xce79c41c336f01d9ULL, 0xfcf3dd887121925fULL, RunPhase::Won}},
    };
    for (const Case& c : cases) {
        CAPTURE(c.seed);
        const StoryPoints got = playStory(c.seed, c.difficulty, c.map);
        char line[160];
        std::snprintf(line, sizeof(line), "now {0x%llxULL, 0x%llxULL, 0x%llxULL, 0x%llxULL}", static_cast<unsigned long long>(got.early),
                      static_cast<unsigned long long>(got.lateWave), static_cast<unsigned long long>(got.boss), static_cast<unsigned long long>(got.end));
        INFO(line);
        CHECK(got.early == c.expected.early);
        CHECK(got.lateWave == c.expected.lateWave);
        CHECK(got.boss == c.expected.boss);
        CHECK(got.end == c.expected.end);
        CHECK(got.phase == RunPhase::Won);
    }
}

TEST_CASE("an Endless run plays exactly like Story until the first boss arrives") {
    auto story = std::make_unique<GameState>();
    sim::newRun(*story, 21, Difficulty::Hard, maps::MapId::Wasteland);
    auto endless = std::make_unique<GameState>();
    sim::newRun(*endless, 21, Difficulty::Hard, maps::MapId::Wasteland, RunMode::Endless);
    playSteps(*story, 120 * 60);
    playSteps(*endless, 120 * 60);
    // Same run, but the Endless hash also covers its mode. So compare the pieces instead.
    CHECK(story->run.kills == endless->run.kills);
    CHECK(story->enemies.count == endless->enemies.count);
    CHECK(story->rng.state == endless->rng.state);
    CHECK(story->firstPlayer().pos.x == endless->firstPlayer().pos.x);
    endless->run.mode = RunMode::Story;
    CHECK(sim::stateHash(*story) == sim::stateHash(*endless));
}

TEST_CASE("killing the boss in Endless is not a win, and the waves come back") {
    auto state = endlessRun(31);
    state->run.tick = ticksAt(balance::kRunSeconds) - 2;
    REQUIRE(playUntilBoss(*state, 10) != nullptr);
    killBoss(*state);
    CHECK(sawEvent(*state, EventType::BossKilled));
    CHECK(state->run.phase == RunPhase::Playing);
    CHECK(state->run.bossesKilled == 1);
    CHECK_FALSE(state->run.bossSpawned);
    CHECK(state->run.bossHpFraction == 0.0f);
    CHECK(findBoss(*state) == nullptr);
    state->events.clear();

    // The loop starts from 6:00 of the table, which spawns Walkers, Brutes and Chargers.
    const int before = state->enemies.count;
    playSteps(*state, 10 * 60);
    CHECK(state->enemies.count > before + 20);
    CHECK(state->run.phase == RunPhase::Playing);
    // An elite from the loop's stretch comes at 6:30 of the table, 30 seconds after the kill.
    playSteps(*state, 25 * 60);
    bool elite = false;
    for (const Enemy& enemy : state->enemies) elite = elite || enemy.elite;
    CHECK(elite);

    // A new pass of the loop brings the elites back.
    state->run.lastBossKillTick = state->run.tick + 1 - ticksAt(balance::kEndlessLoopEndSec - balance::kEndlessLoopStartSec);
    playSteps(*state, 1);
    CHECK(state->run.nextEliteEvent == balance::firstLoopElite());
}

TEST_CASE("the Story boss still wins the run") {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 31);
    state->run.tick = ticksAt(balance::kRunSeconds) - 2;
    REQUIRE(playUntilBoss(*state, 10) != nullptr);
    killBoss(*state);
    CHECK(state->run.phase == RunPhase::Won);
    CHECK(state->run.bossesKilled == 0);
}

TEST_CASE("the boss returns at 20:00 and 30:00, tougher and with a harder move set each time") {
    auto state = endlessRun(41);
    state->run.tick = ticksAt(balance::kRunSeconds) - 2;
    Enemy* first = playUntilBoss(*state, 10);
    REQUIRE(first != nullptr);
    const float firstHp = first->maxHp;
    CHECK(firstHp == doctest::Approx(waves::bossHpForBuild(*state)));
    CHECK(&balance::bossMovesFor(state->run) == &balance::bossMoves(Difficulty::Normal));
    killBoss(*state);

    // Not a moment early.
    state->run.tick = ticksAt(2.0f * balance::kRunSeconds) - 2;
    playSteps(*state, 1);
    CHECK_FALSE(state->run.bossSpawned);
    Enemy* second = playUntilBoss(*state, 2);
    REQUIRE(second != nullptr);
    CHECK(state->timeSeconds() == doctest::Approx(2.0f * balance::kRunSeconds));
    CHECK(state->run.bossHpFraction == 1.0f);
    CHECK(second->maxHp == doctest::Approx(waves::bossHpForBuild(*state) * balance::kEndlessBossHpGrowth));
    CHECK(&balance::bossMovesFor(state->run) == &balance::bossMoves(Difficulty::Hard));
    // Its first wait comes from that set too. One step has run since it arrived.
    CHECK(second->timer == doctest::Approx(balance::bossMoves(Difficulty::Hard).attackInterval[0] - kDt));
    // The horde cleared for it, as it does for the first.
    CHECK(state->enemies.count == 1);
    killBoss(*state);
    CHECK(state->run.bossesKilled == 2);

    state->run.tick = ticksAt(3.0f * balance::kRunSeconds) - 2;
    Enemy* third = playUntilBoss(*state, 10);
    REQUIRE(third != nullptr);
    CHECK(third->maxHp == doctest::Approx(waves::bossHpForBuild(*state) * balance::kEndlessBossHpGrowth * balance::kEndlessBossHpGrowth));
    CHECK(&balance::bossMovesFor(state->run) == &balance::bossMoves(Difficulty::Hell));
    killBoss(*state);

    // Hell's set is the hardest there is, so later returns keep it.
    state->run.tick = ticksAt(4.0f * balance::kRunSeconds) - 2;
    REQUIRE(playUntilBoss(*state, 10) != nullptr);
    CHECK(&balance::bossMovesFor(state->run) == &balance::bossMoves(Difficulty::Hell));
}

TEST_CASE("an Endless boss killed late still leaves a break before the next one") {
    auto state = endlessRun(43);
    state->run.tick = ticksAt(2.0f * balance::kRunSeconds - 10.0f);  // The first boss dies at 19:50.
    state->run.nextEliteEvent = 1000;
    state->run.bossSpawned = false;
    REQUIRE(playUntilBoss(*state, 2) != nullptr);  // It is due already, so it arrives at once.
    killBoss(*state);
    const float killedAt = state->timeSeconds();
    playSteps(*state, 15 * 60);  // Past the 20:00 mark.
    CHECK_FALSE(state->run.bossSpawned);
    REQUIRE(playUntilBoss(*state, 60 * 60) != nullptr);
    CHECK(state->timeSeconds() == doctest::Approx(killedAt + balance::kEndlessBreakSeconds).epsilon(0.001));
}

TEST_CASE("Endless zombies keep getting tougher past 10:00, and Story's stop") {
    CHECK(balance::endlessHpGrowth(balance::kRunSeconds) == 1.0f);
    CHECK(balance::endlessSpawnGrowth(balance::kRunSeconds * 0.5f) == 1.0f);
    CHECK(balance::endlessHpGrowth(30.0f * 60.0f) > 4.0f);
    // An hour in, no build keeps up: every zombie has hundreds of times its 10:00 health.
    CHECK(balance::endlessHpGrowth(70.0f * 60.0f) > 100.0f);

    const auto walkerHpAt = [](RunMode mode, float seconds) {
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 51, Difficulty::Normal, maps::kDefaultMap, mode);
        state->run.tick = ticksAt(seconds);
        state->run.bossesKilled = mode == RunMode::Endless ? 2 : 0;  // Between bosses, so the table runs.
        state->run.lastBossKillTick = state->run.tick;
        state->run.nextEliteEvent = 1000;
        playSteps(*state, 5 * 60);
        float hp = 0.0f;
        for (const Enemy& enemy : state->enemies) {
            if (enemy.type == EnemyType::Walker && !enemy.elite) hp = std::max(hp, enemy.maxHp);
        }
        return hp;
    };
    const float storyHp = walkerHpAt(RunMode::Story, 590.0f);
    const float endlessHp = walkerHpAt(RunMode::Endless, 30.0f * 60.0f);
    REQUIRE(storyHp > 0.0f);
    CHECK(endlessHp > storyHp * 4.0f);
}

TEST_CASE("Endless runs are deterministic through two boss returns") {
    // Played twice with the same seed, with time jumps so the test stays quick. Every checkpoint
    // must match, and the third run with another seed must not.
    const auto play = [](uint64_t seed, uint64_t* hashes) {
        auto state = endlessRun(seed, Difficulty::Hard);
        int at = 0;
        playSteps(*state, 40 * 60, true);
        hashes[at++] = sim::stateHash(*state);
        for (int boss = 1; boss <= 3; ++boss) {
            state->run.tick = ticksAt(static_cast<float>(boss) * balance::kRunSeconds) - 120;
            REQUIRE(playUntilBoss(*state, 200, true) != nullptr);
            playSteps(*state, 12 * 60, true);  // Some of the fight, with its attacks.
            hashes[at++] = sim::stateHash(*state);
            if (boss < 3) {
                killBoss(*state);
                playSteps(*state, 40 * 60, true);  // Some of the loop that follows.
                hashes[at++] = sim::stateHash(*state);
            }
        }
        CHECK(state->run.bossesKilled == 2);
        return at;
    };
    uint64_t first[8] = {};
    uint64_t second[8] = {};
    uint64_t other[8] = {};
    const int count = play(61, first);
    REQUIRE(play(61, second) == count);
    play(62, other);
    for (int i = 0; i < count; ++i) {
        CAPTURE(i);
        CHECK(first[i] == second[i]);
        CHECK(first[i] != other[i]);
    }
}

TEST_CASE("the Endless fields only reach the hash in an Endless run") {
    auto state = endlessRun(71);
    playSteps(*state, 60);
    const uint64_t original = sim::stateHash(*state);
    state->run.bossesKilled++;
    CHECK(sim::stateHash(*state) != original);
    state->run.bossesKilled--;
    state->run.lastBossKillTick++;
    CHECK(sim::stateHash(*state) != original);
    state->run.lastBossKillTick--;
    state->run.mode = RunMode::Story;
    CHECK(sim::stateHash(*state) != original);
}

// --- Saves ----------------------------------------------------------------------

TEST_CASE("Endless best runs and the chosen mode survive a save and load") {
    SaveData original;
    original.mode = RunMode::Endless;
    original.bestFor(Difficulty::Normal) = BestRun{421.5f, 812, 14, 2, 9, 0};
    original.bestFor(RunMode::Endless, Difficulty::Normal) = BestRun{1834.25f, 5120, 31, 0, 4, 2};
    original.bestFor(RunMode::Endless, Difficulty::Hell) = BestRun{722.0f, 2210, 22, 0, 6, 1};
    original.bestFor(RunMode::Endless, Difficulty::Easy) = BestRun{3901.0f, 9000, 40, 0, 1, 6};
    const SaveData loaded = save::parse(save::serialize(original));
    CHECK(loaded.mode == RunMode::Endless);
    for (int level = 0; level < kDifficultyCount; ++level) {
        CAPTURE(level);
        for (const RunMode mode : {RunMode::Story, RunMode::Endless}) {
            const BestRun& want = original.bestFor(mode, static_cast<Difficulty>(level));
            const BestRun& got = loaded.bestFor(mode, static_cast<Difficulty>(level));
            CHECK(got.bestTimeSeconds == doctest::Approx(want.bestTimeSeconds));
            CHECK(got.mostKills == want.mostKills);
            CHECK(got.highestLevel == want.highestLevel);
            CHECK(got.wins == want.wins);
            CHECK(got.runs == want.runs);
            CHECK(got.mostBosses == want.mostBosses);
        }
    }
}

TEST_CASE("a save from before Endless loads as Story, with no Endless runs") {
    const char* old = "version=1\nmusic=0.5\ndifficulty=2\nmap=1\nbestTime=300\nmostKills=50\nhard.bestTime=200\nhard.runs=3\n";
    const SaveData loaded = save::parse(old);
    CHECK(loaded.mode == RunMode::Story);
    CHECK(loaded.difficulty == Difficulty::Hard);
    CHECK(loaded.bestFor(Difficulty::Normal).bestTimeSeconds == doctest::Approx(300.0f));
    CHECK(loaded.bestFor(Difficulty::Hard).runs == 3);
    for (int level = 0; level < kDifficultyCount; ++level) {
        CHECK(loaded.endlessBest[level].runs == 0);
        CHECK(loaded.endlessBest[level].bestTimeSeconds == 0.0f);
    }
    // A broken or unknown mode keeps the default.
    CHECK(save::parse("mode=7\n").mode == RunMode::Endless);  // Clamped into range.
    CHECK(save::parse("mode=banana\n").mode == RunMode::Story);
    CHECK(save::parse("mode=-3\n").mode == RunMode::Story);
}

TEST_CASE("Endless best runs are kept apart from Story's and compared by time") {
    SaveData data;
    save::FinishedRun story;
    story.difficulty = Difficulty::Normal;
    story.timeSeconds = 610.0f;
    story.kills = 3000;
    story.level = 30;
    story.won = true;
    save::recordRun(data, story);

    save::FinishedRun endless;
    endless.mode = RunMode::Endless;
    endless.difficulty = Difficulty::Normal;
    endless.timeSeconds = 1500.0f;
    endless.kills = 6000;
    endless.level = 35;
    endless.bossesKilled = 2;
    const save::NewBests first = save::recordRun(data, endless);
    CHECK(first.time);
    // Story's record is untouched by the Endless run.
    CHECK(data.bestFor(Difficulty::Normal).bestTimeSeconds == doctest::Approx(610.0f));
    CHECK(data.bestFor(Difficulty::Normal).runs == 1);
    CHECK(data.bestFor(Difficulty::Normal).wins == 1);
    const BestRun& best = data.bestFor(RunMode::Endless, Difficulty::Normal);
    CHECK(best.bestTimeSeconds == doctest::Approx(1500.0f));
    CHECK(best.mostBosses == 2);
    CHECK(best.wins == 0);
    CHECK(best.runs == 1);

    // More kills and a higher level, but a shorter run: not a new best in Endless.
    save::FinishedRun shorter = endless;
    shorter.timeSeconds = 1400.0f;
    shorter.kills = 9000;
    shorter.level = 50;
    const save::NewBests second = save::recordRun(data, shorter);
    CHECK_FALSE(second.time);
    CHECK_FALSE(second.kills);
    CHECK_FALSE(second.level);
    CHECK(best.runs == 2);

    save::FinishedRun longer = endless;
    longer.timeSeconds = 1600.0f;
    CHECK(save::recordRun(data, longer).time);

    // Another level's Endless table is separate too.
    CHECK(data.bestFor(RunMode::Endless, Difficulty::Hard).runs == 0);
}

TEST_CASE("merging saves raises the Endless records as well") {
    SaveData mine;
    SaveData stored;
    stored.bestFor(RunMode::Endless, Difficulty::Hell) = BestRun{900.0f, 100, 10, 0, 2, 1};
    mine.bestFor(RunMode::Endless, Difficulty::Hell) = BestRun{800.0f, 200, 5, 0, 1, 0};
    save::mergeBestRuns(mine, stored);
    const BestRun& merged = mine.bestFor(RunMode::Endless, Difficulty::Hell);
    CHECK(merged.bestTimeSeconds == doctest::Approx(900.0f));
    CHECK(merged.mostKills == 200);
    CHECK(merged.highestLevel == 10);
    CHECK(merged.runs == 2);
    CHECK(merged.mostBosses == 1);
}
