#pragma once

#include "components/Input/Input.h"

namespace ls::ui {

// Draws the floating touch joystick where the thumb landed. Does nothing when no
// thumb is steering. The movement logic itself lives in Input.
void drawVirtualJoystick(const input::Snapshot& snapshot);

}  // namespace ls::ui
