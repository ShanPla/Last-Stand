#include "components/WorldView/Rig.h"

#include "rlgl.h"

#include <cmath>

namespace ls::worldview {

namespace {

constexpr int kMaxPolygonPoints = 16;
constexpr float kJointOverlap = 0.5f;  // Fills run past their ends a little, so bent joints have no gaps.

Vec2 side(Vec2 dir) { return {-dir.y, dir.x}; }

}  // namespace

void emitQuads(const FigureQuad* quads, int count, const Placement& place, const Palette& palette) {
    for (int i = 0; i < count; ++i) {
        const FigureQuad& q = quads[i];
        const Color color = palette.colors[q.layer];
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (const Vec2& p : q.corners) {
            // Figure y points up and screen y points down, which is why it is subtracted.
            rlVertex2f(place.x + p.x * place.scaleX + p.y * place.shear, place.y - p.y * place.scaleY);
        }
    }
}

Rig::Rig(FigureQuad* out, int capacity, float outline) : out_(out), capacity_(capacity), outline_(outline) {}

void Rig::addTo(FigureQuad* list, int& size, Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint8_t layer) {
    if (size >= kMaxGroupQuads) return;
    FigureQuad& q = list[size++];
    q.corners[0] = a;
    q.corners[1] = b;
    q.corners[2] = c;
    q.corners[3] = d;
    q.layer = layer;
}

void Rig::limb(Vec2 from, Vec2 to, float widthFrom, float widthTo) {
    const Vec2 dir = normalizedOr(to - from, Vec2{1.0f, 0.0f});
    const Vec2 normal = side(dir);
    // The fill runs past each end to close the gap at a bent joint. The outline runs
    // further still, so a limb's end is always capped by a bright line.
    const float overlapFrom = widthFrom * kJointOverlap;
    const float overlapTo = widthTo * kJointOverlap;
    const Vec2 outFrom = from - dir * (overlapFrom + outline_);
    const Vec2 outTo = to + dir * (overlapTo + outline_);
    const float outlineFrom = widthFrom + outline_;
    const float outlineTo = widthTo + outline_;
    addTo(outlines_, outlineCount_, outFrom + normal * outlineFrom, outFrom - normal * outlineFrom, outTo - normal * outlineTo,
          outTo + normal * outlineTo, kLayerOutline);
    const Vec2 fillFrom = from - dir * overlapFrom;
    const Vec2 fillTo = to + dir * overlapTo;
    addTo(fills_, fillCount_, fillFrom + normal * widthFrom, fillFrom - normal * widthFrom, fillTo - normal * widthTo,
          fillTo + normal * widthTo, kLayerFill);
}

void Rig::hull(const Vec2* points, int count) {
    Vec2 center{};
    for (int i = 0; i < count; ++i) center += points[i];
    center *= 1.0f / static_cast<float>(count);

    Vec2 grown[kMaxPolygonPoints];
    for (int i = 0; i < count; ++i) grown[i] = points[i] + normalizedOr(points[i] - center, Vec2{}) * (outline_ * 1.25f);

    // A fan of quads from the first corner. An odd count ends in a triangle,
    // which is a quad with two corners in the same place.
    for (int i = 1; i + 1 < count; i += 2) {
        const int last = i + 2 < count ? i + 2 : i + 1;
        addTo(outlines_, outlineCount_, grown[0], grown[i], grown[i + 1], grown[last], kLayerOutline);
        addTo(fills_, fillCount_, points[0], points[i], points[i + 1], points[last], kLayerFill);
    }
}

void Rig::blob(Vec2 center, float radiusX, float radiusY, int sides, float turn) {
    Vec2 points[kMaxPolygonPoints];
    for (int i = 0; i < sides; ++i) {
        const Vec2 unit = fromAngle(turn + kTau * static_cast<float>(i) / static_cast<float>(sides));
        points[i] = center + Vec2{unit.x * radiusX, unit.y * radiusY};
    }
    hull(points, sides);
}

void Rig::spike(Vec2 base, Vec2 tip, float halfWidth) {
    const Vec2 dir = normalizedOr(tip - base, Vec2{1.0f, 0.0f});
    const Vec2 normal = side(dir) * halfWidth;
    const Vec2 outNormal = side(dir) * (halfWidth + outline_);
    const Vec2 outTip = tip + dir * (outline_ * 2.2f);
    addTo(outlines_, outlineCount_, base + outNormal, base - outNormal, outTip, outTip, kLayerOutline);
    addTo(fills_, fillCount_, base + normal, base - normal, tip, tip, kLayerFill);
}

void Rig::detailLimb(Vec2 from, Vec2 to, float widthFrom, float widthTo, Layer layer) {
    const Vec2 normal = side(normalizedOr(to - from, Vec2{1.0f, 0.0f}));
    addTo(details_, detailCount_, from + normal * widthFrom, from - normal * widthFrom, to - normal * widthTo, to + normal * widthTo, layer);
}

void Rig::detailBlob(Vec2 center, float radiusX, float radiusY, int sides, Layer layer) {
    Vec2 points[kMaxPolygonPoints];
    for (int i = 0; i < sides; ++i) {
        const Vec2 unit = fromAngle(kTau * static_cast<float>(i) / static_cast<float>(sides));
        points[i] = center + Vec2{unit.x * radiusX, unit.y * radiusY};
    }
    for (int i = 1; i + 1 < sides; i += 2) {
        const int last = i + 2 < sides ? i + 2 : i + 1;
        addTo(details_, detailCount_, points[0], points[i], points[i + 1], points[last], layer);
    }
}

void Rig::endGroup() {
    const auto copy = [this](const FigureQuad* list, int size) {
        for (int i = 0; i < size && used_ < capacity_; ++i) out_[used_++] = list[i];
    };
    copy(outlines_, outlineCount_);
    copy(fills_, fillCount_);
    copy(details_, detailCount_);
    outlineCount_ = 0;
    fillCount_ = 0;
    detailCount_ = 0;
}

}  // namespace ls::worldview
