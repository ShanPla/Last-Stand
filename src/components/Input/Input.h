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
    bool back = false;
    bool reroll = false;

    // Mouse click or touch start, in drawing buffer pixels.
    bool pointerPressed = false;
    Vec2 pointer;

    // Floating joystick state, for drawing it.
    bool joystickActive = false;
    Vec2 joystickOrigin;
    Vec2 joystickKnob;
};

// Radius of the floating joystick, in drawing buffer pixels.
float joystickRadius();

void update(Snapshot& snapshot);

}  // namespace ls::input
