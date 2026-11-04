#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Blades/Blades.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <memory>

using namespace ls;

namespace {

std::unique_ptr<GameState> newRun() {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 11);
    return state;
}

}  // namespace

TEST_CASE("combat blades cut a zombie right next to the player") {
    auto state = newRun();
    state->player.weaponCount = 0;
    weapons::grant(*state, WeaponId::Blades);
    // Touching distance, where the outer circle alone can't reach it.
    const Vec2 pos{balance::kPlayerRadius + 13.0f, 0.0f};
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    REQUIRE(stats.area - balance::kBladeRadius - 13.0f > pos.x);
    enemies::spawn(*state, EnemyType::Brute, pos, false);
    const float startHp = state->enemies[0].hp;
    // A full turn, holding the zombie in place, so every blade passes it.
    const int steps = static_cast<int>(kTau / (stats.speed * kDt)) + 2;
    for (int i = 0; i < steps; ++i) {
        state->enemies[0].pos = pos;
        state->enemies[0].prevPos = pos;
        state->player.hp = state->player.maxHp;
        sim::step(*state, InputFrame{});
        state->events.clear();
    }
    CHECK(state->enemies[0].hp < startHp);
}

TEST_CASE("blades alternate between the outer and inner circles") {
    const balance::WeaponLevel stats = balance::kWeaponLevels[static_cast<int>(WeaponId::Blades)][0];
    CHECK(weapons::bladeOrbitRadius(0, stats) == doctest::Approx(stats.area));
    CHECK(weapons::bladeOrbitRadius(1, stats) == doctest::Approx(stats.area * balance::kBladeInnerRadiusFraction));
    CHECK(weapons::bladeOrbitRadius(2, stats) == doctest::Approx(stats.area));
}
