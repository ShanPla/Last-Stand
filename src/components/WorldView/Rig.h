#pragma once

#include "core/Math.h"

#include "raylib.h"

#include <cstdint>

// A figure is a list of quads, built from simple body parts. Each part is added
// twice: a bigger copy in the outline color, and a smaller one in the fill color.
// Drawing outlines first and fills on top merges the parts into one neon silhouette.
namespace ls::worldview {

enum Layer : uint8_t { kLayerOutline, kLayerFill, kLayerAccent, kLayerHot, kLayerCount };

// Corners are in figure space: feet at the origin, x forward, y up.
struct FigureQuad {
    Vec2 corners[4];
    uint8_t layer = kLayerFill;
};

// The color for each layer. Swapping a palette is how a figure flashes.
struct Palette {
    Color colors[kLayerCount];
};

// Where a figure goes on screen. A shear leans it without any trigonometry.
struct Placement {
    float x, y;
    float scaleX;  // Negative mirrors the figure to face left.
    float scaleY;
    float shear = 0.0f;  // Screen units sideways, for each figure unit of height.
};

void emitQuads(const FigureQuad* quads, int count, const Placement& place, const Palette& palette);

// Collects body parts in groups. A later group overlaps an earlier one, with its
// own outline showing, so an arm can read clearly in front of the torso.
class Rig {
public:
    // outline is the outline width, in figure units.
    Rig(FigureQuad* out, int capacity, float outline);

    // A straight tapered part, such as a limb. Widths are half widths.
    void limb(Vec2 from, Vec2 to, float widthFrom, float widthTo);
    // A regular polygon. An even number of sides costs the fewest quads.
    void blob(Vec2 center, float radiusX, float radiusY, int sides, float turn = 0.0f);
    // A convex polygon, listed in order around its edge.
    void hull(const Vec2* points, int count);
    // A triangle, for horns, claws and spines. Its outline keeps the tip sharp.
    void spike(Vec2 base, Vec2 tip, float halfWidth);

    // Details with no outline, drawn on top of the group's fills.
    void detailLimb(Vec2 from, Vec2 to, float widthFrom, float widthTo, Layer layer = kLayerAccent);
    void detailBlob(Vec2 center, float radiusX, float radiusY, int sides, Layer layer = kLayerAccent);

    void endGroup();
    int count() const { return used_; }

private:
    static constexpr int kMaxGroupQuads = 48;

    void addTo(FigureQuad* list, int& size, Vec2 a, Vec2 b, Vec2 c, Vec2 d, uint8_t layer);

    FigureQuad* out_;
    int capacity_;
    int used_ = 0;
    float outline_;
    FigureQuad outlines_[kMaxGroupQuads];
    FigureQuad fills_[kMaxGroupQuads];
    FigureQuad details_[kMaxGroupQuads];
    int outlineCount_ = 0;
    int fillCount_ = 0;
    int detailCount_ = 0;
};

}  // namespace ls::worldview
