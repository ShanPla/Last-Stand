#include "components/Maps/FlatCar/FlatCar.h"

#include "components/Maps/Paint/Paint.h"
#include "data/Theme.h"

#include <algorithm>

namespace ls::maps {

namespace {

// Roomy enough for the longest wreck, a tanker of 175 texels with its strays, and the tallest,
// a bus seen from behind of 91 texels. Sand heaped against a wreck strays past its picture.
constexpr int kStrayX = 26;
constexpr int kStrayY = 10;
constexpr int kPictureWidth = 200;
constexpr int kPictureHeight = 120;

// The private picture. A pixel's alpha says if the car painted it, the same as in a chunk.
Color g_picture[kPictureWidth * kPictureHeight];
Paint::Surface g_chunkSurface{nullptr, 0, 0, 0, 0};
uint32_t* g_probe = nullptr;

// How low each picture ends up, and how far it spreads sideways when pressed. A car seen from
// the side loses nearly all its height. One seen from behind runs away up the screen along
// the ground, so it keeps its length and only loses the height of its back end.
struct FlatShape {
    int rows;
    int spread;
};

FlatShape shapeOf(CarSprite sprite) {
    switch (sprite) {
        case CarSprite::CitySide:
        case CarSprite::WreckSide: return {11, 3};
        case CarSprite::WreckOnRoof: return {8, 3};
        case CarSprite::BusSide: return {14, 3};
        case CarSprite::TankerSide: return {13, 3};
        case CarSprite::CityEnd:
        case CarSprite::WreckEnd: return {30, 3};
        case CarSprite::BusEnd: return {46, 3};
    }
    return {11, 3};
}

Color shaded(Color color, int percent) {
    return Color{static_cast<unsigned char>(std::min(255, color.r * percent / 100)), static_cast<unsigned char>(std::min(255, color.g * percent / 100)),
                 static_cast<unsigned char>(std::min(255, color.b * percent / 100)), color.a};
}

}  // namespace

void setFlatCarProbe(uint32_t* out) { g_probe = out; }

TexelRect flatCarReach(const Obstacle& obstacle) {
    const TexelRect rect = spriteRect(obstacle);
    return {rect.x0 - kStrayX, rect.y0 - kStrayY, rect.x1 + kStrayX, rect.y1 + kStrayY};
}

void beginFlatCar(Paint& paint, CarSprite sprite, int x, int y) {
    const TexelRect reach = flatCarReach(makeObstacle(sprite, x, y));
    g_chunkSurface = paint.surface();
    // Alpha at plain ground means nothing has been painted here. A see-through shadow only
    // mixes colors and leaves alpha alone, so a car's shadow never makes it into the picture.
    std::fill(g_picture, g_picture + kPictureWidth * kPictureHeight, Color{0, 0, 0, kPlainGround});
    const int centerX = (reach.x0 + reach.x1) / 2;
    paint.setSurface({g_picture, centerX - kPictureWidth / 2, reach.y1 - kPictureHeight, kPictureWidth, kPictureHeight});
}

void finishFlatCar(Paint& paint, CarSprite sprite, int x, int y) {
    const Paint::Surface picture = paint.surface();
    paint.setSurface(g_chunkSurface);

    if (g_probe) {
        uint32_t sum = 2166136261u;
        for (const Color& color : g_picture) {
            sum = (sum ^ color.r ^ (static_cast<uint32_t>(color.g) << 8) ^ (static_cast<uint32_t>(color.b) << 16) ^ (static_cast<uint32_t>(color.a) << 24)) * 16777619u;
        }
        *g_probe = sum;
    }

    const auto at = [&](int px, int py) -> const Color& { return g_picture[(py - picture.originY) * kPictureWidth + (px - picture.originX)]; };

    // Each column of the car is pressed on its own, from its own top and bottom down to the
    // ground. So a tilted wreck comes out level, and a cabin still rises a little above the
    // hood, which keeps the shape of the car it was.
    const TexelRect rect = spriteRect(makeObstacle(sprite, x, y));
    const FlatShape shape = shapeOf(sprite);
    const int halfBefore = (rect.x1 - rect.x0) / 2;
    const int halfAfter = halfBefore + shape.spread;
    const int feet = y + 1;

    // The rows a column of the car covers. Map rows can be negative, so "nothing here" needs
    // its own flag and not a special row number.
    struct Span {
        bool found = false;
        int top = 0;
        int bottom = 0;
    };
    const auto spanOf = [&](int px) {
        Span span;
        for (int py = picture.originY; py < picture.originY + kPictureHeight; ++py) {
            if (at(px, py).a != kCarBody) continue;
            if (!span.found) span.top = py;
            span.found = true;
            span.bottom = py;
        }
        return span;
    };

    int tallest = 1;
    for (int px = rect.x0; px < rect.x1; ++px) {
        const Span span = spanOf(px);
        if (span.found) tallest = std::max(tallest, span.bottom - span.top + 1);
    }

    for (int tx = x - halfAfter; tx <= x + halfAfter; ++tx) {
        // Sampled backward, from where each new pixel comes from, so pressing leaves no holes.
        const int px = x + (tx - x) * halfBefore / halfAfter;
        const Span span = spanOf(px);
        if (!span.found) continue;
        const uint32_t noise = texelNoise(tx >> 1, y);
        const int tall = span.bottom - span.top + 1;
        // Mostly one even plate, with what used to be the cabin rising a little above the rest,
        // and a torn top edge.
        const int rows = std::max(3, shape.rows * (tallest + 2 * tall) / (3 * tallest) - static_cast<int>(noise & 1u) - static_cast<int>((noise >> 1) & 1u));
        // A fold in the metal runs across the body every so often, leaning as it goes.
        const int foldGap = 13 + static_cast<int>(texelNoise(x, y) & 3u);
        for (int row = 0; row < rows; ++row) {
            const int py = span.bottom - (row * tall + tall / 2) / rows;
            const Color source = at(px, py);
            if (source.a != kCarBody) continue;
            // Map columns can be negative too, and % keeps the sign, hence the second round.
            const int fold = ((tx + row / 2) % foldGap + foldGap) % foldGap;
            int tone = 86;
            if (fold == 0) tone = 58;
            else if (fold == 1) tone = 102;
            // A lit top edge and a dark bottom edge show how thin it is now.
            if (row == rows - 1) tone = std::max(tone, 112);
            if (row == 0) tone = 46;
            paint.put(tx, feet - row, shaded(source, tone));
        }
        // The thin line of shadow under it, which is all a flat thing casts.
        paint.blend(tx, feet + 1, theme::kPropShadow);
    }

    // Glass, metal and rust thrown clear, from a hash of the car's place and not from the
    // painter's stream, which must carry on exactly as it would have.
    const int bits = 10 + (rect.x1 - rect.x0) / 8;
    for (int i = 0; i < bits; ++i) {
        const uint32_t a = texelNoise(x + i * 31, y - i * 17);
        const uint32_t b = texelNoise(x - i * 13, y + i * 29);
        const int bx = x - halfAfter - 3 + static_cast<int>(a % static_cast<uint32_t>(halfAfter * 2 + 6));
        const int by = feet + 3 - static_cast<int>(b % static_cast<uint32_t>(shape.rows + 6));
        const Color color = theme::kFlatCarBits[(a >> 8) % 3u];
        paint.put(bx, by, color);
        if (((b >> 8) & 3u) == 0u) paint.put(bx + 1, by, color);
    }
}

}  // namespace ls::maps
