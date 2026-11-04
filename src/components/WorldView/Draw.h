#pragma once

#include "core/Math.h"

#include "raylib.h"
#include "rlgl.h"

#include <cstdint>

// Small drawing helpers shared by the world renderer. Everything is emitted as
// quads inside one rlBegin(RL_QUADS) pass, so raylib keeps one big batch.
namespace ls::worldview {

// The simulation is flat. The renderer squashes the world's y axis to look like
// a tilted camera. Height is added straight up the screen, after this.
inline constexpr float kTilt = 0.72f;

inline Vec2 project(Vec2 world) { return {world.x, world.y * kTilt}; }

// Height that bullets fly at, matching the gun, so they read as hitting torsos.
inline constexpr float kShotHeight = 24.0f;

// Screen edges in view units (after the tilt), for skipping unseen objects.
struct ViewBounds {
    float left, right, top, bottom;

    // above is how far the object rises over its ground point, below how far it
    // hangs under it. Standing figures need more room above than below.
    bool contains(Vec2 view, float side, float above, float below) const {
        return view.x > left - side && view.x < right + side && view.y > top - below && view.y < bottom + above;
    }
};

inline Color fade(Color color, float amount) {
    color.a = static_cast<unsigned char>(static_cast<float>(color.a) * clampf(amount, 0.0f, 1.0f));
    return color;
}

inline void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color color) {
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex2f(a.x, a.y);
    rlVertex2f(b.x, b.y);
    rlVertex2f(c.x, c.y);
    rlVertex2f(d.x, d.y);
}

// A quad that fades from one end to the other. One quad gives a soft trail.
inline void gradientQuad(Vec2 a, Vec2 b, Color start, Vec2 c, Vec2 d, Color end) {
    rlColor4ub(start.r, start.g, start.b, start.a);
    rlVertex2f(a.x, a.y);
    rlVertex2f(b.x, b.y);
    rlColor4ub(end.r, end.g, end.b, end.a);
    rlVertex2f(c.x, c.y);
    rlVertex2f(d.x, d.y);
}

inline Vec2 perpendicular(Vec2 dir) { return {-dir.y, dir.x}; }

inline void thickLine(Vec2 from, Vec2 to, float width, Color color) {
    const Vec2 side = perpendicular(normalizedOr(to - from, Vec2{1.0f, 0.0f})) * (width * 0.5f);
    quad(from + side, from - side, to - side, to + side, color);
}

// One unit circle, computed once, so drawing never needs sine or cosine per object.
inline constexpr int kCirclePoints = 48;
struct UnitCircle {
    Vec2 points[kCirclePoints];
    UnitCircle();
};
extern const UnitCircle g_circle;

// Filled ellipse. step picks every Nth circle point: 12 gives four corners, 6
// eight, 4 twelve, and 2 twenty-four. It must divide kCirclePoints evenly.
void oval(Vec2 center, float radiusX, float radiusY, int step, Color color);

// The same ellipse, turned so its long side points along dir.
void turnedOval(Vec2 center, Vec2 dir, float radiusAlong, float radiusAcross, int step, Color color);

// Ring around an ellipse. width is measured on the wide side of the ellipse, and
// it shrinks by the same ratio on the short side, so it looks flat on the ground.
void ovalRing(Vec2 center, float radiusX, float radiusY, float width, int step, Color color);

// A flat shape lying on the ground, so it gets squashed by the tilt.
inline void groundOval(Vec2 viewCenter, float radius, int step, Color color) {
    oval(viewCenter, radius, radius * kTilt, step, color);
}
inline void groundRing(Vec2 viewCenter, float radius, float width, int step, Color color) {
    ovalRing(viewCenter, radius, radius * kTilt, width, step, color);
}

// Small repeatable scramble of a number, so anything that must look random comes
// from position and tick, never from the simulation's random number generator.
inline uint32_t scramble(uint32_t value) {
    value ^= value >> 15;
    value *= 0x2C1B3C6Du;
    value ^= value >> 12;
    value *= 0x297A2D39u;
    value ^= value >> 15;
    return value;
}

inline uint32_t hashPosition(Vec2 world) {
    return scramble(static_cast<uint32_t>(static_cast<int32_t>(world.x * 3.0f)) * 73856093u ^
                    static_cast<uint32_t>(static_cast<int32_t>(world.y * 3.0f)) * 19349663u);
}

}  // namespace ls::worldview
