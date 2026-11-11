#include "core/Sim.h"

#include "components/Enemies/Enemies.h"
#include "components/Enemies/Waves/Waves.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Pickups/Pickups.h"
#include "components/Player/Player.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"

#include <cstring>
#include <type_traits>

namespace ls::sim {

namespace {

// Debug only: keeps a known number of objects alive for performance measurement.
void applyStressTargets(GameState& state) {
    const Vec2 center = state.firstPlayer().pos;
    while (state.enemies.count < state.run.debugEnemyTarget) {
        const Vec2 pos = center + fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(120.0f, 650.0f);
        if (!enemies::spawn(state, EnemyType::Walker, pos, false)) break;
    }
    while (state.projectiles.count < state.run.debugProjectileTarget) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        const Vec2 pos = center + fromAngle(state.rng.range(0.0f, kTau)) * state.rng.range(0.0f, 600.0f);
        shot->pos = pos;
        shot->prevPos = pos;
        shot->vel = fromAngle(state.rng.range(0.0f, kTau)) * 200.0f;
        shot->damage = 1.0f;
        shot->radius = balance::kBulletRadius;
        shot->life = 1.5f;
        shot->pierce = 2;
    }
    // The stress test must keep running, so nobody can die or level up.
    if (state.run.debugEnemyTarget > 0) {
        for (int slot = 0; slot < state.playerCount(); ++slot) {
            state.players[slot].hp = state.players[slot].maxHp;
            state.players[slot].pendingLevels = 0;
        }
    }
}

// A card or a reroll from one player's input. If both arrive in one step, the card wins and
// the reroll is not spent. The card index means a card on screen now, and a reroll would
// swap it for one the player hasn't seen.
void answerCards(GameState& state, int slot, const InputFrame& input) {
    if (input.chooseCard >= 0) {
        upgrades::choose(state, slot, input.chooseCard);
    } else if (input.reroll) {
        upgrades::reroll(state, slot);
    }
}

}  // namespace

void newRun(GameState& state, uint64_t seed, Difficulty difficulty, maps::MapId map, RunMode mode, int playerCount) {
    state.rng = Rng{};
    state.rng.seed(seed);
    state.run = RunState{};
    state.run.seed = seed;
    state.run.difficulty = difficulty;
    state.run.map = map;
    state.run.mode = mode;
    state.run.playerCount = playerCount < 1 ? 1 : (playerCount > kMaxPlayers ? kMaxPlayers : playerCount);
    state.run.nextPickupDropKills = balance::kKillsPerPickupDrop;
    state.enemies.clear();
    state.projectiles.clear();
    state.enemyBullets.clear();
    state.zones.clear();
    state.gems.clear();
    state.pickups.clear();
    state.events.clear();
    // Every slot is cleared, so a slot left over from a bigger run holds nothing stale.
    for (int slot = 0; slot < kMaxPlayers; ++slot) player::reset(state, slot);
    const int count = state.playerCount();
    for (int slot = 0; slot < count; ++slot) {
        Player& player = state.players[slot];
        // A team starts in a small ring around the middle. One player starts in the middle.
        if (count > 1) player.pos = fromAngle(kTau * static_cast<float>(slot) / static_cast<float>(count)) * balance::kCoopStartSpread;
        player.rerollsLeft = balance::kStartingRerolls;
        upgrades::recomputeStats(state, slot);
        player.hp = player.maxHp;
        weapons::grant(state, slot, WeaponId::Rifle);
    }
    obstacles::invalidate(state);
    obstacles::update(state);
    for (int slot = 0; slot < count; ++slot) {
        Player& player = state.players[slot];
        obstacles::resolveCircle(state, player.pos, balance::kPlayerRadius);
        player.prevPos = player.pos;
    }
}

void setMap(GameState& state, maps::MapId map) {
    state.run.map = map;
    // A crushed car is named by where it stands, which only means something on its own map.
    state.run.crushedCount = 0;
    obstacles::invalidate(state);
    obstacles::update(state);
}

void step(GameState& state, const InputFrame inputs[], int inputCount) {
    RunState& run = state.run;
    if (run.phase == RunPhase::Won || run.phase == RunPhase::Dead) return;
    const InputFrame idle;
    const auto inputOf = [&](int slot) -> const InputFrame& { return slot < inputCount ? inputs[slot] : idle; };
    const int count = state.playerCount();

    // Single-player only: the run waits on the level-up screen until a card is taken.
    if (run.phase == RunPhase::LevelUp) {
        answerCards(state, 0, inputOf(0));
        return;
    }
    // Co-op: each player answers their own cards when ready, and the run goes on meanwhile.
    if (state.isCoop()) {
        for (int slot = 0; slot < count; ++slot) {
            if (state.players[slot].cardsOpen) answerCards(state, slot, inputOf(slot));
        }
    }

    run.tick++;
    obstacles::update(state);
    applyStressTargets(state);
    for (int slot = 0; slot < count; ++slot) {
        const InputFrame& input = inputOf(slot);
        player::update(state, slot, input.moveX, input.moveY, input.dash);
    }
    waves::update(state);
    enemies::update(state);
    weapons::update(state);
    enemies::updateBullets(state);
    pickups::update(state);
    enemies::removeDead(state);
    if (run.debugEnemyTarget > 0) {
        for (int slot = 0; slot < count; ++slot) state.players[slot].pendingLevels = 0;
    }
    upgrades::update(state);

    // One rule for a step with a lethal hit: it is decided here, after all damage of the step
    // has landed, so the result never depends on the order of the calls above. A boss killed in
    // the same step still counts as killed, but the run is a loss, not a win.
    if (!state.isCoop()) {
        // Single-player: 0 HP ends the run, and death beats everything else in that step.
        Player& player = state.firstPlayer();
        if (player.hp <= 0.0f) {
            run.phase = RunPhase::Dead;
            pushPlayerEvent(state, EventType::PlayerDied, 0, player.pos);
        }
        return;
    }
    // Co-op: 0 HP puts a player down, and the run ends only when the whole team is down at once.
    if (player::downAndRevive(state)) {
        run.phase = RunPhase::Dead;
        pushEvent(state, EventType::PlayerDied, state.firstPlayer().pos);
    }
}

uint64_t stateHash(const GameState& state) {
    // FNV-1a over the values that matter. Hashing fields one at a time avoids
    // reading uninitialized padding bytes inside the structs.
    uint64_t hash = 14695981039346656037ULL;
    const auto mixBytes = [&hash](const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    };
    // One entry point for every number, flag and enum. Each is widened to a fixed
    // size first, so the hash does not depend on how a struct is laid out.
    const auto mix = [&](auto value) {
        using T = decltype(value);
        if constexpr (std::is_floating_point_v<T>) {
            const float narrowed = static_cast<float>(value);
            mixBytes(&narrowed, sizeof(narrowed));
        } else {
            const int64_t wide = static_cast<int64_t>(value);  // Integers, bools and enums.
            mixBytes(&wide, sizeof(wide));
        }
    };
    const auto mixVec = [&](Vec2 v) { mix(v.x); mix(v.y); };

    // The fields that every player has had since before co-op. A player's cards, rerolls and
    // blade angle are not here, because they used to belong to the run and are mixed in where
    // the run's used to be.
    const auto mixPlayer = [&](const Player& player) {
        mixVec(player.pos);
        mixVec(player.facing);
        mixVec(player.aim);
        mix(player.hp);
        mix(player.maxHp);
        mix(player.invulnerable);
        mix(player.dashLeft);
        mix(player.dashCooldown);
        mix(player.dashInvulnerable);
        mixVec(player.dashDir);
        mix(player.level);
        mix(player.xp);
        mix(player.xpToNext);
        mix(player.stats.damageMult);
        mix(player.stats.cooldownMult);
        mix(player.stats.areaMult);
        mix(player.stats.moveSpeed);
        mix(player.stats.pickupRange);
        mix(player.stats.armor);
        mix(player.weaponCount);
        for (int i = 0; i < player.weaponCount; ++i) {
            const WeaponSlot& slot = player.weapons[i];
            mix(slot.id);
            mix(slot.level);
            mix(slot.evolved);
            mix(slot.cooldown);
            mix(slot.magazine);
            mix(slot.reserve);
            mix(slot.reloadLeft);
            mix(slot.dryReported);
        }
        mix(player.passiveCount);
        for (int i = 0; i < player.passiveCount; ++i) {
            mix(player.passives[i].id);
            mix(player.passives[i].level);
        }
    };
    const auto mixCards = [&](const Player& player) {
        mix(player.rerollsLeft);
        mix(player.pendingLevels);
        mix(player.offerCount);
        for (const Card& card : player.offers) {
            mix(card.kind);
            mix(card.weapon);
            mix(card.passive);
            mix(card.newLevel);
        }
    };

    // Everything below can change what a later step does. What is left out is only
    // for drawing: previous positions (derived from the last step), hit flashes,
    // the event list (cleared by its reader), the enemy grid (rebuilt each step) and the
    // obstacle cache (worked out from the map, the seed, the players' tiles and the crushed list).
    // A new field that a step reads must be added here, or the determinism tests
    // cannot see it.
    mixBytes(&state.rng.state, sizeof(state.rng.state));
    mixBytes(&state.rng.stream, sizeof(state.rng.stream));

    // The order below is exactly the order from before co-op, so a single-player run keeps
    // every fingerprint it had then, and tests can compare it with hashes recorded back then.
    const RunState& run = state.run;
    const Player& first = state.firstPlayer();
    mix(run.phase);
    mix(run.difficulty);
    mix(run.map);
    mixBytes(&run.seed, sizeof(run.seed));
    mix(run.tick);
    mix(run.nextId);
    mix(run.kills);
    mixCards(first);
    for (float damage : run.damageByWeapon) mix(damage);
    for (float owed : run.waveAccumulators) mix(owed);
    mix(run.nextEliteEvent);
    mix(run.nextPickupDropKills);
    mix(first.bladeAngle);
    mix(run.bossSpawned);
    mix(run.bossPhase);
    mix(run.bossStep);
    mix(run.bossRepeat);
    mix(run.bossHpFraction);
    mix(run.debugEnemyTarget);
    mix(run.debugProjectileTarget);
    // Flat cars stop being solid, so they change every later step.
    mix(run.crushedCount);
    for (int i = 0; i < run.crushedCount; ++i) {
        mix(run.crushed[i].texelX);
        mix(run.crushed[i].texelY);
    }
    // The Endless fields are only mixed in for an Endless run. A Story run never reads them,
    // and leaving them out keeps every Story fingerprint exactly what it was before Endless
    // existed, so a test can compare Story runs with hashes recorded back then.
    if (run.mode != RunMode::Story) {
        mix(run.mode);
        mix(run.bossesKilled);
        mix(run.lastBossKillTick);
    }

    mixPlayer(first);

    mix(state.enemies.count);
    for (const Enemy& enemy : state.enemies) {
        mixVec(enemy.pos);
        mixVec(enemy.knock);
        mixVec(enemy.dir);
        mix(enemy.hp);
        mix(enemy.maxHp);
        mix(enemy.radius);
        mix(enemy.speed);
        mix(enemy.contactDamage);
        mix(enemy.timer);
        mix(enemy.bladeCooldown);
        mix(enemy.id);
        mix(enemy.stuck);
        mixVec(enemy.stuckFrom);
        mix(enemy.stuckQueued);
        mix(enemy.type);
        mix(enemy.state);
        mix(enemy.elite);
    }
    mix(state.projectiles.count);
    for (const Projectile& shot : state.projectiles) {
        mixVec(shot.pos);
        mixVec(shot.vel);
        mix(shot.damage);
        mix(shot.radius);
        mix(shot.life);
        mix(shot.area);
        mix(shot.lastHitId);
        mix(shot.pierce);
        mix(shot.kind);
        mix(shot.source);
        mix(shot.evolved);
    }
    mix(state.enemyBullets.count);
    for (const EnemyBullet& bullet : state.enemyBullets) {
        mixVec(bullet.pos);
        mixVec(bullet.vel);
        mix(bullet.damage);
        mix(bullet.radius);
        mix(bullet.life);
    }
    mix(state.zones.count);
    for (const Zone& zone : state.zones) {
        mixVec(zone.a);
        mixVec(zone.b);
        mix(zone.radius);
        mix(zone.prevRadius);  // Read by ring zones to find who the ring just crossed.
        mix(zone.maxRadius);
        mix(zone.life);
        mix(zone.maxLife);
        mix(zone.damage);
        mix(zone.kind);
        mix(zone.source);
        mix(zone.evolved);
        mix(zone.hostile);
        mix(zone.applied);
    }
    mix(state.gems.count);
    for (const Gem& gem : state.gems) {
        mixVec(gem.pos);
        mix(gem.xp);
        mix(gem.attracted);
    }
    mix(state.pickups.count);
    for (const Pickup& pickup : state.pickups) {
        mixVec(pickup.pos);
        mix(pickup.type);
    }

    // Co-op adds more players and a few fields, mixed in last and only for a co-op run, the
    // same way as the Endless fields above. With one player these never change: every zombie
    // chases slot 0, every shot is the first player's, and nobody is ever downed.
    if (state.isCoop()) {
        mix(run.playerCount);
        for (int slot = 0; slot < run.playerCount; ++slot) {
            const Player& player = state.players[slot];
            if (slot > 0) {
                mixPlayer(player);
                mixCards(player);
                mix(player.bladeAngle);
            }
            for (float damage : player.damageByWeapon) mix(damage);
            mix(player.reviveProgress);
            mix(player.cardShield);
            mix(player.downed);
            mix(player.cardsOpen);
            mix(player.offerSerial);
        }
        for (const Enemy& enemy : state.enemies) mix(enemy.target);
        for (const Projectile& shot : state.projectiles) mix(shot.owner);
        for (const Zone& zone : state.zones) mix(zone.owner);
    }
    return hash;
}

void setStressTargets(GameState& state, int enemies, int projectiles) {
    state.run.debugEnemyTarget = enemies;
    state.run.debugProjectileTarget = projectiles;
}

}  // namespace ls::sim
