#include "core/Platform.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>

namespace ls::platform {

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
    // Mouse events arrive in CSS pixels, but the game works in buffer pixels.
    SetMouseScale(static_cast<float>(ratio), static_cast<float>(ratio));
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
