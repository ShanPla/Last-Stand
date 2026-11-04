#pragma once

#include "core/GameState.h"

namespace ls::ui {

enum class PauseAction { None, Resume, Settings, Restart, Quit };

// Draws the pause menu with the current build and exact ammo counts. Resume is
// selected first. Esc, P and gamepad B or Start are handled by the caller.
PauseAction drawPauseMenu(const GameState& state);

// Puts the selection back on Resume. Call when the menu opens.
void resetPauseMenu();

}  // namespace ls::ui
