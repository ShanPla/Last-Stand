#pragma once

#include "components/Input/Input.h"
#include "core/GameState.h"

namespace ls::ui {

struct LevelUpChoice {
    int card = -1;
    bool reroll = false;
};

// Call each frame while the level-up screen is open. Clears the input delay
// when the screen closes.
LevelUpChoice updateLevelUpCards(const GameState& state, const input::Snapshot& snapshot, float dtReal);

void drawLevelUpCards(const GameState& state);

// Call when the level-up screen closes, so the next one starts with its delay.
void resetLevelUpCards();

}  // namespace ls::ui
