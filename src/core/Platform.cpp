#include "core/Platform.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__

// Browser code. These small functions are compiled into the page's JavaScript.
// Everything the page tells us between frames is remembered in Module.lsInput, and
// the game takes it when the next frame runs.

EM_JS(void, ls_js_install, (), {
    if (Module.lsInput) return;
    var state = Module.lsInput = { touchPending: false, touchX: 0, touchY: 0, lastTouchMs: -100000, focusLost: false, probe: null };
    var canvas = Module.canvas || document.getElementById('canvas');

    // The first finger of the event, converted to drawing buffer pixels.
    window.addEventListener('touchstart', function (event) {
        var touch = event.changedTouches[0];
        state.lastTouchMs = performance.now();
        if (!touch) return;
        var rect = canvas.getBoundingClientRect();
        state.touchX = (touch.clientX - rect.left) * canvas.width / Math.max(1, rect.width);
        state.touchY = (touch.clientY - rect.top) * canvas.height / Math.max(1, rect.height);
        state.touchPending = true;
    }, { capture: true, passive: true });
    window.addEventListener('touchend', function () { state.lastTouchMs = performance.now(); }, { capture: true, passive: true });

    // After a tap, browsers send a made-up mouse click. It would press a second
    // button under the finger, so it is dropped before the game sees it.
    var dropFake = function (event) {
        if (performance.now() - state.lastTouchMs < 800) event.stopImmediatePropagation();
    };
    window.addEventListener('mousedown', dropFake, true);
    window.addEventListener('mouseup', dropFake, true);

    document.addEventListener('visibilitychange', function () { if (document.hidden) state.focusLost = true; });
    window.addEventListener('blur', function () { state.focusLost = true; });
});

EM_JS(int, ls_js_take_touch, (float* out), {
    var state = Module.lsInput;
    if (!state || !state.touchPending) return 0;
    state.touchPending = false;
    HEAPF32[out >> 2] = state.touchX;
    HEAPF32[(out >> 2) + 1] = state.touchY;
    return 1;
});

EM_JS(int, ls_js_take_focus_lost, (), {
    var state = Module.lsInput;
    if (!state || !state.focusLost) return 0;
    state.focusLost = false;
    return 1;
});

// Writes top, right, bottom, left insets in CSS pixels. The probe is an invisible
// box padded by the browser's safe area values, so reading its padding reads them.
EM_JS(void, ls_js_read_insets, (float* out), {
    var state = Module.lsInput;
    var values = [0, 0, 0, 0];
    try {
        if (!state.probe) {
            var probe = document.createElement('div');
            probe.style.cssText = 'position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;' +
                'padding:env(safe-area-inset-top,0px) env(safe-area-inset-right,0px) env(safe-area-inset-bottom,0px) env(safe-area-inset-left,0px)';
            document.body.appendChild(probe);
            state.probe = probe;
        }
        var style = getComputedStyle(state.probe);
        values = [parseFloat(style.paddingTop) || 0, parseFloat(style.paddingRight) || 0,
                  parseFloat(style.paddingBottom) || 0, parseFloat(style.paddingLeft) || 0];
    } catch (error) {}
    for (var i = 0; i < 4; i++) HEAPF32[(out >> 2) + i] = values[i];
});

EM_JS(int, ls_js_prefers_reduced_motion, (), {
    try { return window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 1 : 0; } catch (error) { return 0; }
});

EM_JS(int, ls_js_touch_is_only_pointer, (), {
    try {
        return window.matchMedia('(any-pointer: coarse)').matches && !window.matchMedia('(any-pointer: fine)').matches ? 1 : 0;
    } catch (error) {
        return 0;
    }
});

// Returns the text length, or a negative number when there is nothing usable.
// Storage can be blocked by browser settings, so every access is guarded.
EM_JS(int, ls_js_storage_read, (char* buffer, int capacity), {
    try {
        var text = window.localStorage.getItem('laststand.save');
        if (text === null) return -1;
        if (text.length + 1 > capacity) return -2;
        for (var i = 0; i < text.length; i++) HEAPU8[buffer + i] = text.charCodeAt(i) & 255;
        HEAPU8[buffer + text.length] = 0;
        return text.length;
    } catch (error) {
        return -3;
    }
});

EM_JS(int, ls_js_storage_write, (const char* text), {
    try {
        var out = String();
        for (var i = 0; HEAPU8[text + i] !== 0; i++) out += String.fromCharCode(HEAPU8[text + i]);
        window.localStorage.setItem('laststand.save', out);
        return 1;
    } catch (error) {
        return 0;
    }
});

#endif

namespace ls::platform {

namespace {

float g_insetOverride = -1.0f;
bool g_insetDirty = false;  // Set when the override changes, so the next read is fresh.

#ifdef __EMSCRIPTEN__
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

void ensureInstalled() {
    static const bool installed = [] {
        ls_js_install();
        return true;
    }();
    (void)installed;
}
#endif

}  // namespace

bool consumeMouseDown(float* x, float* y) {
#ifdef __EMSCRIPTEN__
    ensureInstalled();
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

bool consumeTouchStart(float* x, float* y) {
#ifdef __EMSCRIPTEN__
    ensureInstalled();
    float out[2] = {0.0f, 0.0f};
    if (!ls_js_take_touch(out)) return false;
    *x = out[0];
    *y = out[1];
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
}

bool consumeFocusLost() {
#ifdef __EMSCRIPTEN__
    ensureInstalled();
    return ls_js_take_focus_lost() != 0;
#else
    static bool hadFocus = true;
    const bool hasFocus = IsWindowFocused() && !IsWindowHidden() && !IsWindowMinimized();
    const bool lost = hadFocus && !hasFocus;
    hadFocus = hasFocus;
    return lost;
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

SafeInsets safeInsets() {
    SafeInsets insets;
#ifdef __EMSCRIPTEN__
    ensureInstalled();
    // Reading computed style forces the browser to lay out the page, so it is done
    // twice a second, which still follows a phone turning around quickly.
    static SafeInsets cached;
    static int framesLeft = 0;
    static int lastWidth = 0;
    static int lastHeight = 0;
    if (framesLeft <= 0 || g_insetDirty || GetScreenWidth() != lastWidth || GetScreenHeight() != lastHeight) {
        float css[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // Top, right, bottom, left.
        ls_js_read_insets(css);
        if (g_insetOverride >= 0.0f) css[0] = css[1] = css[2] = css[3] = g_insetOverride;
        double cssWidth = 1.0;
        double cssHeight = 1.0;
        emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
        const float scaleX = static_cast<float>(GetScreenWidth() / std::max(1.0, cssWidth));
        const float scaleY = static_cast<float>(GetScreenHeight() / std::max(1.0, cssHeight));
        cached.top = css[0] * scaleY;
        cached.right = css[1] * scaleX;
        cached.bottom = css[2] * scaleY;
        cached.left = css[3] * scaleX;
        framesLeft = 30;
        g_insetDirty = false;
        lastWidth = GetScreenWidth();
        lastHeight = GetScreenHeight();
    }
    framesLeft--;
    insets = cached;
#else
    if (g_insetOverride >= 0.0f) insets.left = insets.top = insets.right = insets.bottom = g_insetOverride;
#endif
    return insets;
}

void setSafeInsetOverride(float cssPixels) {
    g_insetOverride = cssPixels;
    g_insetDirty = true;
}

float pixelRatio() {
#ifdef __EMSCRIPTEN__
    return static_cast<float>(std::min(emscripten_get_device_pixel_ratio(), 2.0));
#else
    return 1.0f;
#endif
}

bool prefersReducedMotion() {
#ifdef __EMSCRIPTEN__
    return ls_js_prefers_reduced_motion() != 0;
#else
    return false;
#endif
}

bool touchIsOnlyPointer() {
#ifdef __EMSCRIPTEN__
    return ls_js_touch_is_only_pointer() != 0;
#else
    return false;
#endif
}

#ifndef __EMSCRIPTEN__
namespace {

// The native save is a small file next to the program.
std::FILE* openSaveFile(const char* mode) {
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return fopen_s(&file, "laststand.save", mode) == 0 ? file : nullptr;
#else
    return std::fopen("laststand.save", mode);
#endif
}

}  // namespace
#endif

bool readSave(std::string* text) {
#ifdef __EMSCRIPTEN__
    static char buffer[8192];
    const int length = ls_js_storage_read(buffer, static_cast<int>(sizeof(buffer)));
    if (length < 0) {
        // A value too big for the buffer is not ours. Hand over a marker that parses to defaults.
        text->clear();
        return false;
    }
    text->assign(buffer, static_cast<size_t>(length));
    return true;
#else
    std::FILE* file = openSaveFile("rb");
    if (!file) return false;
    char buffer[8192];
    const size_t length = std::fread(buffer, 1, sizeof(buffer), file);
    std::fclose(file);
    text->assign(buffer, length);
    return true;
#endif
}

bool writeSave(const std::string& text) {
#ifdef __EMSCRIPTEN__
    return ls_js_storage_write(text.c_str()) != 0;
#else
    std::FILE* file = openSaveFile("wb");
    if (!file) return false;
    const size_t written = std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    return written == text.size();
#endif
}

}  // namespace ls::platform
