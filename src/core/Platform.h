#pragma once

// Everything that differs between the browser and the native Windows build.
namespace ls::platform {

// Matches the drawing buffer to the window size and device pixel ratio.
// renderScale is an extra factor, below 1 on slow devices. Call once per frame.
void syncCanvasSize(float renderScale);

// True once for each left mouse press since the last call, with its position in
// drawing buffer pixels. Catches a press and release that both happen between two
// frames, which a check of the button state each frame would miss.
bool consumeMouseDown(float* x, float* y);

// True when the page was opened with ?debug, which shows the debug overlay.
bool debugEnabled();

}  // namespace ls::platform
