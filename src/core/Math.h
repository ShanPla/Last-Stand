#pragma once

#include <cmath>

namespace ls {

// 2D vector used by the simulation. The simulation has its own type, rather than
// raylib's, so it can run with no window or graphics library.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kTau = 2.0f * kPi;

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { a.x += b.x; a.y += b.y; return a; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline Vec2& operator*=(Vec2& a, float s) { a.x *= s; a.y *= s; return a; }

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float lengthSq(Vec2 a) { return dot(a, a); }
inline float length(Vec2 a) { return std::sqrt(lengthSq(a)); }
inline float distanceSq(Vec2 a, Vec2 b) { return lengthSq(a - b); }
inline float distance(Vec2 a, Vec2 b) { return length(a - b); }

// Returns fallback when the vector is too short to have a direction.
inline Vec2 normalizedOr(Vec2 a, Vec2 fallback) {
    const float lenSq = lengthSq(a);
    if (lenSq < 1e-8f) return fallback;
    return a * (1.0f / std::sqrt(lenSq));
}

inline Vec2 fromAngle(float radians) { return {std::cos(radians), std::sin(radians)}; }
inline float clampf(float v, float low, float high) { return v < low ? low : (v > high ? high : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline Vec2 lerp(Vec2 a, Vec2 b, float t) { return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t)}; }

}  // namespace ls
