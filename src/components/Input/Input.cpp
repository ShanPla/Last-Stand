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

constexpr int kMaxFingers = 8;  // raylib keeps at most this many touch points.
constexpr float kStickGrabReach = 1.8f;  // A thumb landing this many radii from the stick still takes it.

bool g_touching = false;
int g_stickId = -1;  // Touch id of the finger steering the joystick, or -1 for none.
int g_knownIds[kMaxFingers];  // Fingers seen last frame, to spot the ones that just landed.
int g_knownCount = 0;
Vec2 g_stickHome;
int g_stickMenuX = 0;  // Remembers stick direction, so a held stick moves one step.
int g_stickMenuY = 0;
bool g_joystickEnabled = true;
bool g_textInput = false;
double g_backspaceRepeatAt = 0.0;  // When a held Backspace deletes again.
constexpr double kBackspaceDelay = 0.40;
constexpr double kBackspaceRepeat = 0.05;
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

int indexOfId(const int* ids, int count, int id) {
    for (int i = 0; i < count; ++i) {
        if (ids[i] == id) return i;
    }
    return -1;
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
        if (g_textInput) {
            // Letters and Space are text now, so they must not also move the menu.
            switch (key) {
                case KEY_UP: s.up = true; s.stepY--; break;
                case KEY_DOWN: s.down = true; s.stepY++; break;
                case KEY_LEFT: s.left = true; s.stepX--; break;
                case KEY_RIGHT: s.right = true; s.stepX++; break;
                case KEY_ENTER: case KEY_KP_ENTER: s.confirm = true; break;
                case KEY_ESCAPE: s.back = true; break;
                case KEY_BACKSPACE:
                    s.backspaces++;
                    g_backspaceRepeatAt = GetTime() + kBackspaceDelay;
                    break;
                default: break;
            }
        } else {
            switch (key) {
                case KEY_W: case KEY_UP: s.up = true; s.stepY--; break;
                case KEY_S: case KEY_DOWN: s.down = true; s.stepY++; break;
                case KEY_A: case KEY_LEFT: s.left = true; s.stepX--; break;
                case KEY_D: case KEY_RIGHT: s.right = true; s.stepX++; break;
                case KEY_ENTER: case KEY_KP_ENTER: s.confirm = true; break;
                case KEY_SPACE: s.confirm = true; s.dash = true; break;
                case KEY_LEFT_SHIFT: case KEY_RIGHT_SHIFT: s.dash = true; break;
                case KEY_ESCAPE: case KEY_P: s.back = true; break;
                case KEY_R: s.reroll = true; break;
                case KEY_F3: s.netOverlay = true; break;
                default: break;
            }
        }
        s.lastDevice = Device::Keyboard;
        s.anyPress = true;
        key = GetKeyPressed();
    }

    // Typed characters are always read out of raylib's queue, so old ones can never turn up later
    // when a field opens. They are only kept while a field is being edited.
    // A character that arrives while Ctrl is held belongs to a shortcut such as Ctrl+V, not to the text.
    const bool shortcut = anyKeyDown(KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL) || anyKeyDown(KEY_LEFT_SUPER, KEY_RIGHT_SUPER);
    int character = GetCharPressed();
    while (character != 0) {
        if (g_textInput && !shortcut && character >= 32 && character < 127 && s.typedCount < static_cast<int>(sizeof(s.typed))) {
            s.typed[s.typedCount++] = static_cast<char>(character);
            s.lastDevice = Device::Keyboard;
        }
        character = GetCharPressed();
    }
    // Pasted text is always taken out of the platform's queue, so an old paste can't turn up later.
    s.pastedLength = platform::takePastedText(s.pasted, static_cast<int>(sizeof(s.pasted)));
    // A held Backspace deletes again and again, after a short wait.
    if (g_textInput && s.backspaces == 0 && IsKeyDown(KEY_BACKSPACE) && GetTime() >= g_backspaceRepeatAt) {
        s.backspaces = 1;
        g_backspaceRepeatAt = GetTime() + kBackspaceRepeat;
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
    // The right shoulder, not A: A is menu confirm, and must not dash when a card is picked.
    const bool dash = IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
    s.netOverlay |= IsGamepadButtonDown(kGamepad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) && IsGamepadButtonPressed(kGamepad, GAMEPAD_BUTTON_MIDDLE_LEFT);
    s.stepX += (right ? 1 : 0) - (left ? 1 : 0);
    s.stepY += (down ? 1 : 0) - (up ? 1 : 0);
    s.left |= left;
    s.right |= right;
    s.up |= up;
    s.down |= down;
    s.confirm |= confirm;
    s.back |= back;
    s.reroll |= reroll;
    s.dash |= dash;
    if (left || right || up || down || confirm || back || reroll || dash) {
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
    // raylib keeps counting a finger whose touch the browser cancelled (an edge swipe, a
    // call), because it only removes fingers on touchend. The browser's own count says the
    // screen is empty, and that wins, so a cancelled touch can't leave the joystick stuck.
    const int points = platform::noFingerDown() ? 0 : std::min(GetTouchPointCount(), kMaxFingers);
    if (latched) {
        s.pointerPressed = true;
        s.pointerIsTouch = true;
        s.pointer = Vec2{latchedX, latchedY};
        s.pointerPos = s.pointer;
        s.anyPress = true;
        s.lastDevice = Device::Touch;
        g_lastTouchTime = GetTime();
    }

    if (points <= 0) {
        g_touching = false;
        g_knownCount = 0;
        g_stickId = -1;
        return;
    }
    g_touching = true;
    g_lastTouchTime = GetTime();

    // raylib's list is not stable: it shifts when a finger lifts, so index 0 is not always
    // the same finger. The touch ids are stable, so fingers are followed by id.
    int ids[kMaxFingers];
    Vec2 positions[kMaxFingers];
    for (int i = 0; i < points; ++i) {
        ids[i] = GetTouchPointId(i);
        const Vector2 touch = GetTouchPosition(i);
        positions[i] = Vec2{touch.x, touch.y};
    }

    // If the joystick finger lifted, the joystick ends. It never moves to another finger.
    if (g_stickId != -1 && indexOfId(ids, points, g_stickId) < 0) g_stickId = -1;

    // Fingers we did not see last frame just landed. Only a finger that lands away from the
    // buttons, while the joystick is free, can become the joystick.
    int landed[kMaxFingers];
    int landedCount = 0;
    for (int i = 0; i < points; ++i) {
        bool known = false;
        for (int k = 0; k < g_knownCount; ++k) known = known || g_knownIds[k] == ids[i];
        if (!known) landed[landedCount++] = i;
    }
    for (int n = 0; n < landedCount; ++n) {
        const int i = landed[n];
        // The browser event has the exact start. It has no id though, so it is only trusted
        // when one finger landed this frame. With several, the current position stands in.
        const bool exact = latched && landedCount == 1;
        const Vec2 start = exact ? Vec2{latchedX, latchedY} : positions[i];
        if (n == 0 && !latched) {
            // Native builds have no browser event, so raylib's list is the source.
            s.pointerPressed = true;
            s.pointerIsTouch = true;
            s.pointer = positions[i];
            s.anyPress = true;
        }
        // A thumb can't see the stick under it and won't land exactly on it, so the reach is
        // wider than the stick. Touches elsewhere leave the player's view of the map free.
        const float reach = joystickRadius() * kStickGrabReach;
        if (g_stickId == -1 && g_joystickEnabled && !insideBlocker(start) && distanceSq(start, g_stickHome) <= reach * reach) {
            g_stickId = ids[i];
        }
    }
    g_knownCount = points;
    for (int i = 0; i < points; ++i) g_knownIds[i] = ids[i];

    s.pointerDown = true;
    s.pointerPos = positions[0];
    s.lastDevice = Device::Touch;

    const int stick = g_stickId == -1 ? -1 : indexOfId(ids, points, g_stickId);
    if (stick >= 0 && g_joystickEnabled) {
        // The stick stays put, so it is always in the same place under the thumb. A thumb past
        // its edge just means full speed in that direction.
        const Vec2 pos = positions[stick];
        const float radius = joystickRadius();
        Vec2 delta = pos - g_stickHome;
        const float len = length(delta);
        if (len > radius) delta = delta * (radius / len);
        s.moveX = delta.x / radius;
        s.moveY = delta.y / radius;
        s.joystickActive = true;
        s.joystickOrigin = g_stickHome;
        s.joystickKnob = pos;
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

void setTextInputActive(bool active) {
    g_textInput = active;
    // A field that opens tells the page at once, so it lets Space through as a typed space even
    // when the very next key is Space. Waiting for the next frame lost that space. Closing can wait
    // for update(), because letting Space through for one more frame is harmless.
    if (active) platform::setTextEditing(true);
}

void setJoystickHome(Vec2 home) { g_stickHome = home; }

void setTouchBlockers(const Rect* rects, int count) {
    g_blockerCount = std::min(count, kMaxBlockers);
    for (int i = 0; i < g_blockerCount; ++i) g_blockers[i] = rects[i];
}

void update(Snapshot& s) {
    const Device kept = s.lastDevice;
    s = Snapshot{};
    s.lastDevice = kept;
    // What the last frame's text field asked for. The page is told, so it lets Space through.
    platform::setTextEditing(g_textInput);
    readKeyboard(s);
    readGamepad(s);
    readTouch(s);
    readMouse(s);
}

}  // namespace ls::input
