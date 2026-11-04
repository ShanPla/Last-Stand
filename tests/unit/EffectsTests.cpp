// The rules behind the effects, tested with no window: flash rates, hit pause, shake,
// hit flash in crowds, and the limits on damage numbers.
#include "components/Effects/Budget/Budget.h"
#include "components/Effects/DamageNumbers/DamageNumbers.h"
#include "components/Effects/FlashLimiter/FlashLimiter.h"
#include "components/Effects/HitFlash/HitFlash.h"
#include "components/Effects/HitPause/HitPause.h"
#include "components/Effects/ScreenShake/ScreenShake.h"
#include "components/Enemies/Waves/Waves.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include "doctest.h"

#include <cmath>
#include <memory>

using namespace ls;
using namespace ls::effects;

TEST_CASE("the flash limiter stays well under three flashes in any second") {
    FlashLimiter limiter;
    constexpr int kFrames = 60 * 20;
    constexpr float kStep = 1.0f / 60.0f;
    bool shown[kFrames] = {};
    int total = 0;
    // The worst case: a flash asked for on every single frame, for 20 seconds.
    for (int frame = 0; frame < kFrames; ++frame) {
        shown[frame] = limitFlash(limiter, static_cast<float>(frame) * kStep, 0.5f, 1.0f) > 0.0f;
        total += shown[frame] ? 1 : 0;
    }
    CHECK(total >= 15);  // It must still let flashes through.
    CHECK(total <= 21);
    for (int start = 0; start + 60 <= kFrames; ++start) {
        int inWindow = 0;
        for (int frame = start; frame < start + 60; ++frame) inWindow += shown[frame] ? 1 : 0;
        CHECK(inWindow <= 2);
    }
}

TEST_CASE("the flash limiter scales by the setting, and zero intensity uses nothing up") {
    FlashLimiter limiter;
    CHECK(limitFlash(limiter, 0.0f, 0.4f, 0.5f) == doctest::Approx(0.2f));
    CHECK(limitFlash(limiter, 0.1f, 0.4f, 1.0f) == 0.0f);  // Too soon.
    CHECK(limitFlash(limiter, 1.2f, 0.4f, 1.0f) == doctest::Approx(0.4f));

    FlashLimiter off;
    for (int i = 0; i < 100; ++i) CHECK(limitFlash(off, static_cast<float>(i), 1.0f, 0.0f) == 0.0f);
    CHECK(off.lastFlashSeconds < -100.0f);
}

TEST_CASE("hit pause holds back time, stays short and never stacks") {
    HitPause pause;
    CHECK(pause.trigger(0.05f));
    CHECK_FALSE(pause.trigger(0.05f));  // A second big moment doesn't add to it.
    float heldBack = 0.0f;
    for (int i = 0; i < 10; ++i) heldBack += kDt - pause.filter(kDt);
    CHECK(heldBack == doctest::Approx(0.05f).epsilon(0.001));
    CHECK_FALSE(pause.active());
    CHECK_FALSE(pause.trigger(0.05f));  // It has only just ended.
    for (int i = 0; i < 60; ++i) pause.filter(kDt);
    CHECK(pause.trigger(5.0f));
    float long_ = 0.0f;
    for (int i = 0; i < 60; ++i) long_ += kDt - pause.filter(kDt);
    CHECK(long_ <= kHitPauseLongest + 0.001f);  // Asking for a long one still gets a short one.
}

TEST_CASE("hit pause changes when steps run, not what they do") {
    const auto runWith = [](bool pausing) {
        auto state = std::make_unique<GameState>();
        sim::newRun(*state, 4242);
        HitPause pause;
        float accumulator = 0.0f;
        int steps = 0;
        // The same frame length either way, with pauses thrown in on one side only.
        for (int frame = 0; steps < 1200; ++frame) {
            if (pausing && frame % 37 == 0) pause.trigger(0.1f);
            accumulator += pause.filter(1.0f / 144.0f);
            while (accumulator >= kDt && steps < 1200) {
                sim::step(*state, InputFrame{});
                state->events.clear();
                accumulator -= kDt;
                steps++;
            }
        }
        return sim::stateHash(*state);
    };
    CHECK(runWith(false) == runWith(true));
}

TEST_CASE("screen shake only moves in whole pixels, follows the setting, and drains away") {
    ScreenShake shake;
    shake.add(1.0f);
    shake.update(0.001f, 1.0f, 1.0f, -1.0f);
    CHECK(shake.x() == static_cast<int>(ScreenShake::kMaxPixels));
    CHECK(shake.y() == -static_cast<int>(ScreenShake::kMaxPixels));

    ScreenShake half;
    half.add(1.0f);
    half.update(0.001f, 0.4f, 1.0f, 1.0f);
    CHECK(half.x() == 2);

    ScreenShake off;
    off.add(1.0f);
    for (int i = 0; i < 30; ++i) {
        off.update(kDt, 0.0f, 1.0f, 1.0f);
        CHECK(off.x() == 0);
        CHECK(off.y() == 0);
    }

    ScreenShake drains;
    drains.add(1.0f);
    for (int i = 0; i < 120; ++i) drains.update(kDt, 1.0f, 1.0f, 1.0f);
    CHECK(drains.trauma() == 0.0f);
    CHECK(drains.x() == 0);
    CHECK(drains.y() == 0);
}

TEST_CASE("a lone hit flashes strongly, and a crowd only tints") {
    CHECK(crowdFlashStrength(1) == doctest::Approx(kFlashLone));
    CHECK(crowdFlashStrength(500) == doctest::Approx(kFlashCrowd));
    CHECK(crowdFlashStrength(30) < crowdFlashStrength(5));
    CHECK(kFlashCrowd < 0.3f);  // Never mostly white in a crowd.

    CHECK(flashStride(10) == 1);
    for (int flashing : {37, 100, 500, 2000}) {
        // Every Nth by id, so about this many flash at once however big the crowd is.
        CHECK(flashing / flashStride(flashing) <= kMostFlashingAtOnce);
    }

    CHECK(hitFlashAmount(0.09f, 0.09f, 0.8f, 1.0f, false) == doctest::Approx(0.8f));
    CHECK(hitFlashAmount(0.045f, 0.09f, 0.8f, 1.0f, false) == doctest::Approx(0.2f));  // It falls fast.
    CHECK(hitFlashAmount(0.09f, 0.09f, 0.8f, 0.5f, false) == doctest::Approx(0.4f));   // The setting scales it.
    CHECK(hitFlashAmount(0.09f, 0.09f, 0.8f, 0.0f, false) == 0.0f);
    CHECK(hitFlashAmount(0.09f, 0.09f, 0.8f, 1.0f, true) <= kFlashReducedMost);
    CHECK(hitFlashAmount(100.0f, 0.09f, 0.8f, 1.0f, false) == doctest::Approx(0.8f));  // A held flash doesn't overshoot.
}

TEST_CASE("a token budget allows a burst, then only the steady rate") {
    Budget budget(10.0f, 5.0f);
    int taken = 0;
    for (int i = 0; i < 100; ++i) taken += budget.take() ? 1 : 0;
    CHECK(taken == 5);
    budget.refill(1.0f, 0.5f);  // 10 a second at half amount, capped at the burst.
    taken = 0;
    for (int i = 0; i < 100; ++i) taken += budget.take() ? 1 : 0;
    CHECK(taken == 5);
}

TEST_CASE("damage numbers merge nearby hits and never pass the hard cap") {
    auto pool = std::make_unique<DamageNumberPool>();
    // Thousands of hits a second, spread all over a crowd, for ten seconds.
    for (int frame = 0; frame < 600; ++frame) {
        for (int hit = 0; hit < 100; ++hit) {
            const Vec2 at{static_cast<float>((hit * 7919 + frame * 31) % 1200) - 600.0f, static_cast<float>((hit * 104729 + frame * 17) % 800) - 400.0f};
            pool->add(at, 20.0f, 10.0f + static_cast<float>(hit % 4));
        }
        pool->update(1.0f / 60.0f);
        CHECK(pool->live() <= DamageNumberPool::kCap);
    }
    CHECK(pool->merged() > pool->spawned());
    CHECK(pool->dropped() > 0);

    auto single = std::make_unique<DamageNumberPool>();
    for (int i = 0; i < 40; ++i) single->add(Vec2{10.0f, 10.0f}, 20.0f, 5.0f);
    CHECK(single->live() == 1);  // One spot gives one number, adding up.
    float shown = 0.0f;
    for (const DamageNumber& number : *single) {
        if (number.used) shown = number.value;
    }
    CHECK(shown == doctest::Approx(200.0f));
}

TEST_CASE("a big hit shows up even when the pool is full of small ones") {
    auto pool = std::make_unique<DamageNumberPool>();
    for (int frame = 0; frame < 300; ++frame) {
        for (int hit = 0; hit < 200; ++hit) {
            pool->add(Vec2{static_cast<float>(hit) * 120.0f, static_cast<float>(frame) * 90.0f}, 20.0f, 10.0f);
        }
        pool->update(1.0f / 60.0f);
    }
    pool->add(Vec2{-5000.0f, -5000.0f}, 20.0f, 400.0f);
    bool found = false;
    NumberTier tier = kTierSmall;
    for (const DamageNumber& number : *pool) {
        if (number.used && number.value == doctest::Approx(400.0f)) {
            found = true;
            tier = number.tier;
        }
    }
    CHECK(found);
    CHECK(tier == kTierHuge);
}

TEST_CASE("the boss starts further out than zombies do") {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 11);
    state->player.pos = Vec2{100.0f, -40.0f};
    state->run.nextEliteEvent = 1000;  // Skips elites, so only the boss arrives.
    state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt);
    waves::update(*state);
    REQUIRE(state->run.bossSpawned);
    bool found = false;
    for (const Enemy& enemy : state->enemies) {
        if (enemy.type != EnemyType::Boss) continue;
        found = true;
        CHECK(distance(enemy.pos, state->player.pos) == doctest::Approx(balance::kSpawnRadius + balance::kBossSpawnExtra).epsilon(0.001));
    }
    CHECK(found);
}

TEST_CASE("a new shake setting applies on the next update") {
    effects::ScreenShake shake;
    shake.add(1.0f);
    shake.update(0.001f, 1.0f, 1.0f, 1.0f);
    CHECK(shake.x() == 5);
    shake.resample();
    shake.update(0.001f, 0.3f, 1.0f, 1.0f);
    CHECK(shake.x() <= 2);
}
