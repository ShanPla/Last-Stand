#pragma once

#include "components/Maps/FlatCar/FlatCar.h"
#include "components/Maps/Layout/Layout.h"
#include "components/Maps/Maps.h"

#include "raylib.h"

#include <algorithm>
#include <cstdint>

// Drawing tools shared by the maps. All coordinates are global texels of the whole map, not
// local to a chunk, and everything clips to the chunk being painted. That is what
// makes seams invisible: a car over a chunk edge is simply drawn by both chunks.
namespace ls::maps {

// The alpha a painted pixel carries. See CarMark and CoverMark. Every value in between
// belongs to a standing object, and says how far below the pixel its foot is.
inline constexpr unsigned char kPlainGround = 255;
inline constexpr unsigned char kCarBody = 0;
inline constexpr unsigned char kCoverFoot = 1;    // Alpha of a pixel on its object's foot row.
inline constexpr int kCoverMostRows = 253;        // The tallest reach that fits under kPlainGround.

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
        color.a = covering_ ? coverAlpha(lx, ly) : marking_;
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

    // Where to list the standing objects painted, or nullptr to keep no list.
    void setCovers(CoverList* covers) {
        covers_ = covers;
        if (covers_) covers_->count = 0;
    }

    // See CoverMark. footY is the ground row the object stands on, in map texels.
    void beginCover(int footX, int footY, CoverKind kind) {
        endCover();
        covering_ = true;
        coverFoot_ = footY;
        open_ = Cover{0, 0, 0, 0, static_cast<int16_t>(footX - originX_), 0, kind};
        openEmpty_ = true;
    }
    // Something that runs up the screen, like a side wall, stands on a different row at each
    // part, so its painter moves the foot as it goes.
    void setCoverFoot(int footY) { coverFoot_ = footY; }
    // Safe to call twice: only an open cover is listed.
    void endCover() {
        if (!covering_) return;
        covering_ = false;
        if (!covers_ || openEmpty_) return;
        if (covers_->count < kMaxCoversPerChunk) {
            covers_->items[covers_->count++] = open_;
            return;
        }
        // Full: the last entry grows to hold this one too, so it still shows through.
        Cover& last = covers_->items[kMaxCoversPerChunk - 1];
        last.x0 = std::min(last.x0, open_.x0);
        last.y0 = std::min(last.y0, open_.y0);
        last.x1 = std::max(last.x1, open_.x1);
        last.y1 = std::max(last.y1, open_.y1);
        last.nearRow = std::max(last.nearRow, open_.nearRow);
    }

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
    // A standing object's pixel keeps how many rows below it its foot is. The renderer turns
    // that into the depth of the foot, so a figure further back is hidden by it, pixel by pixel.
    unsigned char coverAlpha(int lx, int ly) {
        const int foot = std::max(coverFoot_ - originY_, ly);
        const int rows = std::min(foot - ly, kCoverMostRows);
        const auto x = static_cast<int16_t>(lx);
        const auto y = static_cast<int16_t>(ly);
        const auto near = static_cast<int16_t>(ly + rows);
        if (openEmpty_) {
            open_.x0 = x;
            open_.x1 = static_cast<int16_t>(x + 1);
            open_.y0 = y;
            open_.y1 = static_cast<int16_t>(y + 1);
            open_.nearRow = near;
            openEmpty_ = false;
        } else {
            open_.x0 = std::min(open_.x0, x);
            open_.x1 = std::max(open_.x1, static_cast<int16_t>(x + 1));
            open_.y0 = std::min(open_.y0, y);
            open_.y1 = std::max(open_.y1, static_cast<int16_t>(y + 1));
            open_.nearRow = std::max(open_.nearRow, near);
        }
        return static_cast<unsigned char>(kCoverFoot + rows);
    }

    Color* pixels_;
    int originX_;
    int originY_;
    int width_;
    int height_;
    unsigned char marking_ = kPlainGround;
    CrushedCars crushed_;
    CoverList* covers_ = nullptr;
    bool covering_ = false;
    bool openEmpty_ = true;
    int coverFoot_ = 0;
    Cover open_{};
};

// A color as seen on a face turned toward or away from the light, in percent of its brightness.
// The light comes from the upper left, the way the sun's shadows lean, so tops and left sides
// are lit and right sides are in shade. Only theme colors go in, so the palette stays in one file.
inline Color shade(Color color, int percent) {
    const auto scaled = [percent](unsigned char value) { return static_cast<unsigned char>(std::min(255, value * percent / 100)); };
    return Color{scaled(color.r), scaled(color.g), scaled(color.b), color.a};
}

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

// While one of these exists, what is painted is part of a standing object, like a lamp or a
// wall, whose foot is at row y. Each pixel keeps in its spare alpha how far above that foot it
// is, and the chunk lists where the object is. Shadows and flat parts are painted outside it,
// as they lie on the ground and must cover nothing. It only marks, so the colors and the
// random numbers used stay exactly what they would be without it.
class CoverMark {
public:
    CoverMark(Paint& paint, int x, int y, CoverKind kind) : paint_(paint) { paint_.beginCover(x, y, kind); }
    ~CoverMark() { paint_.endCover(); }
    CoverMark(const CoverMark&) = delete;
    CoverMark& operator=(const CoverMark&) = delete;

private:
    Paint& paint_;
};

}  // namespace ls::maps
