#pragma once

#include "components/Input/Input.h"
#include "core/GameState.h"

namespace ls::ui {

// Health, XP, level, timer, kills, the held build, the boss bar, a pause button
// and a warning when health is low. All of it stays on the screen edges.
// hintAlpha (0 to 1) fades the controls hint, which the caller lowers after the
// player starts moving. Presses are only reported while playing is true.
struct HudPresses {
    bool pause = false;
    bool dash = false;  // The dash button was clicked or tapped.
};
HudPresses drawHud(const GameState& state, float hintAlpha, bool playing);

// Where a finger would press the pause button, for the touch joystick to avoid.
input::Rect pauseButtonRect();
// The same for the dash button. It is the big corner button for touch, and the small
// indicator under the weapon slots for every other device.
input::Rect dashButtonRect();

}  // namespace ls::ui
