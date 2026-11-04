#pragma once

#include "core/Save.h"

namespace ls::ui {

struct SettingsResult {
    bool changed = false;  // A value changed this frame, so apply and save it.
    bool back = false;     // The Back button was pressed.
};

// Draws the settings list and edits settings in place. Keyboard: up and down pick a
// row, left and right change it. Gamepad the same. Mouse and touch drag or tap.
// backLabel says where Back leads, for example "Back" or "Back to pause".
SettingsResult drawSettingsScreen(Settings& settings, const char* backLabel);

void resetSettingsScreen();

}  // namespace ls::ui
