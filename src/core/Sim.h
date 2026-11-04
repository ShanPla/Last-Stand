#pragma once

#include "core/GameState.h"

#include <cstdint>

namespace ls {

// One step of player input. A saved list of these, plus the seed, replays a run.
struct InputFrame {
    float moveX = 0.0f;
    float moveY = 0.0f;
    int8_t chooseCard = -1;  // Card index picked on the level-up screen, or -1.
    bool reroll = false;
};

namespace sim {

void newRun(GameState& state, uint64_t seed);

// Advances the run by one fixed step. Same state and input give the same result.
void step(GameState& state, const InputFrame& input);

// A fingerprint of the run, for checking that two runs match exactly.
uint64_t stateHash(const GameState& state);

// Debug stress test: keeps the pools topped up to these counts each step.
void setStressTargets(GameState& state, int enemies, int projectiles);

}  // namespace sim
}  // namespace ls
