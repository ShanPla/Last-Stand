#include "components/WorldView/Canvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ls::worldview {

namespace {

// The light comes from above and to the left, on screen.
constexpr float kLightX = -0.55f;
constexpr float kLightY = -0.83f;

// Big shapes would look like smooth vector art with flat bands alone. A checkerboard
// where bands meet, and a little grit, is how pixel artists shade large surfaces.
constexpr float kDitherFromRadius = 6.5f;
constexpr float kGritFromRadius = 9.0f;

float roughen(float light, int px, int py, float radius) {
    if (radius < kDitherFromRadius) return light;
    light += ((px + py) & 1) != 0 ? 0.07f : -0.07f;
    if (radius >= kGritFromRadius) {
        const uint32_t noise = (static_cast<uint32_t>(px) * 73856093u) ^ (static_cast<uint32_t>(py) * 19349663u);
        if (((noise * 0x9E3779B1u) >> 27) == 0u) light -= 0.45f;
    }
    return light;
}

uint8_t toneFor(float light, Ramp ramp, int shift) {
    // light runs from -1 (facing away) to 1 (facing the light).
    int tone;
    if (ramp.count >= 4) tone = light < -0.30f ? 0 : (light < 0.30f ? 1 : (light < 0.72f ? 2 : 3));
    else if (ramp.count == 3) tone = light < -0.25f ? 0 : (light < 0.50f ? 1 : 2);
    else if (ramp.count == 2) tone = light < 0.10f ? 0 : 1;
    else tone = 0;
    tone = std::clamp(tone + shift, 0, ramp.count - 1);
    return static_cast<uint8_t>(ramp.first + tone);
}

}  // namespace

void Canvas::begin(float scale) {
    // Only the part used by the last figure needs clearing. Thousands of frames are
    // built at startup, so whole-canvas passes would add up to real time.
    if (dirtyMaxX_ >= dirtyMinX_) {
        for (int y = dirtyMinY_; y <= dirtyMaxY_; ++y) {
            std::memset(pixels_ + y * kSize + dirtyMinX_, 0, static_cast<size_t>(dirtyMaxX_ - dirtyMinX_ + 1));
        }
    }
    dirtyMinX_ = kSize;
    dirtyMinY_ = kSize;
    dirtyMaxX_ = -1;
    dirtyMaxY_ = -1;
    scale_ = scale;
    clearFrame();
    contour_ = false;
}

void Canvas::setTurn(float radians) {
    cos_ = std::cos(radians);
    sin_ = std::sin(radians);
    frameX_ = 0.0f;
    frameY_ = 0.0f;
}

void Canvas::setFrame(float originX, float originY, float dirX, float dirY) {
    frameX_ = originX;
    frameY_ = originY;
    cos_ = dirX;
    sin_ = dirY;
}

void Canvas::clearFrame() {
    frameX_ = 0.0f;
    frameY_ = 0.0f;
    cos_ = 1.0f;
    sin_ = 0.0f;
}

void Canvas::mapPoint(float x, float y, float* outX, float* outY) const {
    const float wx = frameX_ + x * cos_ - y * sin_;
    const float wy = frameY_ + x * sin_ + y * cos_;
    *outX = static_cast<float>(originX()) + wx * scale_;
    *outY = static_cast<float>(originY()) - wy * scale_;
}

void Canvas::put(int x, int y, uint8_t index) {
    if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
    pixels_[y * kSize + x] = index;
    dirtyMinX_ = std::min(dirtyMinX_, x);
    dirtyMaxX_ = std::max(dirtyMaxX_, x);
    dirtyMinY_ = std::min(dirtyMinY_, y);
    dirtyMaxY_ = std::max(dirtyMaxY_, y);
}

void Canvas::darkenIfUsed(int x, int y) {
    if (x < 0 || y < 0 || x >= kSize || y >= kSize) return;
    uint8_t& pixel = pixels_[y * kSize + x];
    if (pixel != 0) pixel = 1;  // Palette position 1 is the outline color.
}

void Canvas::ball(float x, float y, float rx, float ry, Ramp ramp, int shift) {
    float cx, cy;
    mapPoint(x, y, &cx, &cy);
    const float radiusX = std::max(0.5f, rx * scale_);
    const float radiusY = std::max(0.5f, ry * scale_);
    const float rim = contour_ ? 1.0f : 0.0f;
    const int x0 = static_cast<int>(std::floor(cx - radiusX - rim));
    const int x1 = static_cast<int>(std::ceil(cx + radiusX + rim));
    const int y0 = static_cast<int>(std::floor(cy - radiusY - rim));
    const int y1 = static_cast<int>(std::ceil(cy + radiusY + rim));
    for (int py = y0; py < y1; ++py) {
        for (int px = x0; px < x1; ++px) {
            const float dx = static_cast<float>(px) + 0.5f - cx;
            const float dy = static_cast<float>(py) + 0.5f - cy;
            const float nx = dx / radiusX;
            const float ny = dy / radiusY;
            if (nx * nx + ny * ny > 1.0f) {
                if (contour_) {
                    const float ox = dx / (radiusX + rim);
                    const float oy = dy / (radiusY + rim);
                    if (ox * ox + oy * oy <= 1.0f) darkenIfUsed(px, py);
                }
                continue;
            }
            put(px, py, toneFor(roughen(nx * kLightX + ny * kLightY, px, py, std::min(radiusX, radiusY)), ramp, shift));
        }
    }
}

void Canvas::limb(float ax, float ay, float bx, float by, float ra, float rb, Ramp ramp, int shift) {
    float x0f, y0f, x1f, y1f;
    mapPoint(ax, ay, &x0f, &y0f);
    mapPoint(bx, by, &x1f, &y1f);
    const float radiusA = std::max(0.5f, ra * scale_);
    const float radiusB = std::max(0.5f, rb * scale_);
    const float big = std::max(radiusA, radiusB) + (contour_ ? 1.0f : 0.0f);
    const int left = static_cast<int>(std::floor(std::min(x0f, x1f) - big));
    const int right = static_cast<int>(std::ceil(std::max(x0f, x1f) + big));
    const int top = static_cast<int>(std::floor(std::min(y0f, y1f) - big));
    const int bottom = static_cast<int>(std::ceil(std::max(y0f, y1f) + big));
    const float ex = x1f - x0f;
    const float ey = y1f - y0f;
    const float lenSq = std::max(ex * ex + ey * ey, 1e-4f);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            const float dx = static_cast<float>(px) + 0.5f - x0f;
            const float dy = static_cast<float>(py) + 0.5f - y0f;
            const float t = std::clamp((dx * ex + dy * ey) / lenSq, 0.0f, 1.0f);
            const float qx = dx - ex * t;
            const float qy = dy - ey * t;
            const float radius = radiusA + (radiusB - radiusA) * t;
            const float distSq = qx * qx + qy * qy;
            if (distSq > radius * radius) {
                if (contour_) {
                    const float outer = radius + 1.0f;
                    if (distSq <= outer * outer) darkenIfUsed(px, py);
                }
                continue;
            }
            const float light = (qx * kLightX + qy * kLightY) / radius;
            put(px, py, toneFor(roughen(light, px, py, radius), ramp, shift));
        }
    }
}

void Canvas::box(float x0, float y0, float x1, float y1, Ramp ramp, int shift) {
    // Works by asking, for each pixel, where it falls in the frame. This way a
    // rotated gun part has clean edges and the same shading as a straight one.
    float cornersX[4];
    float cornersY[4];
    mapPoint(x0, y0, &cornersX[0], &cornersY[0]);
    mapPoint(x1, y0, &cornersX[1], &cornersY[1]);
    mapPoint(x1, y1, &cornersX[2], &cornersY[2]);
    mapPoint(x0, y1, &cornersX[3], &cornersY[3]);
    const int left = static_cast<int>(std::floor(std::min({cornersX[0], cornersX[1], cornersX[2], cornersX[3]}))) - 1;
    const int right = static_cast<int>(std::ceil(std::max({cornersX[0], cornersX[1], cornersX[2], cornersX[3]}))) + 1;
    const int top = static_cast<int>(std::floor(std::min({cornersY[0], cornersY[1], cornersY[2], cornersY[3]}))) - 1;
    const int bottom = static_cast<int>(std::ceil(std::max({cornersY[0], cornersY[1], cornersY[2], cornersY[3]}))) + 1;
    const float low = std::min(x0, x1);
    const float high = std::max(x0, x1);
    const float bottomY = std::min(y0, y1);
    const float topY = std::max(y0, y1);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            // Back from the target to frame coordinates.
            const float wx = (static_cast<float>(px) + 0.5f - static_cast<float>(originX())) / scale_ - frameX_;
            const float wy = (static_cast<float>(originY()) - static_cast<float>(py) - 0.5f) / scale_ - frameY_;
            const float u = wx * cos_ + wy * sin_;
            const float v = -wx * sin_ + wy * cos_;
            if (u < low || u > high || v < bottomY || v > topY) {
                if (contour_) {
                    const float margin = 1.0f / scale_;
                    if (u >= low - margin && u <= high + margin && v >= bottomY - margin && v <= topY + margin) darkenIfUsed(px, py);
                }
                continue;
            }
            const float midU = (low + high) * 0.5f;
            const float midV = (bottomY + topY) * 0.5f;
            const float halfU = std::max((high - low) * 0.5f, 0.5f);
            const float halfV = std::max((topY - bottomY) * 0.5f, 0.5f);
            // In frame space, so a held gun keeps its bright side up.
            const float light = (u - midU) / halfU * -0.35f + (v - midV) / halfV * 0.85f;
            put(px, py, toneFor(light, ramp, shift));
        }
    }
}

void Canvas::dot(float x, float y, uint8_t index) {
    float cx, cy;
    mapPoint(x, y, &cx, &cy);
    put(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)), index);
}

void Canvas::patch(float x, float y, float w, float h, uint8_t index) {
    float cx, cy;
    mapPoint(x, y, &cx, &cy);
    const int left = static_cast<int>(std::floor(cx));
    const int top = static_cast<int>(std::floor(cy));
    const int width = std::max(1, static_cast<int>(std::lround(w * scale_)));
    const int height = std::max(1, static_cast<int>(std::lround(h * scale_)));
    for (int py = top; py < top + height; ++py) {
        for (int px = left; px < left + width; ++px) put(px, py, index);
    }
}

void Canvas::erase(float x, float y) {
    float cx, cy;
    mapPoint(x, y, &cx, &cy);
    put(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy)), 0);
}

void Canvas::stamp(const char* const* rows, int rowCount, float x, float y, const uint8_t* table) {
    float cx, cy;
    mapPoint(x, y, &cx, &cy);
    const int left = static_cast<int>(std::floor(cx));
    const int top = static_cast<int>(std::floor(cy));
    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; rows[row][col] != '\0'; ++col) {
            const uint8_t index = table[static_cast<uint8_t>(rows[row][col]) & 127];
            if (index != 0) put(left + col, top + row, index);
        }
    }
}

void Canvas::outline(uint8_t index) {
    if (dirtyMaxX_ < dirtyMinX_) return;
    constexpr uint8_t kMark = 255;  // New outline pixels must not grow the outline further.
    const int left = std::max(1, dirtyMinX_ - 1);
    const int right = std::min(kSize - 2, dirtyMaxX_ + 1);
    const int top = std::max(1, dirtyMinY_ - 1);
    const int bottom = std::min(kSize - 2, dirtyMaxY_ + 1);
    const auto used = [](uint8_t pixel) { return pixel != 0 && pixel != kMark; };
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            const int at = y * kSize + x;
            if (pixels_[at] != 0) continue;
            if (used(pixels_[at - 1]) || used(pixels_[at + 1]) || used(pixels_[at - kSize]) || used(pixels_[at + kSize])) pixels_[at] = kMark;
        }
    }
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            if (pixels_[y * kSize + x] == kMark) put(x, y, index);
        }
    }
}

bool Canvas::bounds(int* minX, int* minY, int* maxX, int* maxY) const {
    if (dirtyMaxX_ < dirtyMinX_) return false;
    *minX = dirtyMinX_;
    *minY = dirtyMinY_;
    *maxX = dirtyMaxX_;
    *maxY = dirtyMaxY_;
    return true;
}

}  // namespace ls::worldview
