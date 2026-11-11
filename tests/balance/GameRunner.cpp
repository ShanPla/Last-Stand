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
RunMode g_mode = RunMode::Story;
int g_players = 1;

constexpr float kCarHugDistance = 60.0f;

// Adds up time spent held, dry and fully dry for this one step.
void trackWeapons(const GameState& state, RunResult& result) {
    const Player& player = state.firstPlayer();
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
        const balance::WeaponLevel stats = weapons::effectiveStats(state.firstPlayer(), slot);
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
    state.enemyGrid.query(state.firstPlayer().pos, 24.0f + balance::kLargestEnemyRadius, [&](int index) {
        if (index >= state.enemies.count) return false;
        const Enemy& enemy = state.enemies[index];
        const float gap = distance(enemy.pos, state.firstPlayer().pos) - enemy.radius - balance::kPlayerRadius;
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
        // Numbers about one player are the first player's. In co-op every bot plays the same
        // profile, so the first stands for all of them.
        const bool mine = event.player == kNoPlayer || event.player == 0;
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
            case EventType::BossKilled:
                result.bossKilled = true;
                ++result.bossesKilled;
                break;
            case EventType::PlayerDashed: ++result.dashes; break;
            case EventType::ZombieUnstuck: ++result.zombiesUnstuck; break;
            case EventType::CarCrushed: ++result.carsCrushed; break;
            case EventType::PlayerHit:
                if (!mine) break;
                result.damageTaken += event.value;
                result.damageBySource[hitSource] += event.value;
                break;
            case EventType::PickupCollected:
                if (mine && event.detail == static_cast<uint8_t>(PickupType::Ammo)) ++result.cratesCollected;
                break;
            case EventType::EvolutionGained:
                if (mine && result.firstEvolutionSeconds < 0.0f) result.firstEvolutionSeconds = state.timeSeconds();
                break;
            case EventType::PlayerDowned: ++result.downs; break;
            case EventType::PlayerRevived: ++result.revives; break;
            default: break;
        }
    }
}

// One line of the --trace output: where the player is and what is around it.
void traceLine(std::FILE* out, const GameState& state) {
    const Vec2 pos = state.firstPlayer().pos;
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
    for (int i = 0; i < state.firstPlayer().weaponCount; ++i) {
        const WeaponSlot& slot = state.firstPlayer().weapons[i];
        loadout += format(" %s%d%s", weaponName(static_cast<int>(slot.id)), slot.level, slot.evolved ? "*" : "");
    }
    std::fprintf(out, "%6.1fs hp %5.1f lv %2d zombies %4d | within 100: %3d, 250: %3d | gap %6.1f (%s) | pos %6.0f %6.0f | shots %d |%s\n",
                 static_cast<double>(state.timeSeconds()), static_cast<double>(state.firstPlayer().hp), state.firstPlayer().level, state.enemies.count,
                 close, near, static_cast<double>(nearestGap), nearestType >= 0 ? damageSourceName(nearestType) : "-",
                 static_cast<double>(pos.x), static_cast<double>(pos.y), state.enemyBullets.count, loadout.c_str());
}

}  // namespace

void setGameDifficulty(Difficulty difficulty) { g_difficulty = difficulty; }
void setGameMap(maps::MapId map) { g_map = map; }
maps::MapId gameMap() { return g_map; }
void setGameMode(RunMode mode) { g_mode = mode; }
void setGamePlayers(int players) { g_players = players < 1 ? 1 : (players > kMaxPlayers ? kMaxPlayers : players); }
int gamePlayers() { return g_players; }

RunResult playGame(const BotProfile& profile, uint64_t seed, int favoriteWeapon, std::FILE* trace) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, g_difficulty, g_map, g_mode, g_players);
    const int stepLimit = g_mode == RunMode::Endless ? kEndlessStepLimit : kStepLimit;
    // A focus run starts with the favorite next to the Rifle, so every weapon gets the same head start.
    if (favoriteWeapon >= 0) {
        for (int slot = 0; slot < g_players; ++slot) weapons::grant(*state, slot, static_cast<WeaponId>(favoriteWeapon));
    }
    // One bot per player, all with the same profile. The first bot's seed is the one a
    // single-player game has always used, so solo results stay the same.
    std::vector<Bot> bots;
    for (int slot = 0; slot < g_players; ++slot) {
        bots.emplace_back(profile, slot == 0 ? seed : seed * 1000003u + static_cast<uint64_t>(slot), favoriteWeapon, slot);
    }
    RunResult result;
    result.seed = seed;
    result.favoriteWeapon = favoriteWeapon;
    result.players = g_players;

    const int sampleTicks = static_cast<int>(kSampleSeconds / kDt + 0.5f);
    const int binTicks = static_cast<int>(kBinSeconds / kDt + 0.5f);
    int previousRerolls = state->firstPlayer().rerollsLeft;

    while (state->run.phase != RunPhase::Won && state->run.phase != RunPhase::Dead &&
           static_cast<int>(state->run.tick) < stepLimit) {
        const bool wasPlaying = state->run.phase == RunPhase::Playing;
        const int killsBefore = result.kills;
        const float damageBefore = result.damageTaken;
        const int hitSource = wasPlaying ? likelyHitSource(*state) : kDamageSources - 1;
        InputFrame inputs[kMaxPlayers];
        for (int slot = 0; slot < g_players; ++slot) inputs[slot] = bots[static_cast<size_t>(slot)].act(*state);
        sim::step(*state, inputs, g_players);
        readEvents(*state, result, hitSource);
        result.kills = state->run.kills;
        state->events.clear();

        if (state->firstPlayer().rerollsLeft < previousRerolls) result.rerollsUsed += previousRerolls - state->firstPlayer().rerollsLeft;
        previousRerolls = state->firstPlayer().rerollsLeft;

        // A step spent on the level-up screen does not advance game time.
        if (!wasPlaying) continue;

        const int tick = static_cast<int>(state->run.tick);
        result.playedSeconds += kDt;
        result.peakZombies = std::max(result.peakZombies, state->enemies.count);
        trackWeapons(*state, result);
        if (obstacles::distanceToNearestCar(*state, state->firstPlayer().pos, kCarHugDistance) < kCarHugDistance) result.carHugSeconds += kDt;
        if (result.bossReached) {
            result.bossDamageTaken += result.damageTaken - damageBefore;
            result.bossLowestHp = std::min(result.bossLowestHp, std::max(0.0f, state->firstPlayer().hp));
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
            result.sampleHp.push_back(std::max(0.0f, state->firstPlayer().hp));
            result.sampleLevel.push_back(static_cast<float>(state->firstPlayer().level));
        }
    }

    result.outcome = state->run.phase;
    result.endSeconds = state->timeSeconds();
    result.level = state->firstPlayer().level;
    result.kills = state->run.kills;
    for (int i = 0; i < kWeaponCount; ++i) result.damage[i] = state->run.damageByWeapon[i];
    if (result.bossReached) {
        result.bossFightSeconds = result.endSeconds - result.bossSpawnSeconds;
        result.bossHpLeft = state->run.bossHpFraction;
    }
    return result;
}

}  // namespace ls::tools
