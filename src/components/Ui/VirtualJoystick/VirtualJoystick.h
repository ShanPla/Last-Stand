#pragma once

#include "components/Input/Input.h"

namespace ls::ui {

// Where the touch joystick sits: the lower left corner of the safe area, across from the
// dash button, so one thumb sits on each.
Vec2 joystickHome();

// Draws the stick in its place, faint until a thumb is steering. The movement logic itself
// lives in Input.
void drawVirtualJoystick(const input::Snapshot& snapshot);

}  // namespace ls::ui
