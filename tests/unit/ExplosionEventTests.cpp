#include "doctest.h"

#include "core/Sim.h"
#include "data/Balance.h"

#include <memory>

using namespace ls;

TEST_CASE("an explosion that hits nothing still sends its event") {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, 3);
    state->player.weaponCount = 0;
    Projectile* grenade = state->projectiles.add();
    REQUIRE(grenade != nullptr);
    grenade->kind = ProjectileKind::Grenade;
    grenade->source = WeaponId::GrenadeLauncher;
    grenade->life = kDt * 0.5f;
    grenade->area = 80.0f;
    grenade->damage = 10.0f;
    sim::step(*state, InputFrame{});

    bool sent = false;
    for (const SimEvent& event : state->events) {
        if (event.type == EventType::Explosion && event.detail == static_cast<uint8_t>(WeaponId::GrenadeLauncher)) sent = true;
    }
    CHECK(sent);
    CHECK(state->run.damageByWeapon[static_cast<int>(WeaponId::GrenadeLauncher)] == 0.0f);
}
