#pragma once

#include "components/Input/Input.h"
#include "core/GameState.h"

namespace ls::ui {

// Health, XP, level, timer, kills, the held build, the boss bar, a pause button
// and a warning when health is low. All of it stays on the screen edges.
// hintAlpha (0 to 1) fades the controls hint, which the caller lowers after the
// player starts moving. Returns true on the frame the pause button is pressed.
bool drawHud(const GameState& state, float hintAlpha, bool pauseEnabled);

// Where a finger would press the pause button, for the touch joystick to avoid.
input::Rect pauseButtonRect();

}  // namespace ls::ui
