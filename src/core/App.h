#pragma once

// The game loop: runs the fixed-step simulation, and draws between steps.
namespace ls::app {

void init();
void frame();

// A JSON summary of the game, for the browser tests.
const char* queryState();

// A JSON summary of the screens and button positions, for the browser tests.
const char* queryUi();

// Debug commands from the browser tests. See DebugCommand in App.cpp.
void debugCommand(int command, int value);

}  // namespace ls::app
