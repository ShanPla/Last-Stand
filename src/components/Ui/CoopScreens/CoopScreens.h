#pragma once

#include "core/Save.h"

namespace ls::ui {

// The co-op screens: a menu (Host, Join, name), the host screen (room code, player list, Ready,
// Start, Leave), and the join screen (room code entry). They are one component because they share
// the same text field, the same notices and the same waiting views, and they only ever follow one
// another. A joined player sees the host screen too, as the lobby, with Start left out.
//
// The screens only talk to the lobby through components/Net/Net.h, so they work the same on the
// fake backend and on the real one.

struct CoopResult {
    bool back = false;          // The player left the co-op menu. Return to the title.
    bool saveChanged = false;   // The saved name changed, so the caller should save.
};

// Opens at the co-op menu. Call when the player picks Co-op on the title.
void openCoopScreens(const SaveData& data);

// Draws the current co-op screen over the demo and reads its input.
CoopResult drawCoopScreens(SaveData& data);

// Which co-op screen is showing, for the tests: "coop-menu", "coop-host" or "coop-join".
const char* coopScreenName();

// What the co-op screens and the lobby look like right now, as JSON, for the browser tests.
const char* coopDebugJson();

}  // namespace ls::ui
