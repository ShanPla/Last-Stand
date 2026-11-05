#pragma once

#include "components/Input/Input.h"
#include "components/Ui/Font/Font.h"

#include "raylib.h"

// Shared building blocks for every screen: sizes that scale with the window,
// buttons, sliders, bars, and keyboard, gamepad and pointer navigation.
//
// Screens are drawn in immediate mode: a button is drawn and checked for a press in
// the same call, which keeps each screen to one function and removes the chance of
// the picture and the hit area disagreeing.
namespace ls::ui {

// Screen measurements for the current frame. One design unit (u) is 1/560 of the
// short side, so a phone and a 4K monitor get the same layout, scaled.
struct Layout {
    float w = 1.0f;
    float h = 1.0f;
    float u = 1.0f;
    // The area that is safe to draw text in: inside phone notches, plus a margin.
    float left = 0.0f;
    float top = 0.0f;
    float right = 1.0f;
    float bottom = 1.0f;
    bool portrait = false;
    float touch = 44.0f;  // The smallest comfortable finger target: 44 CSS pixels.
    float px = 3.0f;      // One UI pixel, in screen pixels. A whole number, 2 or more.
    float width() const { return right - left; }
    float height() const { return bottom - top; }
    float centerX() const { return (left + right) * 0.5f; }
};

// Call first thing in each frame, before any screen is drawn.
void beginFrame(const input::Snapshot& input, float dtReal);
const Layout& layout();
inline float u() { return layout().u; }
// Grows a button height to the finger-target minimum when the screen is small.
inline float tall(float height) { return height > layout().touch ? height : layout().touch; }
const input::Snapshot& input();
float uiSeconds();  // Seconds since start. Keeps running during pause.

// Screen fade. Everything drawn through these helpers is multiplied by it, so a
// screen fades in by raising it from 0 to 1. It never blocks input.
void setFade(float fade);
float fade();
Color tint(Color color);
Color withAlpha(Color color, float alpha);  // Alpha 0 to 1, multiplied in. Does not apply the fade, so pass the result to helpers that do.

// Forgets this frame's presses. Call after changing screens, so one press cannot
// act on the old screen and then again on the new one.
void consumePresses();

// When false, no button reacts. Used for the short delay that stops accidental presses.
void setInputEnabled(bool enabled);
bool inputEnabled();

// Prompts for whichever device was used last. Empty for touch.
bool showPrompts();
const char* promptConfirm();
const char* promptBack();
const char* promptReroll();
const char* promptDash();
const char* promptPause();

// Text, faded with the screen.
void text(const char* text, float x, float y, float size, Color color, font::Align align = font::Align::Left, float tracking = 0.0f);
void textShadowed(const char* text, float x, float y, float size, Color color, font::Align align = font::Align::Left, float tracking = 0.0f);
void textMiddle(const char* text, float x, float cy, float size, Color color, font::Align align = font::Align::Left, float tracking = 0.0f);

// Shapes, faded with the screen. Everything is built from squares of one UI pixel
// (Layout::px), and every position and size snaps to that grid, so edges stay hard at
// any window size. The radius arguments are kept for the callers, and are ignored:
// corners are always a small stepped notch, never a smooth curve.
float snapPx(float value);
void roundedRect(Rectangle rect, float radius, Color color);
void roundedOutline(Rectangle rect, float radius, float thickness, Color color);
// Flat fill, a dark outer border and a lighter inner border: the two-tone pixel border.
void panel(Rectangle rect, Color fill, Color edge, float radius);
void bar(Rectangle rect, float fraction, Color fill, Color back);
void pixelRect(Rectangle rect, Color color);  // Plain square-cornered rectangle on the pixel grid.
void pixelDisc(float cx, float cy, float radius, Color color);
void pixelRing(float cx, float cy, float radius, float thickness, Color color);
void pixelArrow(float x, float cy, float size, Color color);  // A small right-pointing arrow.
void keyChip(const char* label, float x, float cy, float height, font::Align align = font::Align::Left);

bool inside(Vec2 point, Rectangle rect);

// ---- Navigation and controls ----------------------------------------------------

struct Nav {
    int selected = 0;
};

// Moves the selection with up and down (and left and right when horizontal), with wrap-around.
void navigate(Nav& nav, int count, bool horizontalToo = false);

enum class ButtonLook { Normal, Primary, Small };

// Draws a button, and returns true on the frame it is activated by a click, a tap,
// or Enter, Space or gamepad A while selected. keyHint is shown for keyboard and gamepad.
bool button(Nav& nav, int index, Rectangle rect, const char* label, ButtonLook look = ButtonLook::Normal,
            const char* keyHint = nullptr, bool enabled = true);

// A 0 to 1 slider row. Returns true while the value changes.
bool slider(Nav& nav, int index, Rectangle rect, const char* label, float* value, float step);

// An on or off row. Returns true when it changes.
bool toggle(Nav& nav, int index, Rectangle rect, const char* label, bool* value);

// ---- Layout record for tests ----------------------------------------------------

struct Registered {
    char name[24];
    Rectangle rect;
};
int registeredCount();
const Registered& registered(int index);

// Lets screens that draw their own hit areas, such as cards, appear in the record.
void registerRect(const char* name, Rectangle rect);

// The control that has keyboard or gamepad focus this frame, for the tests.
void markSelected(const char* name);
const char* selectedName();


}  // namespace ls::ui
