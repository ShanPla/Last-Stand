#pragma once

#include "components/Input/Input.h"
#include "core/GameState.h"

namespace ls::ui {

struct LevelUpChoice {
    int card = -1;
    bool reroll = false;
};

// Draws the level-up screen and reads its input. Call once per frame while the
// phase is LevelUp. A short delay after the cards appear ignores presses, so a held
// key or a fast tap cannot choose by accident.
LevelUpChoice drawLevelUpCards(const GameState& state, float dtReal);

// Call when the level-up screen closes, so the next one starts with its delay.
void resetLevelUpCards();

// For the browser tests: what the co-op shield line on the cards says right now ("Shielded 4s"
// or "Shield gone! Pick fast"), or an empty string when it isn't showing.
const char* levelUpShieldText();

}  // namespace ls::ui
