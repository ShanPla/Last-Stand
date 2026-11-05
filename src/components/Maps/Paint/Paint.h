#pragma once

#include "components/Maps/FlatCar/FlatCar.h"
#include "components/Maps/Layout/Layout.h"

#include "raylib.h"

#include <cstdint>

// Drawing tools shared by the maps. All coordinates are global texels of the whole map, not
// local to a chunk, and everything clips to the chunk being painted. That is what
// makes seams invisible: a car over a chunk edge is simply drawn by both chunks.
namespace ls::maps {

// The alpha a painted pixel carries. See CarMark.
inline constexpr unsigned char kPlainGround = 255;
inline constexpr unsigned char kCarBody = 0;

class Paint {
public:
    Paint(Color* pixels, int originX, int originY, int width, int height)
        : pixels_(pixels), originX_(originX), originY_(originY), width_(width), height_(height) {}

    int left() const { return originX_; }
    int top() const { return originY_; }
    int right() const { return originX_ + width_; }
    int bottom() const { return originY_ + height_; }

    bool overlaps(int x0, int y0, int x1, int y1) const { return x1 >= left() && x0 < right() && y1 >= top() && y0 < bottom(); }

    void put(int x, int y, Color color) {
        const int lx = x - originX_;
        const int ly = y - originY_;
        if (lx < 0 || ly < 0 || lx >= width_ || ly >= height_) return;
        color.a = marking_;
        pixels_[ly * width_ + lx] = color;
    }

    // Mixes a see-through color over what is already there.
    void blend(int x, int y, Color color) {
        const int lx = x - originX_;
        const int ly = y - originY_;
        if (lx < 0 || ly < 0 || lx >= width_ || ly >= height_) return;
        Color& to = pixels_[ly * width_ + lx];
        const int a = color.a;
        to.r = static_cast<unsigned char>((color.r * a + to.r * (255 - a)) / 255);
        to.g = static_cast<unsigned char>((color.g * a + to.g * (255 - a)) / 255);
        to.b = static_cast<unsigned char>((color.b * a + to.b * (255 - a)) / 255);
    }

    // A rectangle from (x0, y0) up to but not including (x1, y1).
    void rect(int x0, int y0, int x1, int y1, Color color) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) put(x, y, color);
        }
    }

    void blendRect(int x0, int y0, int x1, int y1, Color color) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) blend(x, y, color);
        }
    }

    void line(int x0, int y0, int x1, int y1, Color color) {
        int dx = x1 > x0 ? x1 - x0 : x0 - x1;
        int dy = y1 > y0 ? y0 - y1 : y1 - y0;
        const int stepX = x0 < x1 ? 1 : -1;
        const int stepY = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true) {
            put(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            const int twice = 2 * error;
            if (twice >= dy) {
                error += dy;
                x0 += stepX;
            }
            if (twice <= dx) {
                error += dx;
                y0 += stepY;
            }
        }
    }

    // A filled ellipse, flattened to sit on the ground.
    void oval(int cx, int cy, int rx, int ry, Color color) {
        for (int y = -ry; y <= ry; ++y) {
            for (int x = -rx; x <= rx; ++x) {
                if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry) put(cx + x, cy + y, color);
            }
        }
    }

    void blendOval(int cx, int cy, int rx, int ry, Color color) {
        for (int y = -ry; y <= ry; ++y) {
            for (int x = -rx; x <= rx; ++x) {
                if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry) blend(cx + x, cy + y, color);
            }
        }
    }

    void setMarking(unsigned char alpha) { marking_ = alpha; }

    // The cars the boss has flattened, so the car painters can tell which ones to flatten.
    void setCrushed(CrushedCars crushed) { crushed_ = crushed; }
    const CrushedCars& crushed() const { return crushed_; }

    // Where this paints. FlatCar borrows this to catch a car's picture before it reaches the
    // chunk, with no change to the painters themselves.
    struct Surface {
        Color* pixels;
        int originX;
        int originY;
        int width;
        int height;
    };
    Surface surface() const { return {pixels_, originX_, originY_, width_, height_}; }
    void setSurface(const Surface& surface) {
        pixels_ = surface.pixels;
        originX_ = surface.originX;
        originY_ = surface.originY;
        width_ = surface.width;
        height_ = surface.height;
    }

private:
    Color* pixels_;
    int originX_;
    int originY_;
    int width_;
    int height_;
    unsigned char marking_ = kPlainGround;
    CrushedCars crushed_;
};

// While one of these exists, what is painted counts as the body of a solid car. The ground
// never shows its alpha, so that spare value can say which pixels are car. The renderer
// uses it to let a car cover whatever stands behind it, with no second copy of the car.
//
// It is also where a crushed car goes flat. Its painter runs as usual and uses up the same
// random numbers, so nothing painted after it changes. Only the picture goes somewhere else
// first, and what lands on the chunk is the flat version, with no marks, so it covers nothing.
class CarMark {
public:
    CarMark(Paint& paint, CarSprite sprite, int x, int y) : paint_(paint), sprite_(sprite), x_(x), y_(y), flat_(paint.crushed().holds(x, y)) {
        paint_.setMarking(kCarBody);
        if (flat_) beginFlatCar(paint_, sprite_, x_, y_);
    }
    ~CarMark() {
        paint_.setMarking(kPlainGround);
        if (flat_) finishFlatCar(paint_, sprite_, x_, y_);
    }
    CarMark(const CarMark&) = delete;
    CarMark& operator=(const CarMark&) = delete;

private:
    Paint& paint_;
    CarSprite sprite_;
    int x_;
    int y_;
    bool flat_;
};

}  // namespace ls::maps
