#pragma once

#include "core/GameState.h"

namespace ls::ui {

enum class PauseAction { None, Resume, Settings, Restart, Quit };

// Draws the pause menu with the current build and exact ammo counts. Resume is
// selected first. Esc, P and gamepad B or Start are handled by the caller.
// Solo and the co-op host pause the run. A co-op guest's menu does not, and has no Restart.
enum class PauseKind { Solo, CoopHost, CoopGuest };
PauseAction drawPauseMenu(const GameState& state, PauseKind kind = PauseKind::Solo);

// Puts the selection back on Resume. Call when the menu opens.
void resetPauseMenu();

}  // namespace ls::ui
