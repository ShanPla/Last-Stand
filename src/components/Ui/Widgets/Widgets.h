#pragma once

#include "components/Input/Input.h"
#include "components/Ui/Font/Font.h"

#include "raylib.h"

#include <cstddef>

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

// A run clock: M:SS, and H:MM:SS from one hour on, so a long Endless run never shows 75:00.
void formatClock(char* out, size_t size, float seconds);

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
void pixelArrow(float x, float cy, float size, Color color, bool pointLeft = false);  // A small arrow. x is the left edge of its box.
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

// A check box with its label, as one large button: "Label [box]". The box is empty when off and a
// green check when on. A click, a tap, Enter or gamepad A flips it. Returns true when it changes.
bool checkBox(Nav& nav, int index, Rectangle rect, const char* label, bool* value);

// Just the box of a check box, centered on (cx, cy). unit is the size of one UI pixel, and the box is
// nine of them wide. The player list in the co-op lobby uses it for the ready mark.
void drawCheckBox(float cx, float cy, float unit, bool checked);

// A row of named choices with an arrow on each side, such as the difficulty or the
// map. Left and right (keys, D-pad, stick) change it while it has focus. A click or
// tap on an arrow steps that way, and on the middle steps forward. Enter and A step
// forward too. It wraps at both ends. optionColors may be null. Returns true when the
// value changes. The three parts are recorded for tests as "<label> <", "<label>" and
// "<label> >".
bool selector(Nav& nav, int index, Rectangle rect, const char* label, const char* const* options, int count, int* value,
              const Color* optionColors = nullptr);

// ---- Text entry -----------------------------------------------------------------

// A one-line field that holds a player name or a room code. It works with a physical
// keyboard, with a mouse click or a touch tap to focus it, and with a pixel keyboard drawn on
// screen, which opens when the last device was touch or a gamepad. The pixel keyboard is
// simple on purpose: it is easier to build and to test than asking the phone for its own.
enum class TextKind {
    Name,  // Letters, digits and spaces, up to 12.
    Code,  // A room code: 5 characters from the code alphabet, always shown in capitals.
};

struct TextEntry {
    char text[16] = "";
    int length = 0;
    TextKind kind = TextKind::Name;
    bool editing = false;
    bool keys = false;       // The on-screen keyboard is open for this field.
    bool shift = true;       // Name keyboard: the next letter is a capital.
    int key = 0;             // The selected on-screen key, for gamepad and keyboard.
    bool finished = false;   // True for the one frame in which editing ended.
};

// Replaces the text, keeping only the characters the field allows.
void textSet(TextEntry& entry, TextKind kind, const char* text);

// Draws the field and reads its input. Draw the fields of a screen before its buttons, because
// an Enter or Esc that ends the editing is used up here and must not also press a button.
// Activating it (click, tap, Enter, A) starts editing. Enter, Esc, an Up or Down press, or a
// click outside the field ends it. placeholder shows, dimmed, while the field is empty.
void textField(Nav& nav, int index, Rectangle rect, const char* label, TextEntry& entry, TextKind kind, const char* placeholder);

// Draws the on-screen keyboard if one is open, and handles its keys. Call it last in a screen's
// drawing, after every field and button, then read TextEntry::finished.
void drawTextKeyboard();

// True while any field is being edited. Esc then only ends the editing, so a screen must not
// also treat it as Back.
bool textEditing();
// True while the on-screen keyboard is open. A screen turns its own buttons and navigation off
// meanwhile, with setInputEnabled(false).
bool textKeyboardOpen();
// Stops editing without a finish. Call it when a screen is left.
void textCancel();

// ---- Layout record for tests ----------------------------------------------------

struct Registered {
    char name[24];
    Rectangle rect;
};
int registeredCount();
const Registered& registered(int index);

// Lets screens that draw their own hit areas, such as cards, appear in the record.
void registerRect(const char* name, Rectangle rect);

// Panels and the reach of their content, for the layout tests: where a tip's text really
// ends, how far a build list runs. They are not controls, so they are kept apart from
// registerRect, and the tests that check every control (size, overlap) never see them.
void registerExtent(const char* name, Rectangle rect);
int extentCount();
const Registered& extent(int index);

// The control that has keyboard or gamepad focus this frame, for the tests.
void markSelected(const char* name);
const char* selectedName();


}  // namespace ls::ui
