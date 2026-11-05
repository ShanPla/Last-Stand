#pragma once

#include "core/Math.h"

namespace ls::input {

enum class Device { Keyboard, Gamepad, Touch, Mouse };

// One frame of input from every device, merged into one shape, so the rest of
// the game never needs to know which device the player is using.
struct Snapshot {
    float moveX = 0.0f;
    float moveY = 0.0f;
    Device lastDevice = Device::Keyboard;

    // Menu actions. Each is true for one frame only.
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;
    bool back = false;     // Esc, P, gamepad B or Start. Pauses, and goes back in menus.
    bool reroll = false;
    // Dash: Shift or Space, or the gamepad's right shoulder button. Space is also confirm,
    // so the game only acts on this while playing. The on-screen button is the HUD's own.
    bool dash = false;
    // Net arrow or D-pad presses this frame, so two quick presses in one frame still
    // move two steps. Left and up are negative.
    int stepX = 0;
    int stepY = 0;
    bool anyPress = false;  // Any key, button or touch went down this frame.

    // Mouse click or touch start, in drawing buffer pixels.
    bool pointerPressed = false;
    Vec2 pointer;
    bool pointerIsTouch = false;

    // The mouse or first finger while held down, for dragging sliders.
    bool pointerDown = false;
    Vec2 pointerPos;

    // Where the mouse is, and whether it moved. Used for hover highlights.
    Vec2 mouse;
    bool mouseMoved = false;

    // Floating joystick state, for drawing it.
    bool joystickActive = false;
    Vec2 joystickOrigin;
    Vec2 joystickKnob;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Radius of the floating joystick, in drawing buffer pixels.
float joystickRadius();

// The joystick only exists while playing. Menus turn it off.
void setJoystickEnabled(bool enabled);

// A touch that starts inside one of these presses a button, and never starts the
// joystick. Set it every frame. The list is copied, and at most 8 rectangles are kept.
void setTouchBlockers(const Rect* rects, int count);

void update(Snapshot& snapshot);

}  // namespace ls::input
