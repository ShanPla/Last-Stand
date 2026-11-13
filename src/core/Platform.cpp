#include "core/Platform.h"

#include "core/DebugSwitch.h"

#include "raylib.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#else
#include "core/NativePaths.h"
#include "core/SaveFile.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif

#ifdef __EMSCRIPTEN__

// Browser code. These small functions are compiled into the page's JavaScript.
// Everything the page tells us between frames is remembered in Module.lsInput, and
// the game takes it when the next frame runs.

EM_JS(void, ls_js_install, (), {
    if (Module.lsInput) return;
    var state = Module.lsInput = { touches: [], lastTouchMs: -100000, focusLost: false, probe: null, fingers: 0 };
    var canvas = Module.canvas || document.getElementById('canvas');

    // The first finger of the event, converted to drawing buffer pixels.
    window.addEventListener('touchstart', function (event) {
        var touch = event.changedTouches[0];
        state.lastTouchMs = performance.now();
        state.fingers = event.touches.length;
        if (!touch) return;
        var rect = canvas.getBoundingClientRect();
        // A short queue, so that two quick taps between frames each count.
        if (state.touches.length < 8) {
            state.touches.push((touch.clientX - rect.left) * canvas.width / Math.max(1, rect.width));
            state.touches.push((touch.clientY - rect.top) * canvas.height / Math.max(1, rect.height));
        }
    }, { capture: true, passive: true });
    // touchcancel is what the browser sends when the system takes a touch away (an edge
    // swipe, a call). It has no touchend, so the finger count is kept up to date here.
    var fingersChanged = function (event) {
        state.lastTouchMs = performance.now();
        state.fingers = event.touches.length;
    };
    window.addEventListener('touchend', fingersChanged, { capture: true, passive: true });
    window.addEventListener('touchcancel', fingersChanged, { capture: true, passive: true });

    // After a tap, browsers send a made-up mouse click. It would press a second
    // button under the finger, so it is dropped before the game sees it.
    var dropFake = function (event) {
        if (performance.now() - state.lastTouchMs < 800) event.stopImmediatePropagation();
    };
    window.addEventListener('mousedown', dropFake, true);
    window.addEventListener('mouseup', dropFake, true);

    document.addEventListener('visibilitychange', function () { if (document.hidden) state.focusLost = true; });
    window.addEventListener('blur', function () { state.focusLost = true; });
    // F3 shows the co-op network overlay. Most browsers also open their find bar on F3, which
    // takes focus from the game, so the browser's own use of the key is turned off here.
    window.addEventListener('keydown', function (event) {
        if (event.key === 'F3' || event.keyCode === 114) event.preventDefault();
    }, true);

    // Ctrl+V, Shift+Insert and the browser's own Paste item all send a paste event that carries the
    // text. Reading it here needs no permission prompt. Only the latest paste is kept until the
    // game takes it on its next frame.
    state.paste = String();
    document.addEventListener('paste', function (event) {
        try {
            var data = event.clipboardData || window.clipboardData;
            var text = data ? data.getData('text') : String();
            if (text) {
                state.paste = String(text);
                event.preventDefault();
            }
        } catch (error) {}
    }, true);
});

// Copies the latest pasted text into the buffer as plain printable ASCII (letters, digits, spaces
// and punctuation). Tabs, new lines and other characters are dropped.
// Returns how many characters were written, or 0 when nothing was pasted since the last call.
EM_JS(int, ls_js_take_paste, (char* out, int capacity), {
    var state = Module.lsInput;
    if (!state || !state.paste) return 0;
    var raw = state.paste;
    state.paste = String();
    var text = String();
    for (var i = 0; i < raw.length; i++) {
        var code = raw.charCodeAt(i);
        if (code >= 32 && code < 127) text += raw.charAt(i);
    }
    var length = Math.min(text.length, capacity - 1);
    for (var j = 0; j < length; j++) HEAPU8[out + j] = text.charCodeAt(j);
    HEAPU8[out + length] = 0;
    return length;
});

EM_JS(int, ls_js_take_touch, (float* out), {
    var state = Module.lsInput;
    if (!state || state.touches.length === 0) return 0;
    HEAPF32[out >> 2] = state.touches.shift();
    HEAPF32[(out >> 2) + 1] = state.touches.shift();
    return 1;
});

EM_JS(int, ls_js_no_finger_down, (), {
    var state = Module.lsInput;
    return state && state.fingers === 0 ? 1 : 0;
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

// Tells the page whether a text field is being edited. The page normally stops the browser's own
// Space action (scrolling), and that also stops the character event, so a name could not hold a
// space. While a field is being edited the page lets Space through.
EM_JS(void, ls_js_set_text_editing, (int editing), { window.LS_textEditing = editing !== 0; });

// Puts text on the system clipboard. A page may only do this soon after a click or tap, which is
// exactly when the Copy button calls it. Where the clipboard is blocked, the old textarea trick
// is tried. The last text is also kept on window, so the browser tests can check it.
EM_JS(void, ls_js_copy_text, (const char* text), {
    var out = String();
    for (var i = 0; HEAPU8[text + i] !== 0; i++) out += String.fromCharCode(HEAPU8[text + i]);
    window.LS_lastCopied = out;
    var fallback = function () {
        try {
            var area = document.createElement('textarea');
            area.value = out;
            area.readOnly = true;
            area.style.position = 'fixed';
            area.style.opacity = '0';
            document.body.appendChild(area);
            area.select();
            document.execCommand('copy');
            document.body.removeChild(area);
        } catch (error) {}
    };
    try {
        if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(out).catch(fallback);
        else fallback();
    } catch (error) {
        fallback();
    }
});

#endif

namespace ls::platform {

namespace {

float g_insetOverride = -1.0f;
bool g_insetDirty = false;  // Set when the override changes, so the next read is fresh.

#ifdef __EMSCRIPTEN__
// Clicks waiting for a frame. Two can land between frames on a slow device, and each
// must count. The game takes one per frame, so a second click can never press a button
// on a screen that the first click has already closed.
struct MousePress {
    float x = 0.0f;
    float y = 0.0f;
};
constexpr int kMaxMousePresses = 8;
MousePress g_mousePresses[kMaxMousePresses];
int g_mousePressCount = 0;

EM_BOOL onMouseDown(int, const EmscriptenMouseEvent* event, void*) {
    if (event->button != 0) return EM_FALSE;
    double cssWidth = 1.0;
    double cssHeight = 1.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    if (g_mousePressCount < kMaxMousePresses) {
        g_mousePresses[g_mousePressCount].x = static_cast<float>(event->targetX * GetScreenWidth() / cssWidth);
        g_mousePresses[g_mousePressCount].y = static_cast<float>(event->targetY * GetScreenHeight() / cssHeight);
        g_mousePressCount++;
    }
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
    if (g_mousePressCount == 0) return false;
    *x = g_mousePresses[0].x;
    *y = g_mousePresses[0].y;
    g_mousePressCount--;
    for (int i = 0; i < g_mousePressCount; ++i) g_mousePresses[i] = g_mousePresses[i + 1];
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

bool noFingerDown() {
#ifdef __EMSCRIPTEN__
    ensureInstalled();
    return ls_js_no_finger_down() != 0;
#else
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

#ifndef __EMSCRIPTEN__
namespace {
int g_argc = 0;
char** g_argv = nullptr;
}  // namespace
#endif

void setCommandLine(int argc, char** argv) {
#ifndef __EMSCRIPTEN__
    g_argc = argc;
    g_argv = argv;
#else
    (void)argc;
    (void)argv;
#endif
}

bool debugEnabled() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({ return new URLSearchParams(location.search).has('debug') ? 1 : 0; }) != 0;
#else
    // The build system defines LS_RELEASE_BUILD for Release builds, which are the ones players get.
#ifdef LS_RELEASE_BUILD
    constexpr bool kReleaseBuild = true;
#else
    constexpr bool kReleaseBuild = false;
#endif
    return nativeDebugWanted(kReleaseBuild, g_argc, g_argv);
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


void setTextEditing(bool editing) {
#ifdef __EMSCRIPTEN__
    // Only on a change: the text field asks every frame.
    static bool last = false;
    if (editing != last) {
        last = editing;
        ls_js_set_text_editing(editing ? 1 : 0);
    }
#else
    (void)editing;
#endif
}

int takePastedText(char* out, int capacity) {
    if (!out || capacity < 2) return 0;
    out[0] = '\0';
#ifdef __EMSCRIPTEN__
    ensureInstalled();
    return ls_js_take_paste(out, capacity);
#else
    // Native: Ctrl+V or Shift+Insert reads the system clipboard through raylib.
    const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (!((ctrl && IsKeyPressed(KEY_V)) || (shift && IsKeyPressed(KEY_INSERT)))) return 0;
    const char* clip = GetClipboardText();
    int length = 0;
    for (; clip && *clip && length < capacity - 1; ++clip) {
        if (*clip >= 32 && *clip < 127) out[length++] = *clip;
    }
    out[length] = '\0';
    return length;
#endif
}

void copyToClipboard(const char* text) {
#ifdef __EMSCRIPTEN__
    ls_js_copy_text(text);
#else
    (void)text;  // Native builds have no clipboard use. The Copy button is only for the browser.
#endif
}

#ifndef __EMSCRIPTEN__
namespace {

// The native save is a small file. On Windows it lives in %APPDATA%\LastStand, because the
// folder the game starts in may be read-only or different each time. The current folder
// comes second, so an older save still loads and a save is never lost for want of a place
// to put it.
int saveCandidates(std::filesystem::path* out) {
    int count = 0;
#ifdef _WIN32
    const std::wstring& path = nativeSavePath();
    if (!path.empty()) out[count++] = std::filesystem::path(path);
#endif
    out[count++] = std::filesystem::path("laststand.save");
    return count;
}

}  // namespace
#endif

SaveRead readSave(std::string* text) {
#ifdef __EMSCRIPTEN__
    static char buffer[8192];
    const int length = ls_js_storage_read(buffer, static_cast<int>(sizeof(buffer)));
    text->clear();
    if (length == -3) return SaveRead::Unreadable;  // Storage is blocked or failed. It may hold a good save.
    if (length < 0) return SaveRead::Missing;       // Nothing stored, or a value too big to be ours.
    text->assign(buffer, static_cast<size_t>(length));
    return SaveRead::Found;
#else
    std::filesystem::path candidates[2];
    const int count = saveCandidates(candidates);
    bool unreadable = false;
    for (int i = 0; i < count; ++i) {
        switch (savefile::readFile(candidates[i], text)) {
            case savefile::ReadResult::Found: return SaveRead::Found;
            case savefile::ReadResult::Unreadable: unreadable = true; break;
            case savefile::ReadResult::Missing: break;
        }
    }
    return unreadable ? SaveRead::Unreadable : SaveRead::Missing;
#endif
}

bool writeSave(const std::string& text) {
#ifdef __EMSCRIPTEN__
    return ls_js_storage_write(text.c_str()) != 0;
#else
    std::filesystem::path candidates[2];
    const int count = saveCandidates(candidates);
    for (int i = 0; i < count; ++i) {
        if (savefile::writeFileSafely(candidates[i], text)) return true;
    }
    return false;
#endif
}

}  // namespace ls::platform
