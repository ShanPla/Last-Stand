#include "components/Input/Input.h"

#include "core/Platform.h"

#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace ls::input {

namespace {

constexpr float kStickDeadZone = 0.2f;
constexpr float kStickMenuThreshold = 0.6f;
constexpr int kGamepad = 0;
constexpr int kMaxBlockers = 8;
constexpr double kMouseAfterTouchSeconds = 1.0;  // Mouse movement this soon after a touch is the touch itself.

bool g_touching = false;
bool g_touchBlocked = false;  // This touch started on a button, so it is not a joystick.
Vec2 g_touchOrigin;
int g_stickMenuX = 0;  // Remembers stick direction, so a held stick moves one step.
int g_stickMenuY = 0;
bool g_joystickEnabled = true;
Rect g_blockers[kMaxBlockers];
int g_blockerCount = 0;
double g_lastTouchTime = -100.0;
Vec2 g_lastMouse;

bool anyKeyDown(int a, int b) { return IsKeyDown(a) || IsKeyDown(b); }

bool insideBlocker(Vec2 p) {
    for (int i = 0; i < g_blockerCount; ++i) {
        const Rect& r = g_blockers[i];
        if (p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h) return true;
    }
    return false;
}

void readKeyboard(Snapshot& s) {
    float x = 0.0f;
    float y = 0.0f;
    if (anyKeyDown(KEY_A, KEY_LEFT)) x -= 1.0f;
    if (anyKeyDown(KEY_D, KEY_RIGHT)) x += 1.0f;
    if (anyKeyDown(KEY_W, KEY_UP)) y -= 1.0f;
    if (anyKeyDown(KEY_S, KEY_DOWN)) y += 1.0f;
    if (x != 0.0f || y != 0.0f) {
        s.moveX = x;
        s.moveY = y;
        s.lastDevice = Device::Keyboard;
    }
    // The press queue, not IsKeyPressed: a key pressed and released between two
    // frames would be invisible to a state check, and on slow devices that happens.
    int key = GetKeyPressed();
    while (key != 0) {
        switch (key) {
            case KEY_W: case KEY_UP: s.up = true; s.stepY--; break;
            case KEY_S: case KEY_DOWN: s.down = true; s.stepY++; break;
            case KEY_A: case KEY_LEFT: s.left = true; s.stepX--; break;
            case KEY_D: case KEY_RIGHT: s.right = true; s.stepX++; break;
            case KEY_ENTER: case KEY_KP_ENTER: case KEY_SPACE: s.confirm = true; break;
            case KEY_ESCAPE: case KEY_P: s.back = true; break;
            case KEY_R: s.reroll = true; break;
            default: break;
        }
        s.lastDevice = Device::Keyboard;
        s.anyPress = true;
        key = GetKeyPressed();
    }
}

void readGamepad(Snapshot& s) {
    if (!IsGamepadAvailable(kGamepad)) return;
    const float x = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_X);
    const float y = GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_Y);
    if (std::hypot(x, y) > kStickDeadZone) {
        s.moveX = x;
        s.moveY = y;
        s.lastDevice = Device::Gamepad;
    }

    const int stickX = x > kStickMenuThreshold ? 1 : (x < -kStickMenuThreshold ? -1 : 0);
    const int stickY = y > kStickMenuThreshold ? 1 : (y < -kStickMenuThreshold ? -1 : 0);
    const bool left = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (stickX < 0 && g_stickMenuX >= 0);
    const bool right = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (stickX > 0 && g_stickMenuX <= 0);
    const bool up = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_UP) || (stickY < 0 && g_stickMenuY >= 0);
    const bool down = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (stickY > 0 && g_stickMenuY <= 0);
    g_stickMenuX = stickX;
    g_stickMenuY = stickY;
    const bool confirm = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    const bool back = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
                      IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    const bool reroll = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_RIGHT_FACE_UP);
    s.stepX += (right ? 1 : 0) - (left ? 1 : 0);
    s.stepY += (down ? 1 : 0) - (up ? 1 : 0);
    s.left |= left;
    s.right |= right;
    s.up |= up;
    s.down |= down;
    s.confirm |= confirm;
    s.back |= back;
    s.reroll |= reroll;
    if (left || right || up || down || confirm || back || reroll) {
        s.lastDevice = Device::Gamepad;
        s.anyPress = true;
    }
}

void readTouch(Snapshot& s) {
    // A tap can start and end between two frames, so raylib's own list may never
    // show it. The browser event was remembered, and is the reliable start.
    float latchedX = 0.0f;
    float latchedY = 0.0f;
    const bool latched = platform::consumeTouchStart(&latchedX, &latchedY);
    const int points = GetTouchPointCount();
    if (latched) {
        s.pointerPressed = true;
        s.pointerIsTouch = true;
        s.pointer = Vec2{latchedX, latchedY};
        s.pointerPos = s.pointer;
        s.anyPress = true;
        s.lastDevice = Device::Touch;
        g_lastTouchTime = GetTime();
    }

    if (points > 0) {
        const Vector2 touch = GetTouchPosition(0);
        const Vec2 pos{touch.x, touch.y};
        g_lastTouchTime = GetTime();
        if (!g_touching) {
            g_touching = true;
            const Vec2 start = latched ? Vec2{latchedX, latchedY} : pos;
            g_touchOrigin = start;
            g_touchBlocked = !g_joystickEnabled || insideBlocker(start);
            if (!latched) {
                // Native builds have no browser event, so raylib's list is the source.
                s.pointerPressed = true;
                s.pointerIsTouch = true;
                s.pointer = pos;
                s.anyPress = true;
            }
        }
        s.pointerDown = true;
        s.pointerPos = pos;
        s.lastDevice = Device::Touch;
        if (!g_touchBlocked && g_joystickEnabled) {
            // The joystick floats: its center follows the thumb if the thumb goes past
            // the edge, so changing direction never needs a long drag back.
            const float radius = joystickRadius();
            Vec2 delta = pos - g_touchOrigin;
            const float len = length(delta);
            if (len > radius) {
                g_touchOrigin += delta * ((len - radius) / len);
                delta = pos - g_touchOrigin;
            }
            s.moveX = delta.x / radius;
            s.moveY = delta.y / radius;
            s.joystickActive = true;
            s.joystickOrigin = g_touchOrigin;
            s.joystickKnob = pos;
        }
    } else {
        g_touching = false;
        g_touchBlocked = false;
    }
}

void readMouse(Snapshot& s) {
    const Vector2 mouse = GetMousePosition();
    s.mouse = Vec2{mouse.x, mouse.y};
    const bool recentTouch = g_touching || (GetTime() - g_lastTouchTime) < kMouseAfterTouchSeconds;
    if (!recentTouch && (std::fabs(mouse.x - g_lastMouse.x) > 0.5f || std::fabs(mouse.y - g_lastMouse.y) > 0.5f)) {
        s.mouseMoved = true;
        s.lastDevice = Device::Mouse;
    }
    g_lastMouse = s.mouse;

    float x = 0.0f;
    float y = 0.0f;
    if (platform::consumeMouseDown(&x, &y) && !recentTouch) {
        s.pointerPressed = true;
        s.pointerIsTouch = false;
        s.pointer = Vec2{x, y};
        s.anyPress = true;
        s.lastDevice = Device::Mouse;
    }
    if (!recentTouch && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        s.pointerDown = true;
        s.pointerPos = s.mouse;
    }
}

}  // namespace

float joystickRadius() {
    return std::max(64.0f, static_cast<float>(std::min(GetScreenWidth(), GetScreenHeight())) * 0.15f);
}

void setJoystickEnabled(bool enabled) { g_joystickEnabled = enabled; }

void setTouchBlockers(const Rect* rects, int count) {
    g_blockerCount = std::min(count, kMaxBlockers);
    for (int i = 0; i < g_blockerCount; ++i) g_blockers[i] = rects[i];
}

void update(Snapshot& s) {
    const Device kept = s.lastDevice;
    s = Snapshot{};
    s.lastDevice = kept;
    readKeyboard(s);
    readGamepad(s);
    readTouch(s);
    readMouse(s);
}

}  // namespace ls::input
