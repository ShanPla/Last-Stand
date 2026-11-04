#pragma once

#include "Bot.h"
#include "RunResult.h"

#include <cstdio>

namespace ls::tools {

// The game ends by itself after the boss, but a stuck run must not loop forever.
inline constexpr int kStepLimit = 20 * 60 * 60;  // 20 minutes of game time.

// Plays one whole game with a bot. The game state lives on the heap, once.
// A favorite weapon (index, or -1 for none) is given at the start and built toward.
// When trace is not null, a line about the player is written to it every half second.
RunResult playGame(const BotProfile& profile, uint64_t seed, int favoriteWeapon = -1, std::FILE* trace = nullptr);

}  // namespace ls::tools
