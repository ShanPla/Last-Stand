#pragma once

#include "components/WorldView/Projection.h"
#include "components/WorldView/Sprites.h"
#include "core/Math.h"

#include "raylib.h"
#include "rlgl.h"

#include <cstdint>

// Small drawing helpers shared by the world renderer. Everything is emitted as quads
// inside rlBegin(RL_QUADS), so raylib keeps them all in as few draw calls as it can.
namespace ls::worldview {

inline Color fade(Color color, float amount) {
    color.a = static_cast<unsigned char>(static_cast<float>(color.a) * clampf(amount, 0.0f, 1.0f));
    return color;
}

// --- Flat shapes, drawn with no texture ---------------------------------------

inline void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color color) {
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex2f(a.x, a.y);
    rlVertex2f(b.x, b.y);
    rlVertex2f(c.x, c.y);
    rlVertex2f(d.x, d.y);
}

inline void gradientQuad(Vec2 a, Vec2 b, Color start, Vec2 c, Vec2 d, Color end) {
    rlColor4ub(start.r, start.g, start.b, start.a);
    rlVertex2f(a.x, a.y);
    rlVertex2f(b.x, b.y);
    rlColor4ub(end.r, end.g, end.b, end.a);
    rlVertex2f(c.x, c.y);
    rlVertex2f(d.x, d.y);
}

// A rectangle on whole art pixels, from its center. This is what pixel fire and smoke
// are made of: its edges are always crisp, however it moves.
inline void pixelBlock(float x, float y, float size, Color color) {
    const float half = static_cast<float>(static_cast<int>(size * 0.5f + 0.5f));
    const float left = static_cast<float>(static_cast<int>(x - half + 1000.5f) - 1000);
    const float top = static_cast<float>(static_cast<int>(y - half + 1000.5f) - 1000);
    const float side = half * 2.0f;
    quad(Vec2{left, top}, Vec2{left, top + side}, Vec2{left + side, top + side}, Vec2{left + side, top}, color);
}

inline Vec2 perpendicular(Vec2 dir) { return {-dir.y, dir.x}; }

inline void thickLine(Vec2 from, Vec2 to, float width, Color color) {
    const Vec2 side = perpendicular(normalizedOr(to - from, Vec2{1.0f, 0.0f})) * (width * 0.5f);
    quad(from + side, from - side, to - side, to + side, color);
}

// Flat shapes lying on the ground, projected point by point. A circle on the ground
// becomes the right egg shape on screen, even with the perspective.
inline constexpr int kCirclePoints = 48;
struct UnitCircle {
    Vec2 points[kCirclePoints];
    UnitCircle();
};
extern const UnitCircle g_circle;

// step picks every Nth point of the circle, and must divide kCirclePoints.
void groundDisc(const View& view, Vec2 center, float radius, int step, Color color);
void groundRing(const View& view, Vec2 center, float radius, float width, int step, Color color);

// --- Sprites, drawn with the palette shader -----------------------------------

inline constexpr float kAtlasTexel = 1.0f / static_cast<float>(kAtlasSize);

// Grade 255 means the time of day tints this sprite, and 0 means it stays true.
inline void spriteQuad(const SpriteRef& ref, int x, int y, bool mirror, uint8_t row, uint8_t grade = 255, uint8_t alpha = 255) {
    const float u0 = static_cast<float>(ref.x) * kAtlasTexel;
    const float v0 = static_cast<float>(ref.y) * kAtlasTexel;
    const float u1 = static_cast<float>(ref.x + ref.w) * kAtlasTexel;
    const float v1 = static_cast<float>(ref.y + ref.h) * kAtlasTexel;
    const int left = mirror ? x - (ref.w - ref.footX) : x - ref.footX;
    const float l = static_cast<float>(left);
    const float t = static_cast<float>(y - ref.footY);
    const float r = l + static_cast<float>(ref.w);
    const float b = t + static_cast<float>(ref.h);
    const float texLeft = mirror ? u1 : u0;
    const float texRight = mirror ? u0 : u1;
    rlColor4ub(row, grade, 0, alpha);
    rlTexCoord2f(texLeft, v0);
    rlVertex2f(l, t);
    rlTexCoord2f(texLeft, v1);
    rlVertex2f(l, b);
    rlTexCoord2f(texRight, v1);
    rlVertex2f(r, b);
    rlTexCoord2f(texRight, v0);
    rlVertex2f(r, t);
}

// A light pool: centered on its point, scaled with depth, with strength in its color.
inline void poolQuad(const SpriteRef& ref, float x, float y, float scale, uint8_t row, float strength) {
    const float u0 = static_cast<float>(ref.x) * kAtlasTexel;
    const float v0 = static_cast<float>(ref.y) * kAtlasTexel;
    const float u1 = static_cast<float>(ref.x + ref.w) * kAtlasTexel;
    const float v1 = static_cast<float>(ref.y + ref.h) * kAtlasTexel;
    const float halfW = static_cast<float>(ref.w) * 0.5f * scale;
    const float halfH = static_cast<float>(ref.h) * 0.5f * scale;
    rlColor4ub(row, 0, 255, static_cast<unsigned char>(clampf(strength, 0.0f, 1.0f) * 255.0f));
    rlTexCoord2f(u0, v0);
    rlVertex2f(x - halfW, y - halfH);
    rlTexCoord2f(u0, v1);
    rlVertex2f(x - halfW, y + halfH);
    rlTexCoord2f(u1, v1);
    rlVertex2f(x + halfW, y + halfH);
    rlTexCoord2f(u1, v0);
    rlVertex2f(x + halfW, y - halfH);
}

// A sprite's dark copy, lying on the ground and stretching away from a low sun.
inline void shadowQuad(const SpriteRef& ref, int x, int y, bool mirror, float offsetX, float offsetY) {
    const float u0 = static_cast<float>(ref.x) * kAtlasTexel;
    const float v0 = static_cast<float>(ref.y) * kAtlasTexel;
    const float u1 = static_cast<float>(ref.x + ref.w) * kAtlasTexel;
    const float v1 = static_cast<float>(ref.y + ref.h) * kAtlasTexel;
    const int left = mirror ? x - (ref.w - ref.footX) : x - ref.footX;
    const float l = static_cast<float>(left);
    const float r = l + static_cast<float>(ref.w);
    const float base = static_cast<float>(y);
    const float texLeft = mirror ? u1 : u0;
    const float texRight = mirror ? u0 : u1;
    rlColor4ub(kRowShadow, 0, 0, 255);
    rlTexCoord2f(texLeft, v0);
    rlVertex2f(l + offsetX, base + offsetY);
    rlTexCoord2f(texLeft, v1);
    rlVertex2f(l, base);
    rlTexCoord2f(texRight, v1);
    rlVertex2f(r, base);
    rlTexCoord2f(texRight, v0);
    rlVertex2f(r + offsetX, base + offsetY);
}

inline int roundToInt(float value) { return static_cast<int>(value + 100000.5f) - 100000; }

// --- Repeatable noise ---------------------------------------------------------

// A small scramble of a number, so anything that must look random comes from
// position and tick, never from the simulation's random number generator.
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

// A smooth 0 to 1 to 0 pulse from a counter, with no trigonometry per object.
inline float triangleWave(uint32_t counter, uint32_t period) {
    const uint32_t t = counter % period;
    const float half = static_cast<float>(period) * 0.5f;
    const float distance = static_cast<float>(t) - half;
    return (distance < 0.0f ? -distance : distance) / half;
}

}  // namespace ls::worldview
