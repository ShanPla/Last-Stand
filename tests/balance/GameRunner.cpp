#include "GameRunner.h"

#include "Stats.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Weapons/Weapons.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace ls::tools {

namespace {

Difficulty g_difficulty = Difficulty::Normal;
maps::MapId g_map = maps::kDefaultMap;

constexpr float kCarHugDistance = 60.0f;

// Adds up time spent held, dry and fully dry for this one step.
void trackWeapons(const GameState& state, RunResult& result) {
    const Player& player = state.player;
    int ammoGuns = 0;
    int dryGuns = 0;
    int ammoFreeWeapons = 0;
    for (int i = 0; i < player.weaponCount; ++i) {
        const WeaponSlot& slot = player.weapons[i];
        const int id = static_cast<int>(slot.id);
        result.everHeld[id] = true;
        result.heldSeconds[id] += kDt;
        result.finalLevel[id] = slot.level;
        result.evolved[id] = slot.evolved;
        const balance::WeaponLevel stats = weapons::effectiveStats(state, slot);
        if (stats.magazine <= 0) {
            ++ammoFreeWeapons;
            continue;
        }
        ++ammoGuns;
        if (slot.magazine <= 0 && slot.reserve <= 0) {
            ++dryGuns;
            result.drySeconds[id] += kDt;
        }
    }
    if (ammoGuns > 0 && dryGuns == ammoGuns) {
        result.allAmmoGunsDrySeconds += kDt;
        if (ammoFreeWeapons == 0) result.fullyDrySeconds += kDt;
    }
}

// Guesses who would hurt the player in the coming step: the enemy closest to touching it,
// or else a shot or boss zone. It looks before the step, because a zombie that hits and
// then dies in the same step is gone afterwards.
int likelyHitSource(const GameState& state) {
    int source = kDamageSources - 1;
    float closest = 24.0f;  // Enough for the fastest enemy to close in during one step.
    state.enemyGrid.query(state.player.pos, 24.0f + balance::kLargestEnemyRadius, [&](int index) {
        if (index >= state.enemies.count) return false;
        const Enemy& enemy = state.enemies[index];
        const float gap = distance(enemy.pos, state.player.pos) - enemy.radius - balance::kPlayerRadius;
        if (gap < closest) {
            closest = gap;
            source = static_cast<int>(enemy.type);
        }
        return false;
    });
    return source;
}

void readEvents(const GameState& state, RunResult& result, int hitSource) {
    for (const SimEvent& event : state.events) {
        switch (event.type) {
            case EventType::BossSpawned:
                if (!result.bossReached) {
                    result.bossReached = true;
                    result.bossSpawnSeconds = state.timeSeconds();
                    for (const Enemy& enemy : state.enemies) {
                        if (enemy.type == EnemyType::Boss) result.bossMaxHp = enemy.maxHp;
                    }
                }
                break;
            case EventType::BossKilled: result.bossKilled = true; break;
            case EventType::PlayerDashed: ++result.dashes; break;
            case EventType::ZombieUnstuck: ++result.zombiesUnstuck; break;
            case EventType::PlayerHit:
                result.damageTaken += event.value;
                result.damageBySource[hitSource] += event.value;
                break;
            case EventType::PickupCollected:
                if (event.detail == static_cast<uint8_t>(PickupType::Ammo)) ++result.cratesCollected;
                break;
            case EventType::EvolutionGained:
                if (result.firstEvolutionSeconds < 0.0f) result.firstEvolutionSeconds = state.timeSeconds();
                break;
            default: break;
        }
    }
}

// One line of the --trace output: where the player is and what is around it.
void traceLine(std::FILE* out, const GameState& state) {
    const Vec2 pos = state.player.pos;
    int close = 0;
    int near = 0;
    float nearestGap = 9999.0f;
    int nearestType = -1;
    for (const Enemy& enemy : state.enemies) {
        const float gap = distance(enemy.pos, pos) - enemy.radius - balance::kPlayerRadius;
        if (gap < 100.0f) ++close;
        if (gap < 250.0f) ++near;
        if (gap < nearestGap) {
            nearestGap = gap;
            nearestType = static_cast<int>(enemy.type);
        }
    }
    std::string loadout;
    for (int i = 0; i < state.player.weaponCount; ++i) {
        const WeaponSlot& slot = state.player.weapons[i];
        loadout += format(" %s%d%s", weaponName(static_cast<int>(slot.id)), slot.level, slot.evolved ? "*" : "");
    }
    std::fprintf(out, "%6.1fs hp %5.1f lv %2d zombies %4d | within 100: %3d, 250: %3d | gap %6.1f (%s) | pos %6.0f %6.0f | shots %d |%s\n",
                 static_cast<double>(state.timeSeconds()), static_cast<double>(state.player.hp), state.player.level, state.enemies.count,
                 close, near, static_cast<double>(nearestGap), nearestType >= 0 ? damageSourceName(nearestType) : "-",
                 static_cast<double>(pos.x), static_cast<double>(pos.y), state.enemyBullets.count, loadout.c_str());
}

}  // namespace

void setGameDifficulty(Difficulty difficulty) { g_difficulty = difficulty; }
void setGameMap(maps::MapId map) { g_map = map; }
maps::MapId gameMap() { return g_map; }

RunResult playGame(const BotProfile& profile, uint64_t seed, int favoriteWeapon, std::FILE* trace) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, g_difficulty, g_map);
    // A focus run starts with the favorite next to the Rifle, so every weapon gets the same head start.
    if (favoriteWeapon >= 0) weapons::grant(*state, static_cast<WeaponId>(favoriteWeapon));
    Bot bot(profile, seed, favoriteWeapon);
    RunResult result;
    result.seed = seed;
    result.favoriteWeapon = favoriteWeapon;

    const int sampleTicks = static_cast<int>(kSampleSeconds / kDt + 0.5f);
    const int binTicks = static_cast<int>(kBinSeconds / kDt + 0.5f);
    int previousRerolls = state->run.rerollsLeft;

    while (state->run.phase != RunPhase::Won && state->run.phase != RunPhase::Dead &&
           static_cast<int>(state->run.tick) < kStepLimit) {
        const bool wasPlaying = state->run.phase == RunPhase::Playing;
        const int killsBefore = result.kills;
        const float damageBefore = result.damageTaken;
        const int hitSource = wasPlaying ? likelyHitSource(*state) : kDamageSources - 1;
        sim::step(*state, bot.act(*state));
        readEvents(*state, result, hitSource);
        result.kills = state->run.kills;
        state->events.clear();

        if (state->run.rerollsLeft < previousRerolls) result.rerollsUsed += previousRerolls - state->run.rerollsLeft;
        previousRerolls = state->run.rerollsLeft;

        // A step spent on the level-up screen does not advance game time.
        if (!wasPlaying) continue;

        const int tick = static_cast<int>(state->run.tick);
        result.playedSeconds += kDt;
        result.peakZombies = std::max(result.peakZombies, state->enemies.count);
        trackWeapons(*state, result);
        if (obstacles::distanceToNearestCar(*state, state->player.pos, kCarHugDistance) < kCarHugDistance) result.carHugSeconds += kDt;
        if (result.bossReached) {
            result.bossDamageTaken += result.damageTaken - damageBefore;
            result.bossLowestHp = std::min(result.bossLowestHp, std::max(0.0f, state->player.hp));
        }

        const size_t bin = static_cast<size_t>((tick - 1) / binTicks);
        if (result.bins.size() <= bin) result.bins.resize(bin + 1);
        PressureBin& pressure = result.bins[bin];
        pressure.zombieSum += static_cast<float>(state->enemies.count);
        pressure.steps++;
        pressure.kills += result.kills - killsBefore;
        pressure.damageTaken += result.damageTaken - damageBefore;

        if (trace && tick % 30 == 0) traceLine(trace, *state);
        if (tick % sampleTicks == 0) {
            result.sampleZombies.push_back(static_cast<float>(state->enemies.count));
            result.sampleHp.push_back(std::max(0.0f, state->player.hp));
            result.sampleLevel.push_back(static_cast<float>(state->player.level));
        }
    }

    result.outcome = state->run.phase;
    result.endSeconds = state->timeSeconds();
    result.level = state->player.level;
    result.kills = state->run.kills;
    for (int i = 0; i < kWeaponCount; ++i) result.damage[i] = state->run.damageByWeapon[i];
    if (result.bossReached) {
        result.bossFightSeconds = result.endSeconds - result.bossSpawnSeconds;
        result.bossHpLeft = state->run.bossHpFraction;
    }
    return result;
}

}  // namespace ls::tools
