#pragma once

#include "core/Math.h"

// The one mapping from the flat simulation world to the low-resolution picture.
// It is a real pinhole camera looking at the ground, so straight lines stay straight
// and road lines converge. Everything drawn in the world goes through project().
namespace ls::worldview {

// Art pixels per world unit, at the player's depth. Sprites are drawn 1:1 at this
// density, so a window of any size just shows more or fewer art pixels of the world.
inline constexpr float kArtPerWorld = 0.6f;

// How much the ground is squashed vertically: a square on the ground is drawn this
// much shorter than it is wide. Together with upright sprites this gives the 3/4 look.
inline constexpr float kTiltSquash = 224.0f / 384.0f;

// How quickly things shrink with distance up the screen, per world unit.
inline constexpr float kPerspective = 0.00045f;

// Keeps the divide safe for points far behind the camera, such as a long boss line.
inline constexpr float kMinDepth = 0.25f;

struct View {
    int width = 640;   // Size of the low-resolution target, in art pixels.
    int height = 360;
    float pixelScale = 2.0f;  // Screen pixels per art pixel.
    float focusX = 0.0f;      // World point at the anchor. Snapped to whole art pixels.
    float focusY = 0.0f;
    float anchorX = 320.0f;   // Where that point is in the target.
    float anchorY = 180.0f;
};

struct ScreenPoint {
    float x;
    float y;
    float scale;  // 1 at the player's depth, smaller further up the screen.
};

inline float depthAt(float dy) {
    const float depth = 1.0f - kPerspective * dy;
    return depth > kMinDepth ? depth : kMinDepth;
}

inline ScreenPoint project(const View& view, Vec2 world) {
    const float dx = world.x - view.focusX;
    const float dy = world.y - view.focusY;
    const float scale = 1.0f / depthAt(dy);
    return {view.anchorX + kArtPerWorld * dx * scale, view.anchorY + kArtPerWorld * kTiltSquash * dy * scale, scale};
}

// The ground point seen at a target pixel, for finding what the screen can see.
inline Vec2 unproject(const View& view, float x, float y) {
    const float rowsFromAnchor = y - view.anchorY;
    // Solves y = k * dy / (1 - p * dy) for dy.
    const float dy = rowsFromAnchor / (kArtPerWorld * kTiltSquash + rowsFromAnchor * kPerspective);
    const float dx = (x - view.anchorX) * depthAt(dy) / kArtPerWorld;
    return {view.focusX + dx, view.focusY + dy};
}

// True when a figure standing at this point could touch the target. Margins are in
// art pixels: figures rise above their feet, so they need more room below the screen.
inline bool onTarget(const View& view, const ScreenPoint& point, float side, float above, float below) {
    return point.x > -side && point.x < static_cast<float>(view.width) + side && point.y > -below &&
           point.y < static_cast<float>(view.height) + above;
}

}  // namespace ls::worldview
