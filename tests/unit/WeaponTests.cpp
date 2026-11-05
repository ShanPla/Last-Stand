#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace ls;

namespace {

constexpr float kHugeHp = 1.0e5f;

// A run with no weapons at all, and a player who cannot die, so a test can add
// exactly the weapon it wants to look at.
std::unique_ptr<GameState> emptyRun(uint64_t seed = 1) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed);
    state->player.weaponCount = 0;
    state->player.maxHp = kHugeHp;
    state->player.hp = kHugeHp;
    return state;
}

// A run holding one weapon, at the given level.
std::unique_ptr<GameState> soloRun(WeaponId id, int level = 1) {
    auto state = emptyRun();
    weapons::grant(*state, id);
    state->player.weapons[0].level = static_cast<uint8_t>(level);
    weapons::refill(*state, state->player.weapons[0]);
    return state;
}

// A Brute that survives anything, so a test can read the damage dealt to it.
Enemy& addTarget(GameState& state, Vec2 pos, float hp = kHugeHp) {
    Enemy* enemy = enemies::spawn(state, EnemyType::Brute, pos, false);
    REQUIRE(enemy != nullptr);
    enemy->hp = hp;
    enemy->maxHp = hp;
    return *enemy;
}

void step(GameState& state, int count = 1) {
    for (int i = 0; i < count; ++i) sim::step(state, InputFrame{});
}

int countEvents(const GameState& state, EventType type) {
    int total = 0;
    for (const SimEvent& event : state.events) {
        if (event.type == type) total++;
    }
    return total;
}

int countZones(const GameState& state, ZoneKind kind) {
    int total = 0;
    for (const Zone& zone : state.zones) {
        if (zone.kind == kind) total++;
    }
    return total;
}

// Damage an enemy has taken. Comparing this, not raw HP, keeps float rounding out of the checks.
float lost(const Enemy& enemy) { return enemy.maxHp - enemy.hp; }

float credited(const GameState& state, WeaponId id) { return state.run.damageByWeapon[static_cast<int>(id)]; }

}  // namespace

TEST_CASE("one trigger pull uses one round, however many bullets it fires") {
    auto state = soloRun(WeaponId::Rifle, 5);
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    REQUIRE(stats.count == 3);
    addTarget(*state, Vec2{0.0f, 300.0f});
    step(*state);
    CHECK(state->player.weapons[0].magazine == stats.magazine - 1);
    CHECK(state->projectiles.count == 3);
    CHECK(countEvents(*state, EventType::WeaponFired) == 1);
    // The gun points at the target, and the event names the weapon.
    CHECK(state->player.aim.y == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(state->events[state->events.count - 1].detail == static_cast<uint8_t>(WeaponId::Rifle));
    // It is cooling down now, so the next step does not use another round.
    step(*state);
    CHECK(state->player.weapons[0].magazine == stats.magazine - 1);
}

TEST_CASE("a gun with no target waits, ready, and uses nothing") {
    auto state = soloRun(WeaponId::Rifle);
    const int magazine = state->player.weapons[0].magazine;
    step(*state, 90);
    CHECK(state->player.weapons[0].magazine == magazine);
    CHECK(state->player.weapons[0].cooldown <= 0.0f);
    CHECK(state->projectiles.count == 0);
    CHECK(countEvents(*state, EventType::WeaponFired) == 0);
    // As soon as a target appears, it fires on the very next step.
    addTarget(*state, Vec2{300.0f, 0.0f});
    step(*state);
    CHECK(state->player.weapons[0].magazine == magazine - 1);
}

TEST_CASE("reloading moves rounds from the reserve and blocks firing") {
    auto state = soloRun(WeaponId::Rifle);
    WeaponSlot& slot = state->player.weapons[0];
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, slot);
    slot.magazine = 1;
    slot.reserve = 50;
    addTarget(*state, Vec2{500.0f, 0.0f});

    step(*state);  // Fires the last loaded round.
    CHECK(slot.magazine == 0);
    state->events.clear();
    step(*state);
    CHECK(slot.reloadLeft > 0.0f);
    CHECK(countEvents(*state, EventType::ReloadStarted) == 1);

    // Nothing fires for the whole reload.
    int fired = 0;
    const int reloadSteps = static_cast<int>(stats.reload / kDt) - 2;
    for (int i = 0; i < reloadSteps; ++i) {
        state->events.clear();
        step(*state);
        fired += countEvents(*state, EventType::WeaponFired);
    }
    CHECK(fired == 0);
    CHECK(slot.magazine == 0);

    for (int i = 0; i < 10 && slot.reloadLeft > 0.0f; ++i) step(*state);
    CHECK(slot.reloadLeft == 0.0f);
    CHECK(slot.reserve == 50 - stats.magazine);
    // It may already have fired one round of the fresh magazine.
    CHECK(slot.magazine >= stats.magazine - 1);
    CHECK(slot.magazine <= stats.magazine);
}

TEST_CASE("a dry gun stops firing and reports it once") {
    auto state = soloRun(WeaponId::Smg);
    WeaponSlot& slot = state->player.weapons[0];
    slot.magazine = 0;
    slot.reserve = 0;
    addTarget(*state, Vec2{300.0f, 0.0f});
    int fired = 0;
    int dry = 0;
    for (int i = 0; i < 120; ++i) {
        state->events.clear();
        step(*state);
        fired += countEvents(*state, EventType::WeaponFired);
        dry += countEvents(*state, EventType::WeaponDry);
    }
    CHECK(fired == 0);
    CHECK(dry == 1);
    CHECK(slot.magazine == 0);
    CHECK(credited(*state, WeaponId::Smg) == 0.0f);
}

TEST_CASE("a dry Rifle still fires free, slower and weaker shots") {
    auto state = soloRun(WeaponId::Rifle);
    WeaponSlot& slot = state->player.weapons[0];
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, slot);
    slot.magazine = 0;
    slot.reserve = 0;
    addTarget(*state, Vec2{300.0f, 0.0f});
    step(*state);
    REQUIRE(state->projectiles.count == 1);
    CHECK(state->projectiles[0].damage == doctest::Approx(stats.damage * balance::kRifleDryDamageMult));
    CHECK(slot.cooldown > stats.cooldown * balance::kRifleDryCooldownMult - 2.0f * kDt);
    CHECK(slot.magazine == 0);
    CHECK(slot.reserve == 0);

    int fired = 1;
    int dry = countEvents(*state, EventType::WeaponDry);
    for (int i = 0; i < 120; ++i) {
        state->events.clear();
        step(*state);
        fired += countEvents(*state, EventType::WeaponFired);
        dry += countEvents(*state, EventType::WeaponDry);
    }
    CHECK(fired >= 2);
    CHECK(dry == 1);
    CHECK(slot.magazine == 0);
}

TEST_CASE("an ammo crate adds a fraction of the reserve, never past the maximum") {
    auto state = emptyRun();
    weapons::grant(*state, WeaponId::Rifle);
    weapons::grant(*state, WeaponId::Tesla);
    WeaponSlot& rifle = state->player.weapons[0];
    const int maxReserve = weapons::effectiveStats(*state, rifle).maxReserve;
    rifle.reserve = 0;
    rifle.dryReported = true;
    weapons::addAmmoFraction(*state, 0.5f);
    CHECK(rifle.reserve == maxReserve / 2);
    CHECK_FALSE(rifle.dryReported);
    weapons::addAmmoFraction(*state, 0.9f);
    CHECK(rifle.reserve == maxReserve);
    // Tesla uses no ammo, so a crate leaves it alone.
    CHECK(state->player.weapons[1].reserve == 0);
    CHECK(state->player.weapons[1].magazine == 0);
}

TEST_CASE("gaining a level refills the magazine and reserve for the new level") {
    auto state = soloRun(WeaponId::Rifle);
    WeaponSlot& slot = state->player.weapons[0];
    slot.magazine = 0;
    slot.reserve = 0;
    slot.reloadLeft = 0.5f;
    slot.dryReported = true;
    slot.level = kMaxLevelPerItem;
    weapons::refill(*state, slot);
    const balance::WeaponLevel stats = balance::kWeaponLevels[static_cast<int>(WeaponId::Rifle)][kMaxLevelPerItem - 1];
    CHECK(slot.magazine == stats.magazine);
    CHECK(slot.reserve == stats.maxReserve);
    CHECK(slot.reloadLeft == 0.0f);
    CHECK_FALSE(slot.dryReported);
}

TEST_CASE("a new weapon starts with a full magazine and reserve") {
    auto state = emptyRun();
    weapons::grant(*state, WeaponId::Shotgun);
    const balance::WeaponLevel stats = balance::kWeaponLevels[static_cast<int>(WeaponId::Shotgun)][0];
    CHECK(state->player.weapons[0].magazine == stats.magazine);
    CHECK(state->player.weapons[0].reserve == stats.maxReserve);
}

TEST_CASE("every weapon damages a zombie and is credited for it") {
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) {
        const WeaponId id = static_cast<WeaponId>(i);
        CAPTURE(i);
        auto state = soloRun(id);
        // Behind the player, where mines are laid and blades pass, and within every weapon's reach.
        Enemy& target = addTarget(*state, Vec2{-150.0f, 0.0f});
        const float startHp = target.hp;
        for (int s = 0; s < 400 && credited(*state, id) == 0.0f; ++s) step(*state);
        CHECK(credited(*state, id) > 0.0f);
        CHECK(state->enemies[0].hp < startHp);
        // Nobody else got the credit.
        for (int other = 0; other < static_cast<int>(WeaponId::Count); ++other) {
            if (other != i) CHECK(state->run.damageByWeapon[other] == 0.0f);
        }
    }
}

TEST_CASE("the Sniper beam hits every zombie in the line, once, and misses the rest") {
    auto state = soloRun(WeaponId::Sniper);
    addTarget(*state, Vec2{100.0f, 0.0f});
    addTarget(*state, Vec2{250.0f, 0.0f});
    addTarget(*state, Vec2{400.0f, 0.0f}, kHugeHp * 2.0f);  // The toughest one, so it is aimed at.
    addTarget(*state, Vec2{250.0f, 200.0f});
    step(*state);
    const float damage = weapons::effectiveStats(*state, state->player.weapons[0]).damage;
    CHECK(lost(state->enemies[0]) == doctest::Approx(damage));
    CHECK(lost(state->enemies[1]) == doctest::Approx(damage));
    CHECK(lost(state->enemies[2]) == doctest::Approx(damage));
    CHECK(lost(state->enemies[3]) == 0.0f);
    CHECK(countZones(*state, ZoneKind::SniperBeam) == 1);
    CHECK(state->player.weapons[0].magazine == balance::kWeaponLevels[static_cast<int>(WeaponId::Sniper)][0].magazine - 1);
    // The beam is only drawn afterwards, so a later step must not hurt anyone again.
    step(*state);
    CHECK(lost(state->enemies[0]) == doctest::Approx(damage));
}

TEST_CASE("Tesla chains from zombie to zombie within its jump range, up to its count") {
    auto state = soloRun(WeaponId::Tesla);
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    REQUIRE(stats.count == 3);
    addTarget(*state, Vec2{100.0f, 0.0f});
    addTarget(*state, Vec2{200.0f, 0.0f});
    addTarget(*state, Vec2{300.0f, 0.0f});
    addTarget(*state, Vec2{400.0f, 0.0f});
    addTarget(*state, Vec2{-100.0f, 500.0f});  // In range of the coil, but too far from the chain.
    step(*state);
    for (int i = 0; i < 3; ++i) CHECK(lost(state->enemies[i]) == doctest::Approx(stats.damage));
    CHECK(lost(state->enemies[3]) == 0.0f);
    CHECK(lost(state->enemies[4]) == 0.0f);
    CHECK(countZones(*state, ZoneKind::TeslaLink) == 3);
    CHECK(credited(*state, WeaponId::Tesla) == doctest::Approx(stats.damage * 3.0f));
    // Tesla uses no ammo.
    CHECK(state->player.weapons[0].magazine == 0);
    CHECK(countEvents(*state, EventType::WeaponDry) == 0);
}

TEST_CASE("Tesla never hits the same zombie twice in one chain") {
    auto state = soloRun(WeaponId::Tesla, 5);
    addTarget(*state, Vec2{100.0f, 0.0f});
    addTarget(*state, Vec2{150.0f, 0.0f});
    step(*state);
    const float damage = weapons::effectiveStats(*state, state->player.weapons[0]).damage;
    CHECK(lost(state->enemies[0]) == doctest::Approx(damage));
    CHECK(lost(state->enemies[1]) == doctest::Approx(damage));
    CHECK(countZones(*state, ZoneKind::TeslaLink) == 2);
}

TEST_CASE("the Shotgun fires count pellets across the cone with one round") {
    auto state = soloRun(WeaponId::Shotgun);
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    addTarget(*state, Vec2{150.0f, 0.0f});
    step(*state);
    REQUIRE(state->projectiles.count == stats.count);
    float lowest = 10.0f;
    float highest = -10.0f;
    for (const Projectile& pellet : state->projectiles) {
        const float angle = std::atan2(pellet.vel.y, pellet.vel.x);
        lowest = std::min(lowest, angle);
        highest = std::max(highest, angle);
        CHECK(pellet.radius == balance::kPelletRadius);
        CHECK(pellet.source == WeaponId::Shotgun);
    }
    CHECK(highest - lowest == doctest::Approx(balance::kShotgunConeRadians).epsilon(0.01));
    CHECK(state->player.weapons[0].magazine == stats.magazine - 1);
}

TEST_CASE("the Shotgun ignores a zombie beyond its reach") {
    auto state = soloRun(WeaponId::Shotgun);
    addTarget(*state, Vec2{balance::kWeaponRange - 10.0f, 0.0f});
    step(*state, 5);
    CHECK(state->projectiles.count == 0);
    CHECK(state->player.weapons[0].magazine == balance::kWeaponLevels[static_cast<int>(WeaponId::Shotgun)][0].magazine);
}

TEST_CASE("a grenade explodes when the fuse ends, hurting the area but not far zombies") {
    auto state = soloRun(WeaponId::GrenadeLauncher);
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    addTarget(*state, Vec2{200.0f, 0.0f});
    addTarget(*state, Vec2{240.0f, 0.0f});
    addTarget(*state, Vec2{200.0f, 400.0f});  // Far outside the blast.

    int explodedAt = -1;
    for (int s = 1; s <= 80 && explodedAt < 0; ++s) {
        step(*state);
        if (countZones(*state, ZoneKind::Explosion) > 0) explodedAt = s;
        if (explodedAt < 0) CHECK(credited(*state, WeaponId::GrenadeLauncher) == 0.0f);
    }
    REQUIRE(explodedAt > 0);
    CHECK(explodedAt == doctest::Approx(stats.duration / kDt).epsilon(0.05));
    CHECK(lost(state->enemies[0]) > 0.0f);
    CHECK(lost(state->enemies[1]) > 0.0f);
    CHECK(lost(state->enemies[2]) == 0.0f);
    CHECK(credited(*state, WeaponId::GrenadeLauncher) == doctest::Approx(stats.damage * 2.0f));
    // The grenade is gone after it goes off.
    CHECK(state->projectiles.count == 0);
}

TEST_CASE("a grenade does not collide in flight") {
    auto state = emptyRun();
    Projectile* grenade = state->projectiles.add();
    grenade->pos = Vec2{0.0f, 500.0f};
    grenade->damage = 30.0f;
    grenade->radius = balance::kGrenadeRadius;
    grenade->area = 80.0f;
    grenade->life = 10.0f * kDt;
    grenade->kind = ProjectileKind::Grenade;
    grenade->source = WeaponId::GrenadeLauncher;
    addTarget(*state, Vec2{0.0f, 500.0f});
    step(*state, 5);
    CHECK(state->projectiles.count == 1);
    CHECK(lost(state->enemies[0]) == 0.0f);
}

TEST_CASE("a mine explodes when a zombie comes close, and vanishes harmlessly when old") {
    {
        auto state = emptyRun();
        Projectile* mine = state->projectiles.add();
        mine->pos = Vec2{500.0f, 500.0f};
        mine->damage = 32.0f;
        mine->radius = balance::kMineTriggerRadius;
        mine->area = 85.0f;
        mine->life = 7.0f;
        mine->kind = ProjectileKind::Mine;
        mine->source = WeaponId::Landmines;
        addTarget(*state, Vec2{500.0f + balance::kMineTriggerRadius, 500.0f});
        step(*state);
        CHECK(countZones(*state, ZoneKind::Explosion) == 1);
        CHECK(credited(*state, WeaponId::Landmines) == doctest::Approx(32.0f));
        CHECK(state->projectiles.count == 0);
    }
    {
        auto state = emptyRun();
        Projectile* mine = state->projectiles.add();
        mine->pos = Vec2{500.0f, 500.0f};
        mine->damage = 32.0f;
        mine->radius = balance::kMineTriggerRadius;
        mine->area = 85.0f;
        mine->life = 3.0f * kDt;
        mine->kind = ProjectileKind::Mine;
        mine->source = WeaponId::Landmines;
        addTarget(*state, Vec2{500.0f + 300.0f, 500.0f});
        step(*state, 2);
        CHECK(state->projectiles.count == 1);
        step(*state, 3);
        CHECK(state->projectiles.count == 0);
        CHECK(countZones(*state, ZoneKind::Explosion) == 0);
        CHECK(credited(*state, WeaponId::Landmines) == 0.0f);
    }
}

TEST_CASE("mines are dropped behind the player, and only while a zombie is in range") {
    auto state = soloRun(WeaponId::Landmines);
    state->player.facing = Vec2{0.0f, 1.0f};
    step(*state, 10);
    CHECK(state->projectiles.count == 0);
    CHECK(state->player.weapons[0].magazine == balance::kWeaponLevels[static_cast<int>(WeaponId::Landmines)][0].magazine);

    addTarget(*state, Vec2{0.0f, 600.0f});
    step(*state);
    REQUIRE(state->projectiles.count == 1);
    const Projectile& mine = state->projectiles[0];
    CHECK(mine.kind == ProjectileKind::Mine);
    CHECK(mine.pos.y < 0.0f);
    CHECK(mine.vel.x == 0.0f);
    CHECK(mine.vel.y == 0.0f);
}

TEST_CASE("a rocket turns toward its target at a limited rate, then explodes on contact") {
    {
        auto state = emptyRun();
        Projectile* rocket = state->projectiles.add();
        rocket->vel = Vec2{330.0f, 0.0f};
        rocket->damage = 11.0f;
        rocket->radius = balance::kRocketRadius;
        rocket->area = 40.0f;
        rocket->life = 3.0f;
        rocket->kind = ProjectileKind::Rocket;
        rocket->source = WeaponId::Rockets;
        addTarget(*state, Vec2{0.0f, 300.0f});
        step(*state);
        const Vec2 vel = state->projectiles[0].vel;
        const float turned = std::atan2(vel.y, vel.x);
        CHECK(turned > 0.0f);
        CHECK(turned <= balance::kRocketTurnRate * kDt + 1e-4f);
        CHECK(length(vel) == doctest::Approx(330.0f).epsilon(0.001));
    }
    {
        auto state = soloRun(WeaponId::Rockets);
        addTarget(*state, Vec2{150.0f, 0.0f});
        for (int s = 0; s < 120 && countZones(*state, ZoneKind::Explosion) == 0; ++s) step(*state);
        CHECK(countZones(*state, ZoneKind::Explosion) > 0);
        CHECK(credited(*state, WeaponId::Rockets) > 0.0f);
    }
}

TEST_CASE("combat blades spin, and hit a touching zombie once per cooldown") {
    auto state = soloRun(WeaponId::Blades);
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    Enemy& target = addTarget(*state, Vec2{stats.area, 0.0f});
    step(*state);
    CHECK(state->run.bladeAngle == doctest::Approx(stats.speed * kDt));
    CHECK(lost(target) == doctest::Approx(stats.damage));
    CHECK(target.bladeCooldown > 0.0f);
    step(*state, 3);
    CHECK(lost(target) == doctest::Approx(stats.damage));

    // With the cooldown over, a blade that is touching it cuts again.
    target.bladeCooldown = 0.0f;
    const float nextAngle = state->run.bladeAngle + stats.speed * kDt;
    target.pos = state->player.pos + fromAngle(nextAngle) * stats.area;
    step(*state);
    CHECK(lost(target) == doctest::Approx(stats.damage * 2.0f));
}

TEST_CASE("a hostile boss circle hurts a player inside it and not one outside") {
    for (const bool inside : {true, false}) {
        CAPTURE(inside);
        auto state = emptyRun();
        state->player.hp = 100.0f;
        Zone* zone = state->zones.add();
        zone->kind = ZoneKind::BossCircle;
        zone->hostile = true;
        zone->a = inside ? Vec2{40.0f, 0.0f} : Vec2{400.0f, 0.0f};
        zone->radius = 100.0f;
        zone->life = 5.0f * kDt;
        zone->maxLife = zone->life;
        zone->damage = 26.0f;
        step(*state, 3);
        // Nothing lands until the telegraph is over.
        CHECK(state->player.hp == 100.0f);
        step(*state, 4);
        CHECK(state->zones.count == 0);
        if (inside) {
            CHECK(state->player.hp < 100.0f);
        } else {
            CHECK(state->player.hp == 100.0f);
        }
    }
}

TEST_CASE("a hostile boss line hurts a player along the line, by its half width") {
    for (const bool inside : {true, false}) {
        CAPTURE(inside);
        auto state = emptyRun();
        state->player.hp = 100.0f;
        Zone* zone = state->zones.add();
        zone->kind = ZoneKind::BossLine;
        zone->hostile = true;
        zone->a = Vec2{-500.0f, inside ? 20.0f : 100.0f};
        zone->b = Vec2{500.0f, inside ? 20.0f : 100.0f};
        zone->radius = 42.0f;
        zone->life = 2.0f * kDt;
        zone->maxLife = zone->life;
        zone->damage = 30.0f;
        step(*state, 3);
        CHECK(state->zones.count == 0);
        if (inside) {
            CHECK(state->player.hp < 100.0f);
        } else {
            CHECK(state->player.hp == 100.0f);
        }
    }
}

TEST_CASE("player zones expire after their life and do not hurt the player") {
    auto state = emptyRun();
    state->player.hp = 100.0f;
    Zone* zone = state->zones.add();
    zone->kind = ZoneKind::Explosion;
    zone->a = Vec2{};
    zone->radius = 100.0f;
    zone->life = 3.0f * kDt;
    zone->damage = 50.0f;
    step(*state, 5);
    CHECK(state->zones.count == 0);
    CHECK(state->player.hp == 100.0f);
}

TEST_CASE("a flame passes through a zombie but hits it only once") {
    auto state = soloRun(WeaponId::Flamethrower);
    const balance::WeaponLevel stats = weapons::effectiveStats(*state, state->player.weapons[0]);
    addTarget(*state, Vec2{120.0f, 0.0f});
    step(*state);
    REQUIRE(state->projectiles.count == 1);
    const Projectile& flame = state->projectiles[0];
    CHECK(flame.kind == ProjectileKind::Flame);
    CHECK(flame.pierce == stats.pierce);
    CHECK(flame.radius == doctest::Approx(balance::kFlameRadius * stats.area / balance::kFlameBaseArea));
    // No further flames, so only this one can hurt the zombie while it flies through.
    state->player.weapons[0].cooldown = 100.0f;
    step(*state, 60);
    CHECK(lost(state->enemies[0]) == doctest::Approx(stats.damage));
}

TEST_CASE("same seed gives the same run with every weapon in use") {
    const WeaponId sets[2][5] = {
        {WeaponId::Shotgun, WeaponId::Smg, WeaponId::Sniper, WeaponId::Flamethrower, WeaponId::GrenadeLauncher},
        {WeaponId::Rockets, WeaponId::Landmines, WeaponId::Tesla, WeaponId::Blades, WeaponId::Smg},
    };
    for (const auto& set : sets) {
        std::vector<uint64_t> hashes[2];  // One per second, so a divergence that later cancels out still shows.
        float credits[2] = {};
        int kills[2] = {};
        for (int run = 0; run < 2; ++run) {
            auto state = std::make_unique<GameState>();
            sim::newRun(*state, 4242);
            for (const WeaponId id : set) weapons::grant(*state, id);
            for (int i = 0; i < state->player.weaponCount; ++i) {
                state->player.weapons[i].level = 3;
                weapons::refill(*state, state->player.weapons[i]);
            }
            state->player.maxHp = kHugeHp;
            state->player.hp = kHugeHp;
            for (int s = 0; s < 60 * 45; ++s) {
                InputFrame input;
                const float angle = static_cast<float>(state->run.tick) * 0.01f;
                input.moveX = std::cos(angle);
                input.moveY = std::sin(angle);
                if (state->run.phase == RunPhase::LevelUp) input.chooseCard = 0;
                sim::step(*state, input);
                state->events.clear();
                if (s % 60 == 59) hashes[run].push_back(sim::stateHash(*state));
            }
            kills[run] = state->run.kills;
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) credits[run] += state->run.damageByWeapon[i];
        }
        CHECK(kills[0] > 0);
        CHECK(credits[0] > 0.0f);
        CHECK(hashes[0].size() == 45);
        CHECK(hashes[0] == hashes[1]);
        CHECK(credits[0] == credits[1]);
        CHECK(kills[0] == kills[1]);
    }
}
