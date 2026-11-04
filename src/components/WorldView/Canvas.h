#pragma once

#include <cstdint>

// A small software painter for sprites. It works in art pixels with y pointing up
// from the figure's feet, and writes palette positions rather than colors. Shapes are
// shaded in hard bands from a light above and to the left, which is what makes
// simple ellipses and limbs read as pixel art instead of flat vector shapes.
namespace ls::worldview {

// A run of palette positions, from the darkest tone to the lightest.
struct Ramp {
    uint8_t first;
    uint8_t count;
};

class Canvas {
public:
    static constexpr int kSize = 256;

    // scale is the size of this variant. Hand-placed details stay 1 pixel wide,
    // so a small variant of a figure keeps its crisp outline.
    void begin(float scale);

    // Turns everything drawn next around the feet, for figures falling over.
    void setTurn(float radians);
    // Moves and rotates the drawing frame, for guns held at an angle.
    void setFrame(float originX, float originY, float dirX, float dirY);
    void clearFrame();

    // When on, each shape first darkens a 1 pixel rim over whatever is under it,
    // so an arm reads clearly in front of the torso.
    void setContour(bool on) { contour_ = on; }

    void ball(float x, float y, float rx, float ry, Ramp ramp, int shift = 0);
    void limb(float ax, float ay, float bx, float by, float ra, float rb, Ramp ramp, int shift = 0);
    void box(float x0, float y0, float x1, float y1, Ramp ramp, int shift = 0);
    void dot(float x, float y, uint8_t index);
    // A filled square of whole art pixels, its size scaled with the variant.
    void patch(float x, float y, float w, float h, uint8_t index);
    // Clears pixels, for ragged hems and holes.
    void erase(float x, float y);

    // Stamps a small picture, one string per row, at a position in the frame.
    // Characters map through table, where a value of 0 leaves the pixel alone.
    void stamp(const char* const* rows, int rowCount, float x, float y, const uint8_t* table);

    // Adds the outer outline: empty pixels touching the figure become this color.
    void outline(uint8_t index);

    // Raw access, for packing into the atlas.
    const uint8_t* pixels() const { return pixels_; }
    bool bounds(int* minX, int* minY, int* maxX, int* maxY) const;
    int originX() const { return kSize / 2; }
    int originY() const { return kSize - 28; }
    float scale() const { return scale_; }

private:
    void mapPoint(float x, float y, float* outX, float* outY) const;
    void put(int x, int y, uint8_t index);
    void darkenIfUsed(int x, int y);

    uint8_t pixels_[kSize * kSize] = {};
    float scale_ = 1.0f;
    float frameX_ = 0.0f;
    float frameY_ = 0.0f;
    float cos_ = 1.0f;
    float sin_ = 0.0f;
    bool contour_ = false;
    int dirtyMinX_ = kSize;
    int dirtyMinY_ = kSize;
    int dirtyMaxX_ = -1;
    int dirtyMaxY_ = -1;
};

}  // namespace ls::worldview
