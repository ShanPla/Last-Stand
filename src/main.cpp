#include "core/App.h"
#include "components/Ui/Icons/Icons.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

extern "C" {
// Test hooks, called by the browser tests.
const char* ls_query_state() { return ls::app::queryState(); }
void ls_debug_command(int command, int value) { ls::app::debugCommand(command, value); }
}

#ifndef __EMSCRIPTEN__
// The window and taskbar icon is the title screen's zombie head, drawn into a small
// image here. That way the program needs no icon file and no resource file.
static void setWindowIconFromCode() {
    constexpr int kSize = 64;
    const RenderTexture2D target = LoadRenderTexture(kSize, kSize);
    BeginTextureMode(target);
    ClearBackground(BLANK);
    ls::ui::drawZombieIcon(kSize / 2.0f, kSize / 2.0f, 60.0f, 1.0f);
    EndTextureMode();
    Image icon = LoadImageFromTexture(target.texture);
    ImageFlipVertical(&icon);  // Render textures are stored upside down.
    SetWindowIcon(icon);
    UnloadImage(icon);
    UnloadRenderTexture(target);
}
#endif

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1280, 720, "Last Stand");
#ifndef __EMSCRIPTEN__
    setWindowIconFromCode();
#endif
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
