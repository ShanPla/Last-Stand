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

bool g_touching = false;
Vec2 g_touchOrigin;
int g_stickMenuX = 0;  // Remembers stick direction, so a held stick moves one step.
int g_stickMenuY = 0;

bool anyKeyDown(int a, int b) { return IsKeyDown(a) || IsKeyDown(b); }

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
            case KEY_W: case KEY_UP: s.up = true; break;
            case KEY_S: case KEY_DOWN: s.down = true; break;
            case KEY_A: case KEY_LEFT: s.left = true; break;
            case KEY_D: case KEY_RIGHT: s.right = true; break;
            case KEY_ENTER: case KEY_SPACE: s.confirm = true; break;
            case KEY_ESCAPE: case KEY_P: s.back = true; break;
            case KEY_R: s.reroll = true; break;
            default: break;
        }
        s.lastDevice = Device::Keyboard;
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
    s.left |= left;
    s.right |= right;
    s.up |= up;
    s.down |= down;
    s.confirm |= confirm;
    s.back |= back;
    s.reroll |= reroll;
    if (left || right || up || down || confirm || back || reroll) s.lastDevice = Device::Gamepad;
}

void readTouch(Snapshot& s) {
    const int points = GetTouchPointCount();
    if (points > 0) {
        const Vector2 touch = GetTouchPosition(0);
        const Vec2 pos{touch.x, touch.y};
        if (!g_touching) {
            g_touching = true;
            g_touchOrigin = pos;
            s.pointerPressed = true;
            s.pointer = pos;
        }
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
        s.lastDevice = Device::Touch;
        s.joystickActive = true;
        s.joystickOrigin = g_touchOrigin;
        s.joystickKnob = pos;
    } else {
        g_touching = false;
    }
}

void readMouse(Snapshot& s) {
    float x = 0.0f;
    float y = 0.0f;
    if (platform::consumeMouseDown(&x, &y) && !g_touching) {
        s.pointerPressed = true;
        s.pointer = Vec2{x, y};
        s.lastDevice = Device::Mouse;
    }
}

}  // namespace

float joystickRadius() {
    return std::max(56.0f, static_cast<float>(std::min(GetScreenWidth(), GetScreenHeight())) * 0.11f);
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
