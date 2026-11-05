#pragma once

#include "core/GameState.h"

#include <cstdio>
#include <string>

namespace ls::tools {

// Each weapon alone, at each level and evolved, against a standing crowd.
// Prints damage per second, kills per minute and how long one full load lasts.
// Zombie HP follows the given difficulty, as in a real game.
// Writes a CSV into the folder when the folder is not empty.
void runSoloTest(std::FILE* out, int threadCount, const std::string& csvFolder, Difficulty difficulty);

}  // namespace ls::tools
