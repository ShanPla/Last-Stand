#include "core/Platform.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>

namespace ls::platform {

#ifdef __EMSCRIPTEN__
namespace {

bool g_mouseDown = false;
float g_mouseX = 0.0f;
float g_mouseY = 0.0f;

EM_BOOL onMouseDown(int, const EmscriptenMouseEvent* event, void*) {
    if (event->button != 0) return EM_FALSE;
    double cssWidth = 1.0;
    double cssHeight = 1.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    g_mouseDown = true;
    g_mouseX = static_cast<float>(event->targetX * GetScreenWidth() / cssWidth);
    g_mouseY = static_cast<float>(event->targetY * GetScreenHeight() / cssHeight);
    return EM_FALSE;  // Not consumed, so the normal input handling still sees it.
}

}  // namespace
#endif

bool consumeMouseDown(float* x, float* y) {
#ifdef __EMSCRIPTEN__
    static const bool registered = [] {
        emscripten_set_mousedown_callback("#canvas", nullptr, EM_FALSE, onMouseDown);
        return true;
    }();
    (void)registered;
    if (!g_mouseDown) return false;
    g_mouseDown = false;
    *x = g_mouseX;
    *y = g_mouseY;
    return true;
#else
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;
    const Vector2 mouse = GetMousePosition();
    *x = mouse.x;
    *y = mouse.y;
    return true;
#endif
}

void syncCanvasSize(float renderScale) {
#ifdef __EMSCRIPTEN__
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    // A pixel ratio above 2 costs a lot of fill rate for almost no visible gain.
    const double ratio = std::min(emscripten_get_device_pixel_ratio(), 2.0) * renderScale;
    const int width = std::max(1, static_cast<int>(std::lround(cssWidth * ratio)));
    const int height = std::max(1, static_cast<int>(std::lround(cssHeight * ratio)));
    if (width != GetScreenWidth() || height != GetScreenHeight()) {
        SetWindowSize(width, height);
    }
    // No mouse scaling here: the browser layer already reports the mouse in drawing
    // buffer pixels, so scaling again would make clicks miss at high pixel ratios.
#else
    (void)renderScale;
#endif
}

bool debugEnabled() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({ return new URLSearchParams(location.search).has('debug') ? 1 : 0; }) != 0;
#else
    return true;
#endif
}

}  // namespace ls::platform
