#include "doctest.h"

#include "components/Enemies/Waves/Waves.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> newRun() {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 11);
    return state;
}

}  // namespace

TEST_CASE("a stronger build gets a tougher boss, but still kills it faster") {
    auto weak = newRun();
    auto strong = newRun();
    strong->player.weapons[0].level = kMaxLevelPerItem;
    weapons::grant(*strong, WeaponId::Sniper);
    weapons::grant(*strong, WeaponId::Smg);
    for (int i = 1; i < strong->player.weaponCount; ++i) strong->player.weapons[i].level = kMaxLevelPerItem;

    const float weakDps = weapons::estimatedBossDps(*weak);
    const float strongDps = weapons::estimatedBossDps(*strong);
    REQUIRE(strongDps > weakDps);
    const float weakHp = waves::bossHpForBuild(*weak);
    const float strongHp = waves::bossHpForBuild(*strong);
    CHECK(strongHp > weakHp);
    // Time to kill is HP divided by damage per second.
    CHECK(strongHp / strongDps < weakHp / weakDps);
}

TEST_CASE("boss HP stays between its limits") {
    // No weapons: the formula gives 0, so only the lower limit can hold it up.
    auto weak = newRun();
    weak->player.weaponCount = 0;
    CHECK(waves::bossHpForBuild(*weak) == doctest::Approx(balance::kBossHpMin));

    // A huge damage bonus: the formula alone goes far past the top, so only the upper
    // limit can hold it down.
    auto strong = newRun();
    strong->player.stats.damageMult = 1000.0f;
    const float strength = weapons::estimatedBossDps(*strong) / balance::kBossReferenceDps;
    REQUIRE(balance::kBossHpAtReferenceDps * std::pow(strength, balance::kBossHpScalePower) > balance::kBossHpMax);
    CHECK(waves::bossHpForBuild(*strong) == doctest::Approx(balance::kBossHpMax));
}

TEST_CASE("the boss spawns with the HP for the build") {
    auto state = newRun();
    state->player.weapons[0].level = kMaxLevelPerItem;
    const float expected = waves::bossHpForBuild(*state);
    state->run.tick = static_cast<uint32_t>(balance::kRunSeconds / kDt) - 1;
    state->run.nextEliteEvent = 1000;
    sim::step(*state, InputFrame{});
    REQUIRE(state->enemies.count == 1);
    CHECK(state->enemies[0].maxHp == doctest::Approx(expected));
    CHECK(state->enemies[0].hp == doctest::Approx(expected));
}
