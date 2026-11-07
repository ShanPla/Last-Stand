#pragma once

#include "core/Save.h"

namespace ls::ui {

enum class TitleAction { None, Play, Settings, HowToPlay };

// Draws the title menu over the running demo, and reads its input. Play is selected
// first, so Enter, Space, gamepad A or a click on Play starts at once, with the level
// the selectors show. Tapping anywhere else does nothing. The selectors change
// data.difficulty, data.map and data.mode themselves, so the caller can see the change
// and save it. The best run shown is the one for that level and mode.
TitleAction drawTitleScreen(SaveData& data);

// Puts the selection back on Play. Call when the title opens.
void resetTitleScreen();

}  // namespace ls::ui
