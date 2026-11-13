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
    // Dash: Space or Shift, or the gamepad's right shoulder button. Space is also confirm,
    // so the game only acts on this while playing. The on-screen button is the HUD's own.
    bool dash = false;
    // Net arrow or D-pad presses this frame, so two quick presses in one frame still
    // move two steps. Left and up are negative.
    int stepX = 0;
    int stepY = 0;
    bool anyPress = false;  // Any key, button or touch went down this frame.
    // Shows or hides the co-op network overlay: F3, or the gamepad's View (Select) button
    // pressed while the left shoulder is held, so it is never pressed by accident.
    bool netOverlay = false;

    // Text entry. Only filled while setTextInputActive(true) is in force. typed holds the printable
    // characters pressed this frame, in order. backspace repeats while the key is held.
    char typed[8] = {};
    int typedCount = 0;
    int backspaces = 0;  // How many characters to delete: two quick presses in one frame count twice.
    // Text the player pasted this frame (Ctrl+V, Shift+Insert), as printable ASCII, or empty. It is
    // filled whether or not a field is being edited, and the first field that takes it clears it.
    char pasted[64] = {};
    int pastedLength = 0;

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

    // Joystick state, for drawing it. The origin is the stick's fixed place on the screen.
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

// Radius of the joystick, in drawing buffer pixels.
float joystickRadius();

// The joystick only exists while playing. Menus turn it off.
void setJoystickEnabled(bool enabled);

// Where the stick sits, in drawing buffer pixels. Set it every frame, because it follows the
// window size. A finger must land near this to steer.
void setJoystickHome(Vec2 home);

// While a text field is being edited, letters and Space type text instead of moving the menu, and
// Backspace and typed characters are reported. Enter, Esc and the arrow keys still work as
// before. The text field sets this every frame it is editing, and it takes effect on the next
// input read, so it ends by itself when the field goes away.
void setTextInputActive(bool active);

// A touch that starts inside one of these presses a button, and never starts the
// joystick. Set it every frame. The list is copied, and at most 8 rectangles are kept.
void setTouchBlockers(const Rect* rects, int count);

void update(Snapshot& snapshot);

}  // namespace ls::input
