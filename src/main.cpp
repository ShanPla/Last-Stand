#include "core/Platform.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace {

void frame() {
    ls::platform::syncCanvasSize(1.0f);
    BeginDrawing();
    ClearBackground(Color{5, 6, 15, 255});
    DrawCircle(GetScreenWidth() / 2, GetScreenHeight() / 2, 40.0f, Color{95, 244, 255, 255});
    EndDrawing();
}

}  // namespace

extern "C" {
// Test hooks, read by the browser tests.
const char* ls_query_state() { return "{\"screen\":\"blank\"}"; }
void ls_debug_command(int, int) {}
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1280, 720, "Last Stand");
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, true);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) frame();
    CloseWindow();
#endif
    return 0;
}
