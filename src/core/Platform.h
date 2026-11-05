#pragma once

#include <string>

// Everything that differs between the browser and the native Windows build.
namespace ls::platform {

// Matches the drawing buffer to the window size and device pixel ratio.
// renderScale is an extra factor, below 1 on slow devices. Call once per frame.
void syncCanvasSize(float renderScale);

// True once for each left mouse press since the last call, with its position in
// drawing buffer pixels. Catches a press and release that both happen between two
// frames, which a check of the button state each frame would miss.
bool consumeMouseDown(float* x, float* y);

// The same for touch: true once for each new touch since the last call. A tap that
// starts and ends between two frames is gone from raylib's touch list by the time
// the frame runs, so the browser event is remembered here. Uses the first finger
// of the event, in drawing buffer pixels.
bool consumeTouchStart(float* x, float* y);

// True once after the tab was hidden or the window lost focus, so the game can pause.
bool consumeFocusLost();

// True when the debug overlay should show: the page was opened with ?debug, or on native
// when the build is not a Release build or the program was started with --debug.
bool debugEnabled();

// Remembers the program's arguments, for --debug. Native only, and call it before init.
void setCommandLine(int argc, char** argv);

// Space taken by phone notches and rounded corners, in drawing buffer pixels.
struct SafeInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};
SafeInsets safeInsets();

// Test hook: pretends every edge has this many CSS pixels of inset. Negative clears it.
void setSafeInsetOverride(float cssPixels);

// Drawing buffer pixels per CSS pixel: the device pixel ratio, capped at 2 like the canvas. 1 on native.
float pixelRatio();

// The system "reduce motion" preference. False where the system has none.
bool prefersReducedMotion();

// True on a device whose only pointer is a finger: a coarse pointer and no fine one.
// A laptop with a touch screen still has a mouse, so it stays false there.
bool touchIsOnlyPointer();

// Save storage: browser localStorage, or a file on native (%APPDATA%\LastStand on Windows,
// with the current folder as the fallback).
// Both fail quietly (for example when storage is blocked), and return false.
bool readSave(std::string* text);
bool writeSave(const std::string& text);

}  // namespace ls::platform
