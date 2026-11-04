#include "components/WorldView/Draw.h"

namespace ls::worldview {

UnitCircle::UnitCircle() {
    for (int i = 0; i < kCirclePoints; ++i) {
        points[i] = fromAngle(kTau * static_cast<float>(i) / static_cast<float>(kCirclePoints));
    }
}

const UnitCircle g_circle;

void oval(Vec2 center, float radiusX, float radiusY, int step, Color color) {
    const auto at = [&](int index) {
        const Vec2 unit = g_circle.points[index];
        return Vec2{center.x + unit.x * radiusX, center.y + unit.y * radiusY};
    };
    // A fan of quads from one rim point: each quad covers two rim segments, so an
    // eight sided shape costs only three quads.
    const Vec2 first = at(0);
    for (int i = step; i + step < kCirclePoints; i += step * 2) {
        const int last = i + step * 2;
        quad(first, at(i), at(i + step), last < kCirclePoints ? at(last) : first, color);
    }
}

void turnedOval(Vec2 center, Vec2 dir, float radiusAlong, float radiusAcross, int step, Color color) {
    const Vec2 side = perpendicular(dir);
    const auto at = [&](int index) {
        const Vec2 unit = g_circle.points[index];
        return center + dir * (unit.x * radiusAlong) + side * (unit.y * radiusAcross);
    };
    const Vec2 first = at(0);
    for (int i = step; i + step < kCirclePoints; i += step * 2) {
        const int last = i + step * 2;
        quad(first, at(i), at(i + step), last < kCirclePoints ? at(last) : first, color);
    }
}

void ovalRing(Vec2 center, float radiusX, float radiusY, float width, int step, Color color) {
    const float innerX = radiusX - width * 0.5f;
    const float outerX = radiusX + width * 0.5f;
    const float innerY = radiusY - width * 0.5f * (radiusY / radiusX);
    const float outerY = radiusY + width * 0.5f * (radiusY / radiusX);
    for (int i = 0; i < kCirclePoints; i += step) {
        const Vec2 a = g_circle.points[i];
        const Vec2 b = g_circle.points[(i + step) % kCirclePoints];
        quad(Vec2{center.x + a.x * innerX, center.y + a.y * innerY}, Vec2{center.x + a.x * outerX, center.y + a.y * outerY},
             Vec2{center.x + b.x * outerX, center.y + b.y * outerY}, Vec2{center.x + b.x * innerX, center.y + b.y * innerY}, color);
    }
}

}  // namespace ls::worldview
