#pragma once

#include "core/GameState.h"

#include <cstdint>

namespace ls {

// One step of one player's input. A saved list of these, plus the seed, replays a run.
struct InputFrame {
    float moveX = 0.0f;
    float moveY = 0.0f;
    int8_t chooseCard = -1;  // Card index picked on the level-up screen, or -1.
    bool reroll = false;
    bool dash = false;       // A dash was asked for this step. Ignored during cooldown.
};

namespace sim {

// Starts a run for 1 to kMaxPlayers players. The host's choices of map, difficulty and mode
// hold for the whole run and every player in it.
void newRun(GameState& state, uint64_t seed, Difficulty difficulty = Difficulty::Normal, maps::MapId map = maps::kDefaultMap,
            RunMode mode = RunMode::Story, int playerCount = 1);

// Changes the map under a run in progress, as the title screen's picker does behind its demo.
void setMap(GameState& state, maps::MapId map);

// Advances the run by one fixed step, with one input per player: inputs[slot] belongs to the
// player in that slot. A player with no input given, because inputCount is smaller than the
// number of players, stands still that step. Same state and inputs give the same result.
void step(GameState& state, const InputFrame inputs[], int inputCount);

// A single-player step: the one input is the first player's.
inline void step(GameState& state, const InputFrame& input) { step(state, &input, 1); }

// A fingerprint of the run, for checking that two runs match exactly.
uint64_t stateHash(const GameState& state);

// Debug stress test: keeps the pools topped up to these counts each step.
void setStressTargets(GameState& state, int enemies, int projectiles);

}  // namespace sim
}  // namespace ls
