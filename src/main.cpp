#include "core/App.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

extern "C" {
// Test hooks, called by the browser tests.
const char* ls_query_state() { return ls::app::queryState(); }
void ls_debug_command(int command, int value) { ls::app::debugCommand(command, value); }
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1280, 720, "Last Stand");
    SetExitKey(KEY_NULL);
    ls::app::init();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(ls::app::frame, 0, true);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) ls::app::frame();
    CloseWindow();
#endif
    return 0;
}
