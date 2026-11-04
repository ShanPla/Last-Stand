#pragma once

#include "core/Save.h"

namespace ls::ui {

enum class TitleAction { None, Play, Settings, HowToPlay };

// Draws the title menu over the running demo, and reads its input. Play is selected
// first, so Enter, Space, gamepad A or a click on Play starts at once. Tapping
// anywhere else does nothing.
TitleAction drawTitleScreen(const BestRun& best);

// Puts the selection back on Play. Call when the title opens.
void resetTitleScreen();

}  // namespace ls::ui
