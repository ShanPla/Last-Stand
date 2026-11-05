#include "components/Maps/City/City.h"

#include "components/Maps/City/Lots/Lots.h"
#include "components/Maps/Layout/CityLayout/CityLayout.h"
#include "components/Maps/Paint/Paint.h"
#include "components/Maps/City/Props/Props.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::city {

using namespace theme;

namespace {

// A crack is a random walk, so it needs a fence. A road only gets painted by the chunks it
// can reach, and this much room keeps a crack, its lit edge and any weed on it inside them.
constexpr int kCrackSeamRoom = 6;

bool isRoad(const Roads& r, int x, int y) {
    const bool inLeft = x < kRoadHalfX;
    const bool inRight = x >= kChunkWidth - kRoadHalfX;
    const bool inTop = y < kRoadHalfY;
    const bool inBottom = y >= kChunkHeight - kRoadHalfY;
    if ((inLeft || inRight) && (inTop || inBottom)) {
        return r.corner[(inRight ? 1 : 0) + (inBottom ? 2 : 0)];
    }
    if (inLeft) return r.left;
    if (inRight) return r.right;
    if (inTop) return r.top;
    if (inBottom) return r.bottom;
    return false;
}

bool isSidewalk(const Roads& r, int x, int y) {
    if (r.left && x < kRoadHalfX + kWalkX) return true;
    if (r.right && x >= kChunkWidth - kRoadHalfX - kWalkX) return true;
    if (r.top && y < kRoadHalfY + kWalkY) return true;
    if (r.bottom && y >= kChunkHeight - kRoadHalfY - kWalkY) return true;
    // Keep the corners of a crossing tidy when only one of its roads reaches here.
    const bool nearLeft = x < kRoadHalfX + kWalkX;
    const bool nearRight = x >= kChunkWidth - kRoadHalfX - kWalkX;
    const bool nearTop = y < kRoadHalfY + kWalkY;
    const bool nearBottom = y >= kChunkHeight - kRoadHalfY - kWalkY;
    if ((nearLeft || nearRight) && (nearTop || nearBottom)) return r.corner[(nearRight ? 1 : 0) + (nearBottom ? 2 : 0)];
    return false;
}

Color asphaltTexel(int gx, int gy, uint32_t noise) {
    // Large soft blotches plus fine speckle. Tones are close together on purpose.
    const int blotch = softNoise(gx, gy, 5, 4) + static_cast<int>((noise >> 4) & 31u) - 15;
    const uint32_t bits = noise & 15u;
    int tone = 1;
    if (bits < 4u) tone = 0;
    if (bits > 12u) tone = 2;
    if (blotch < 100 && tone > 0) tone--;
    if (blotch > 160 && tone < 2) tone++;
    return kAsphalt[tone];
}

Color sidewalkTexel(int gx, int gy, uint32_t noise) {
    // Slabs in a running grid, each with its own tone and the odd missing corner.
    const int slabX = gx >> 4;
    const int slabY = gy >> 3;
    if ((gx & 15) == 0 || (gy & 7) == 0) return kSidewalkJoint;
    const uint32_t slab = texelNoise(slabX, slabY);
    int tone = static_cast<int>(slab % 3u);
    if ((noise & 15u) == 0u && tone > 0) tone--;
    if ((slab & 63u) < 3u) return (noise & 3u) == 0u ? kDirtDark : kDirt[noise % 3u];  // A slab gone missing.
    return kSidewalk[tone];
}

void paintBase(Paint& p, const Roads& roads, int rowFrom, int rowTo) {
    for (int y = p.top() + rowFrom; y < p.top() + rowTo; ++y) {
        const int ly = y - p.top();
        for (int x = p.left(); x < p.right(); ++x) {
            const int lx = x - p.left();
            const uint32_t noise = texelNoise(x, y);
            Color color;
            if (isRoad(roads, lx, ly)) {
                color = asphaltTexel(x, y, noise);
            } else if (isSidewalk(roads, lx, ly)) {
                color = sidewalkTexel(x, y, noise);
            } else {
                const uint32_t bits = noise & 7u;
                color = kDirt[bits < 2u ? 0 : (bits < 6u ? 1 : 2)];
                if ((noise & 63u) == 0u) color = kGravel;
            }
            p.put(x, y, color);
        }
    }
}

// Where a crack may go, in whole-map texels. It is the same box for every chunk that paints
// this road, so they all see the crack stop at the same spot and can't disagree at a seam.
struct CrackBox {
    int x0, y0, x1, y1;
    bool holds(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

void crack(Paint& p, TileRandom& random, const CrackBox& box, int x, int y, int steps, int bias) {
    // A random walk with a preferred direction. Each step has a lit edge beside it,
    // which is what makes a thin dark line read as a split in the road.
    int cx = x;
    int cy = y;
    int branchesLeft = 2;
    for (int i = 0; i < steps; ++i) {
        p.put(cx, cy, kAsphaltCrack);
        p.put(cx + 1, cy + 1, kAsphaltCrackEdge);
        const int run = random.range(1, 4);
        const bool across = bias == 0 ? random.chance(70) : random.chance(30);
        for (int j = 0; j < run; ++j) {
            cx += across ? (bias == 0 ? 1 : random.range(-1, 1)) : random.range(-1, 1);
            cy += across ? random.range(-1, 1) : 1;
            if (!box.holds(cx, cy)) return;
            p.put(cx, cy, kAsphaltCrack);
            if (j % 2 == 0) p.put(cx + 1, cy + 1, kAsphaltCrackEdge);
        }
        if (branchesLeft > 0 && random.chance(12)) {
            --branchesLeft;
            crack(p, random, box, cx, cy, steps / 3, 1 - bias);
        }
        if (random.chance(10)) weedTuft(p, cx, cy, random);
    }
}

void pothole(Paint& p, TileRandom& random, int x, int y) {
    const int rx = random.range(3, 8);
    const int ry = std::max(2, rx / 2);
    p.oval(x, y + 1, rx + 1, ry, kAsphaltCrackEdge);
    p.oval(x, y, rx, ry, kPotholeEdge);
    p.oval(x, y, rx - 1, ry - 1, kPothole);
    if (random.chance(40)) p.oval(x - 1, y, rx / 2, std::max(1, ry / 2), kPuddle);
    p.put(x + rx + 1, y - 1, kAsphaltCrackEdge);
    p.put(x - rx - 2, y + 1, kAsphaltCrack);
}

void asphaltPatch(Paint& p, int x, int y, int w, int h) {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            p.put(px, py, (texelNoise(px, py) & 7u) == 0u ? kAsphalt[0] : kAsphaltPatch);
        }
    }
}

// Paint wears off a road in flakes.
void worn(Paint& p, int x, int y, Color color, uint32_t wear) {
    const uint32_t noise = texelNoise(x, y) & 15u;
    if (noise < wear) return;
    p.put(x, y, color);
}

// --- Roads running up the screen ----------------------------------------------

void paintVerticalRoad(Paint& p, uint64_t seed, int i, int j, LightList& lights) {
    const int centerX = i * kChunkWidth;
    const int y0 = j * kChunkHeight + kRoadHalfY;
    const int y1 = (j + 1) * kChunkHeight - kRoadHalfY;
    if (!p.overlaps(centerX - kRoadHalfX - kWalkX, y0 - 48, centerX + kRoadHalfX + kWalkX, y1 + 8)) return;
    if (!verticalRoadExists(seed, i, j)) return;
    TileRandom random(seed, i, j, kSaltVerticalProps);

    // Center line: a double yellow, a dashed white, or nearly gone.
    const int style = random.range(0, 2);
    if (style == 0) {
        for (int y = y0; y < y1; ++y) {
            worn(p, centerX - 2, y, kPaintYellow, 5u);
            worn(p, centerX - 1, y, kPaintYellow, 5u);
            worn(p, centerX + 1, y, kPaintYellow, 5u);
            worn(p, centerX + 2, y, kPaintYellow, 5u);
        }
    } else if (style == 1) {
        for (int y = y0; y < y1; ++y) {
            if (((y + i * 7) & 31) >= 18) continue;
            for (int x = -1; x <= 1; ++x) worn(p, centerX + x, y, kPaintWhite, 3u);
        }
    }
    // Lane edges and curb.
    for (int y = y0; y < y1; ++y) {
        worn(p, centerX - kRoadHalfX + 5, y, kPaintWhite, 8u);
        worn(p, centerX + kRoadHalfX - 6, y, kPaintWhite, 8u);
        p.put(centerX - kRoadHalfX, y, kCurbLight);
        p.put(centerX - kRoadHalfX + 1, y, kCurbShade);
        p.put(centerX + kRoadHalfX - 1, y, kCurbLight);
        p.put(centerX + kRoadHalfX - 2, y, kCurbShade);
    }
    // Crosswalks at the end of the block, where the stripes run along the road.
    if (random.chance(60)) {
        for (int x = centerX - kRoadHalfX + 8; x < centerX + kRoadHalfX - 8; x += 12) {
            for (int py = y1 - 18; py < y1 - 3; ++py) {
                for (int px = x; px < x + 7; ++px) worn(p, px, py, kPaintWhite, 3u);
            }
        }
    } else {
        for (int x = centerX - kRoadHalfX + 8; x < centerX + kRoadHalfX - 8; ++x) {
            worn(p, x, y1 - 6, kPaintWhite, 4u);
            worn(p, x, y1 - 5, kPaintWhite, 4u);
        }
    }

    // Wear and tear. The row of chunks below this block doesn't paint its road, so a crack
    // stops short of that seam. Both columns and the row above do paint it.
    const CrackBox cracks{centerX - kChunkWidth, y0 - kChunkHeight, centerX + kChunkWidth, (j + 1) * kChunkHeight - kCrackSeamRoom};
    for (int n = random.range(3, 6); n > 0; --n) {
        crack(p, random, cracks, centerX + random.range(-kRoadHalfX + 4, kRoadHalfX - 4), random.range(y0, y1), random.range(6, 18), 1);
    }
    for (int n = random.range(1, 3); n > 0; --n) {
        pothole(p, random, centerX + random.range(-kRoadHalfX + 8, kRoadHalfX - 8), random.range(y0 + 8, y1 - 8));
    }
    if (random.chance(50)) {
        asphaltPatch(p, centerX + random.range(-kRoadHalfX + 4, 4), random.range(y0, y1 - 30), random.range(16, 34), random.range(12, 30));
    }
    for (int n = random.range(4, 10); n > 0; --n) {
        weedTuft(p, random.chance(50) ? centerX - kRoadHalfX + random.range(2, 5) : centerX + kRoadHalfX - random.range(3, 6), random.range(y0, y1), random);
    }

    // Street furniture on both sidewalks, and now and then a wreck in the lane.
    const int walkLeft = centerX - kRoadHalfX - kWalkX / 2;
    const int walkRight = centerX + kRoadHalfX + kWalkX / 2;
    streetLamp(p, walkLeft, random.range(y0 + 14, y0 + 30), true, random.chance(25), lights);
    if (random.chance(50)) streetLamp(p, walkRight, random.range(y1 - 34, y1 - 14), false, random.chance(25), lights);
    if (random.chance(45)) barrel(p, walkLeft + random.range(-3, 3), random.range(y0 + 40, y1 - 10), random);
    if (random.chance(35)) brokenSign(p, walkRight + random.range(-3, 3), random.range(y0 + 40, y1 - 40), random);
    if (random.chance(30)) boards(p, walkRight, random.range(y0 + 10, y1 - 10), random);
    if (CarPlan car = verticalRoadCar(seed, i, j); car.exists) {
        CarMark mark(p, CarSprite::CityEnd, car.x, car.y);
        carEnd(p, car.x, car.y, car.looks);
    }
    if (random.chance(35)) barrier(p, centerX + random.range(-kRoadHalfX / 2, kRoadHalfX / 2), random.range(y0 + 16, y1 - 10), random);
    if (random.chance(25)) burningBarrel(p, walkRight + random.range(-3, 3), random.range(y0 + 14, y1 - 14), random, lights);
}

// --- Roads running across the screen -------------------------------------------

void paintHorizontalRoad(Paint& p, uint64_t seed, int i, int j, LightList& lights) {
    const int centerY = j * kChunkHeight;
    const int x0 = i * kChunkWidth + kRoadHalfX;
    const int x1 = (i + 1) * kChunkWidth - kRoadHalfX;
    if (!p.overlaps(x0 - 8, centerY - kRoadHalfY - 56, x1 + 8, centerY + kRoadHalfY + kWalkY + 8)) return;
    if (!horizontalRoadExists(seed, i, j)) return;
    TileRandom random(seed, i, j, kSaltHorizontalProps);

    const int style = random.range(0, 2);
    if (style == 0) {
        for (int x = x0; x < x1; ++x) {
            worn(p, x, centerY - 2, kPaintYellow, 5u);
            worn(p, x, centerY - 1, kPaintYellow, 5u);
            worn(p, x, centerY + 1, kPaintYellow, 5u);
        }
    } else if (style == 1) {
        for (int x = x0; x < x1; ++x) {
            if (((x + j * 7) & 31) >= 18) continue;
            worn(p, x, centerY - 1, kPaintWhite, 3u);
            worn(p, x, centerY, kPaintWhite, 3u);
        }
    }
    for (int x = x0; x < x1; ++x) {
        worn(p, x, centerY - kRoadHalfY + 4, kPaintWhite, 8u);
        worn(p, x, centerY + kRoadHalfY - 5, kPaintWhite, 8u);
        p.put(x, centerY - kRoadHalfY, kCurbLight);
        p.put(x, centerY - kRoadHalfY + 1, kCurbShade);
        p.put(x, centerY + kRoadHalfY - 1, kCurbLight);
        p.put(x, centerY + kRoadHalfY, kCurbShade);
    }
    // Crosswalks at the block ends. Seen from this angle, the stripes run across.
    if (random.chance(60)) {
        const int endX = random.chance(50) ? x0 + 8 : x1 - 26;
        for (int y = centerY - kRoadHalfY + 5; y < centerY + kRoadHalfY - 5; y += 7) {
            for (int py = y; py < y + 4; ++py) {
                for (int px = endX; px < endX + 18; ++px) worn(p, px, py, kPaintWhite, 3u);
            }
        }
    }

    // The columns on either side of this block don't paint its road, so a crack stops short
    // of both seams. The rows above and below do paint it.
    const CrackBox cracks{i * kChunkWidth + kCrackSeamRoom, centerY - kChunkHeight, (i + 1) * kChunkWidth - kCrackSeamRoom, centerY + kChunkHeight};
    for (int n = random.range(3, 6); n > 0; --n) {
        crack(p, random, cracks, random.range(x0, x1), centerY + random.range(-kRoadHalfY + 4, kRoadHalfY - 4), random.range(6, 18), 0);
    }
    for (int n = random.range(1, 3); n > 0; --n) {
        pothole(p, random, random.range(x0 + 10, x1 - 10), centerY + random.range(-kRoadHalfY + 6, kRoadHalfY - 6));
    }
    if (random.chance(50)) {
        asphaltPatch(p, random.range(x0, x1 - 40), centerY + random.range(-kRoadHalfY + 3, 2), random.range(20, 50), random.range(8, 16));
    }
    for (int n = random.range(4, 10); n > 0; --n) {
        weedTuft(p, random.range(x0, x1), random.chance(50) ? centerY - kRoadHalfY + random.range(2, 4) : centerY + kRoadHalfY - random.range(2, 4), random);
    }

    const int walkTop = centerY - kRoadHalfY - kWalkY / 2;
    const int walkBottom = centerY + kRoadHalfY + kWalkY / 2;
    streetLamp(p, random.range(x0 + 16, x0 + 60), walkTop, true, random.chance(25), lights);
    if (random.chance(55)) streetLamp(p, random.range(x1 - 60, x1 - 16), walkBottom, false, random.chance(25), lights);
    if (random.chance(45)) barrel(p, random.range(x0 + 20, x1 - 20), walkTop + random.range(-2, 2), random);
    if (random.chance(35)) brokenSign(p, random.range(x0 + 60, x1 - 60), walkBottom, random);
    if (random.chance(30)) boards(p, random.range(x0 + 20, x1 - 20), walkBottom, random);
    if (CarPlan car = horizontalRoadCar(seed, i, j); car.exists) {
        CarMark mark(p, CarSprite::CitySide, car.x, car.y);
        carSide(p, car.x, car.y, car.looks);
    }
    if (random.chance(30)) barrier(p, random.range(x0 + 30, x1 - 30), centerY + random.range(-kRoadHalfY / 2, kRoadHalfY / 2), random);
    if (random.chance(20)) burningBarrel(p, random.range(x0 + 30, x1 - 30), walkTop, random, lights);
}

// --- Crossings ----------------------------------------------------------------

void paintCrossing(Paint& p, uint64_t seed, int i, int j) {
    const int centerX = i * kChunkWidth;
    const int centerY = j * kChunkHeight;
    if (!p.overlaps(centerX - kRoadHalfX - kWalkX - 4, centerY - kRoadHalfY - 40, centerX + kRoadHalfX + kWalkX + 4, centerY + kRoadHalfY + kWalkY + 4)) return;
    TileRandom random(seed, i, j, kSaltCorner);
    // All four chunks around a crossing paint it, so these cracks can go anywhere near it.
    const CrackBox cracks{centerX - kChunkWidth, centerY - kChunkHeight, centerX + kChunkWidth, centerY + kChunkHeight};
    for (int n = random.range(1, 3); n > 0; --n) {
        crack(p, random, cracks, centerX + random.range(-kRoadHalfX + 6, kRoadHalfX - 6), centerY + random.range(-kRoadHalfY + 4, kRoadHalfY - 4), random.range(6, 14), random.range(0, 1));
    }
    if (random.chance(70)) {
        // A manhole cover, a little off center.
        const int mx = centerX + random.range(-20, 20);
        const int my = centerY + random.range(-10, 10);
        p.oval(mx, my, 6, 3, kAsphaltCrack);
        p.oval(mx, my, 5, 2, kMetalProp[0]);
        p.put(mx - 2, my, kMetalProp[1]);
        p.put(mx + 2, my, kMetalProp[1]);
    }
    if (random.chance(40)) pothole(p, random, centerX + random.range(-kRoadHalfX + 10, kRoadHalfX - 10), centerY + random.range(-kRoadHalfY + 6, kRoadHalfY - 6));

    // Stop signs at the corners of the crossing, some of them leaning.
    const bool roadsMeetHere = verticalRoadExists(seed, i, j) && horizontalRoadExists(seed, i, j);
    if (roadsMeetHere && random.chance(70)) stopSign(p, centerX + kRoadHalfX + 6, centerY + kRoadHalfY + 5, random.range(-3, 3));
    if (roadsMeetHere && random.chance(40)) stopSign(p, centerX - kRoadHalfX - 6, centerY - kRoadHalfY - 3, random.range(-3, 3));
}

}  // namespace

void paintGroundRows(uint64_t seed, int chunkX, int chunkY, Color* pixels, int rowFrom, int rowTo) {
    Paint paint(pixels, chunkX * kChunkWidth, chunkY * kChunkHeight, kChunkWidth, kChunkHeight);
    paintBase(paint, roadsAround(seed, chunkX, chunkY), rowFrom, rowTo);
}

int paintDetails(uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* outLights, CrushedCars crushed) {
    Paint paint(pixels, chunkX * kChunkWidth, chunkY * kChunkHeight, kChunkWidth, kChunkHeight);
    paint.setCrushed(crushed);
    LightList lights{outLights, 0, &paint};
    const Roads roads = roadsAround(seed, chunkX, chunkY);

    // Lots stay inside their own chunk, so each chunk paints only its own.
    {
        TileRandom random(seed, chunkX, chunkY, kSaltLot);
        const LotArea lot = lotAreaOf(roads, chunkX, chunkY);
        CarPlan cars[kMaxLotCars];
        const int carCount = parkingLotCars(seed, chunkX, chunkY, lot, cars);
        paintLot(paint, random, lot, cars, carCount, lights);
    }

    // Roads and their furniture can cross chunk edges, so each chunk also paints the
    // parts of its neighbors' roads that reach into it. Upper rows go first, so things
    // nearer the camera overlap things farther away.
    for (int j = chunkY - 1; j <= chunkY + 1; ++j) {
        for (int i = chunkX - 1; i <= chunkX + 1; ++i) {
            paintCrossing(paint, seed, i, j);
            paintHorizontalRoad(paint, seed, i, j, lights);
            paintVerticalRoad(paint, seed, i, j, lights);
        }
    }
    return lights.count;
}

}  // namespace ls::maps::city
