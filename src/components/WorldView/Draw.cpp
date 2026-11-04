#include "components/WorldView/Draw.h"

namespace ls::worldview {

UnitCircle::UnitCircle() {
    for (int i = 0; i < kCirclePoints; ++i) {
        points[i] = fromAngle(kTau * static_cast<float>(i) / static_cast<float>(kCirclePoints));
    }
}

const UnitCircle g_circle;

namespace {

Vec2 onGround(const View& view, Vec2 world) {
    const ScreenPoint point = project(view, world);
    return {point.x, point.y};
}

}  // namespace

void groundDisc(const View& view, Vec2 center, float radius, int step, Color color) {
    const Vec2 first = onGround(view, center + g_circle.points[0] * radius);
    // A fan of quads from one rim point: each quad covers two rim segments.
    for (int i = step; i + step < kCirclePoints; i += step * 2) {
        const int last = i + step * 2;
        quad(first, onGround(view, center + g_circle.points[i] * radius), onGround(view, center + g_circle.points[i + step] * radius),
             last < kCirclePoints ? onGround(view, center + g_circle.points[last] * radius) : first, color);
    }
}

void groundRing(const View& view, Vec2 center, float radius, float width, int step, Color color) {
    const float inner = radius - width * 0.5f;
    const float outer = radius + width * 0.5f;
    for (int i = 0; i < kCirclePoints; i += step) {
        const Vec2 a = g_circle.points[i];
        const Vec2 b = g_circle.points[(i + step) % kCirclePoints];
        quad(onGround(view, center + a * inner), onGround(view, center + a * outer), onGround(view, center + b * outer),
             onGround(view, center + b * inner), color);
    }
}

}  // namespace ls::worldview
