#include "components/Maps/City/Lots/Lots.h"

#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::city {

using namespace theme;

namespace {

Color pickNoisy(const Color tones[3], uint32_t noise) {
    const uint32_t bits = noise & 7u;
    return tones[bits < 2u ? 0 : (bits < 6u ? 1 : 2)];
}

void fillDirt(Paint& p, const LotArea& a) {
    for (int y = a.y0; y < a.y1; ++y) {
        for (int x = a.x0; x < a.x1; ++x) {
            const uint32_t noise = texelNoise(x, y);
            Color color = pickNoisy(kDirt, noise);
            if ((noise & 127u) == 0u) color = kGravel;
            if ((noise & 127u) == 1u) color = kDirtDark;
            // Big soft patches of darker earth.
            if (softNoise(x, y, 4, 3) + static_cast<int>((noise >> 4) & 31u) > 185) color = kDirtDark;
            p.put(x, y, color);
        }
    }
}

void fillGrass(Paint& p, const LotArea& a) {
    for (int y = a.y0; y < a.y1; ++y) {
        for (int x = a.x0; x < a.x1; ++x) {
            const uint32_t noise = texelNoise(x, y);
            Color color = pickNoisy(kDeadGrass, noise);
            if (softNoise(x, y, 4, 3) + static_cast<int>((noise >> 4) & 31u) > 165) color = pickNoisy(kDirt, noise);
            if ((noise & 63u) == 0u) color = kWeed[1];
            p.put(x, y, color);
        }
    }
}

enum class Floor { Concrete, Tile, Planks };

void fillFloor(Paint& p, const LotArea& a, Floor floor) {
    for (int y = a.y0; y < a.y1; ++y) {
        for (int x = a.x0; x < a.x1; ++x) {
            const uint32_t noise = texelNoise(x, y);
            Color color;
            switch (floor) {
                case Floor::Tile:
                    color = ((x >> 3) + (y >> 2)) & 1 ? kTileA : kTileB;
                    if ((x & 7) == 0 || (y & 3) == 0) color = kFloorStain;
                    if ((texelNoise(x >> 3, y >> 2) & 15u) == 0u) color = kConcreteFloor[0];  // A missing tile.
                    break;
                case Floor::Planks: {
                    const int row = y >> 2;
                    color = kPlank[(texelNoise(row, (x + row * 7) >> 5) >> 2) % 3u];
                    if ((y & 3) == 0 || ((x + row * 11) & 31) == 0) color = kPlankGap;
                    break;
                }
                default:
                    color = pickNoisy(kConcreteFloor, noise);
                    if ((x % 48) == 0 || (y % 28) == 0) color = kFloorStain;
                    break;
            }
            // Grime in patches over any floor.
            if (softNoise(x, y, 4, 3) + static_cast<int>((noise >> 4) & 31u) > 190) color = kFloorStain;
            p.put(x, y, color);
        }
    }
}

struct WallLook {
    const Color* tones;
    Color top;
    bool plaster;
};

Color wallTexel(const WallLook& look, int x, int y, bool showBrick) {
    if (look.plaster && !showBrick) {
        const uint32_t noise = texelNoise(x, y);
        if ((noise & 15u) == 0u) return kPlasterStain;
        return look.tones[noise % 3u];
    }
    // Courses of bricks, each row offset by half a brick.
    const int course = y >> 2;
    if ((y & 3) == 0) return kBrickMortar;
    const int brickX = x + (course & 1) * 4;
    if ((brickX & 7) == 0) return kBrickMortar;
    return kBrick[texelNoise(brickX >> 3, course) % 3u];
}

// A wall running across the screen. We see its face, up to a ragged broken top.
// heights[] gives the height of each column, and 0 means it has fallen there.
void wallFace(Paint& p, TileRandom& random, const WallLook& look, int x0, int x1, int footY, int maxHeight, bool withWindows, bool shadowBelow) {
    // Room for the widest wall a chunk can hold, so a wall always reaches its corner.
    constexpr int kMaxColumns = kChunkWidth;
    int heights[kMaxColumns];
    const int columns = std::min(kMaxColumns, x1 - x0);

    // Broken into runs, each at its own height. Runs step down in chunks, as brick does.
    int column = 0;
    int runHeight = random.range(maxHeight / 2, maxHeight);
    while (column < columns) {
        const int run = random.range(3, 11);
        for (int i = 0; i < run && column < columns; ++i) heights[column++] = std::max(0, runHeight);
        const int roll = random.range(0, 9);
        if (roll < 1) runHeight = 0;
        else if (roll < 4) runHeight = random.range(2, maxHeight / 2);
        else if (roll < 8) runHeight = random.range(maxHeight / 2, maxHeight);
        else runHeight = maxHeight;
    }

    if (shadowBelow) {
        for (int i = 0; i < columns; ++i) {
            const int depth = std::min(5, heights[i] / 3);
            for (int row = 1; row <= depth; ++row) p.blend(x0 + i, footY + row, kWallShade);
        }
    }

    for (int i = 0; i < columns; ++i) {
        const int x = x0 + i;
        const int height = heights[i];
        if (height == 0) continue;
        for (int row = 0; row < height; ++row) {
            const int y = footY - row;
            // Plaster has fallen off in big patches, showing the bricks behind.
            const bool brickShows = (texelNoise(x >> 3, y >> 2) & 3u) == 0u;
            Color color = wallTexel(look, x, y, brickShows);
            // Soft shading: darker near the ground, and just under the ragged top.
            if (row < 2) color = Color{static_cast<unsigned char>(color.r * 3 / 4), static_cast<unsigned char>(color.g * 3 / 4), static_cast<unsigned char>(color.b * 3 / 4), 255};
            p.put(x, y, color);
        }
        // The top of the wall is lit and has thickness.
        p.put(x, footY - height, look.top);
        p.put(x, footY - height - 1, look.top);
        p.put(x, footY - height - 2, kBrickTop);
    }

    if (withWindows && columns > 40) {
        for (int w = x0 + 12; w + 14 < x1; w += random.range(24, 40)) {
            const int top = footY - maxHeight + 6;
            const int bottom = footY - 5;
            if (bottom - top < 4) break;
            if (heights[std::min(columns - 1, w - x0 + 5)] < maxHeight - 3) continue;
            p.rect(w, top, w + 10, bottom, kWindowDark);
            p.rect(w - 1, bottom, w + 11, bottom + 1, look.top);
            p.put(w + 3, top + 2, kBrickMortar);
            p.rect(w + 4, top, w + 5, bottom, kBeamWood[0]);  // A last piece of the frame.
        }
    }
}

// A wall running up the screen. We mostly see its top, lifted by its height.
void wallAlong(Paint& p, TileRandom& random, const WallLook& look, int x, int y0, int y1, int maxHeight) {
    constexpr int kThick = 5;
    int y = y0;
    int previous = 0;
    while (y < y1) {
        const int run = random.range(6, 22);
        const int roll = random.range(0, 9);
        const int height = roll < 2 ? 0 : (roll < 6 ? random.range(3, maxHeight / 2) : random.range(maxHeight / 2, maxHeight));
        const int end = std::min(y1, y + run);
        for (int row = y; row < end; ++row) {
            if (height == 0) continue;
            for (int col = 0; col < kThick; ++col) {
                const uint32_t noise = texelNoise(x + col, row);
                p.put(x + col, row - height, (noise & 7u) == 0u ? kBrickTop : look.top);
            }
            // Shadow on the floor to the right.
            for (int col = 0; col < std::min(4, height / 2); ++col) p.blend(x + kThick + col, row, kWallShade);
        }
        // Where the wall steps down toward us, its broken end shows bricks.
        if (height > 0) {
            for (int row = 0; row < height; ++row) {
                for (int col = 0; col < kThick; ++col) p.put(x + col, end - 1 - row + 0, wallTexel(look, x + col, end - row, true));
            }
        }
        (void)previous;
        previous = height;
        y = end;
    }
}

void scatterFloorDebris(Paint& p, TileRandom& random, const LotArea& a, int count) {
    for (int i = 0; i < count; ++i) {
        const int x = random.range(a.x0, a.x1 - 1);
        const int y = random.range(a.y0, a.y1 - 1);
        switch (random.range(0, 4)) {
            case 0: looseBricks(p, x, y, 8, random.range(3, 7), random); break;
            case 1: p.rect(x, y, x + random.range(2, 6), y + 1, kRubble[random.range(0, 3)]); break;
            case 2: p.oval(x, y, random.range(2, 5), random.range(1, 2), kFloorStain); break;
            case 3: p.put(x, y, kRubble[3]); p.put(x + 1, y, kRubble[0]); break;
            default: weedTuft(p, x, y, random); break;
        }
    }
}

void paintRuin(Paint& p, TileRandom& random, const LotArea& lot, bool plaster, LightList& lights) {
    fillDirt(p, lot);
    // A building doesn't fill the whole block. The yard around it holds the rest.
    const int width = lot.x1 - lot.x0;
    const int height = lot.y1 - lot.y0;
    LotArea b{lot.x0 + random.range(4, 14), lot.y0 + random.range(20, 26), lot.x1 - random.range(4, 14), lot.y1 - random.range(8, 14)};
    const bool split = width > 180 && random.chance(40);
    const Floor floorType = static_cast<Floor>(random.range(0, 2));
    fillFloor(p, b, floorType);
    scatterFloorDebris(p, random, b, width * height / 900);

    const WallLook look = plaster ? WallLook{kPlaster, kPlasterTop, true} : WallLook{kBrick, kBrickTop, false};
    const int tall = random.range(18, 28);

    // Exposed roof beams: the last of the roof, lying across the room with their shadows.
    if (random.chance(70)) {
        const int beams = random.range(2, 5);
        const int startX = random.range(b.x0 + 10, b.x1 - 60);
        for (int i = 0; i < beams; ++i) {
            const int x = startX + i * random.range(10, 16);
            const int drift = random.range(-6, 6);
            beam(p, x, b.y0 + random.range(2, 10), x + drift, b.y1 - random.range(2, 28));
        }
    } else {
        // A fallen timber or two instead.
        for (int i = 0; i < 2; ++i) {
            const int x = random.range(b.x0 + 10, b.x1 - 50);
            const int y = random.range(b.y0 + 10, b.y1 - 10);
            beam(p, x, y, x + random.range(30, 60), y + random.range(-14, 14));
        }
    }

    // Holes where the floor gave way.
    if (random.chance(35)) {
        const int x = random.range(b.x0 + 16, b.x1 - 16);
        const int y = random.range(b.y0 + 10, b.y1 - 8);
        p.oval(x, y, random.range(8, 14), random.range(4, 7), kWindowDark);
        p.oval(x, y - 1, random.range(5, 8), random.range(2, 4), kPothole);
    }

    // Furniture that survived, more or less.
    const int furniture = random.range(1, 3);
    for (int i = 0; i < furniture; ++i) {
        const int x = random.range(b.x0 + 16, b.x1 - 16);
        const int y = random.range(b.y0 + 18, b.y1 - 6);
        switch (random.range(0, 2)) {
            case 0: sofa(p, x, y, random); break;
            case 1: table(p, x, y, random); break;
            default: bedFrame(p, x, y, random); break;
        }
    }

    // Walls, from the back to the front, so nearer ones overlap farther ones.
    wallFace(p, random, look, b.x0, b.x1, b.y0, tall, true, true);
    if (split) {
        const int middle = (b.x0 + b.x1) / 2 + random.range(-20, 20);
        wallAlong(p, random, look, middle, b.y0 + 2, b.y1 - 12, tall / 2 + 4);
        wallFace(p, random, look, b.x0 + 2, middle, (b.y0 + b.y1) / 2, 9, false, true);
    }
    wallAlong(p, random, look, b.x0, b.y0, b.y1, tall);
    wallAlong(p, random, look, b.x1 - 5, b.y0, b.y1, tall);
    // The near wall is low, or it would hide the room behind it.
    wallFace(p, random, look, b.x0, b.x1, b.y1, random.range(4, 8), false, false);

    // Rubble piles where walls came down.
    const int piles = random.range(2, 5);
    for (int i = 0; i < piles; ++i) {
        const int edge = random.range(0, 3);
        int x, y;
        if (edge == 0) {
            x = random.range(b.x0, b.x1);
            y = b.y0 + random.range(4, 9);
        } else if (edge == 1) {
            x = random.range(b.x0, b.x1);
            y = b.y1 + random.range(0, 4);
        } else {
            x = edge == 2 ? b.x0 + random.range(0, 10) : b.x1 - random.range(0, 10);
            y = random.range(b.y0 + 6, b.y1);
        }
        rubblePile(p, x, y, random.range(6, 13), random);
    }
    looseBricks(p, random.range(lot.x0 + 10, lot.x1 - 10), lot.y1 - 5, 18, 12, random);

    if (random.chance(25)) burningBarrel(p, random.range(b.x0 + 12, b.x1 - 12), random.range(b.y0 + 14, b.y1 - 6), random, lights);
}

void paintParkingLot(Paint& p, TileRandom& random, const LotArea& a, LightList& lights) {
    for (int y = a.y0; y < a.y1; ++y) {
        for (int x = a.x0; x < a.x1; ++x) {
            const uint32_t noise = texelNoise(x, y);
            Color color = pickNoisy(kAsphalt, noise);
            if (softNoise(x, y, 5, 4) + static_cast<int>((noise >> 4) & 31u) > 180) color = kAsphaltPatch;
            p.put(x, y, color);
        }
    }
    // Rows of faded parking bays, and stains where cars once stood.
    const int rowY = a.y0 + (a.y1 - a.y0) / 2;
    for (int x = a.x0 + 8; x < a.x1 - 8; x += 30) {
        const bool up = random.chance(85);
        const bool down = random.chance(85);
        for (int y = rowY - 30; y < rowY + 30; ++y) {
            if ((texelNoise(x, y) & 3u) == 0u) continue;
            if (y < rowY && !up) continue;
            if (y >= rowY && !down) continue;
            p.put(x, y, kPaintWhite);
        }
        if (random.chance(40)) p.oval(x + 15, rowY - 12, 6, 3, kAsphaltCrack);
    }
    for (int x = a.x0 + 8; x < a.x1 - 8; ++x) {
        if ((texelNoise(x, rowY) & 3u) != 0u) p.put(x, rowY, kPaintWhite);
    }
    for (int i = 0; i < 8; ++i) {
        weedTuft(p, random.range(a.x0, a.x1), random.range(a.y0, a.y1), random);
    }
    const int cars = random.range(1, 3);
    for (int i = 0; i < cars; ++i) {
        carSide(p, a.x0 + 40 + i * ((a.x1 - a.x0 - 80) / std::max(1, cars - 1 + 1)) + random.range(-6, 6),
                i % 2 == 0 ? rowY - 8 : rowY + 26, random);
    }
    tires(p, a.x1 - 20, a.y1 - 10);
    rubblePile(p, a.x0 + 18, a.y1 - 12, 7, random);
    if (random.chance(50)) barrier(p, random.range(a.x0 + 40, a.x1 - 40), a.y0 + 12, random);
    fenceAcross(p, a.x0, a.x1, a.y0 + 2, random);
    fenceAlong(p, a.x0, a.y0, a.y1, random);
    fenceAlong(p, a.x1 - 2, a.y0, a.y1, random);
    if (random.chance(25)) burningBarrel(p, random.range(a.x0 + 30, a.x1 - 30), random.range(a.y0 + 18, a.y0 + 34), random, lights);
}

void paintCamp(Paint& p, TileRandom& random, const LotArea& a, LightList& lights) {
    fillDirt(p, a);
    for (int i = 0; i < 16; ++i) {
        weedTuft(p, random.range(a.x0, a.x1), random.range(a.y0, a.y1), random);
    }
    const int centerX = (a.x0 + a.x1) / 2 + random.range(-20, 20);
    const int centerY = (a.y0 + a.y1) / 2 + random.range(-6, 10);

    // Sorted back to front by construction: top row, fire, then the bottom row.
    fenceAcross(p, a.x0 + 4, a.x1 - 4, a.y0 + 6, random);
    tent(p, a.x0 + random.range(30, 60), a.y0 + random.range(30, 44), random);
    tent(p, a.x1 - random.range(70, 95), a.y0 + random.range(28, 42), random);
    campfire(p, centerX, centerY, lights);
    sandbags(p, a.x0 + 16, a.y1 - 12, random.range(4, 7), random);
    tent(p, a.x1 - random.range(50, 70), a.y1 - random.range(14, 24), random);
    crate(p, a.x0 + random.range(70, 110), a.y1 - random.range(16, 30), random);
    crate(p, a.x0 + random.range(16, 40), a.y0 + random.range(66, 86), random);
    barrel(p, centerX + 34, centerY - 8, random);
    barrel(p, centerX + 44, centerY - 3, random);
    boards(p, centerX - 40, centerY + 16, random);
    if (random.chance(50)) burningBarrel(p, a.x1 - random.range(14, 30), a.y0 + random.range(46, 90), random, lights);

    // A wire with clothes, between two poles.
    const int lineX = a.x0 + random.range(20, 90);
    const int lineY = a.y0 + random.range(60, 80);
    p.rect(lineX, lineY - 16, lineX + 1, lineY, kBeamWood[1]);
    p.rect(lineX + 46, lineY - 16, lineX + 47, lineY, kBeamWood[1]);
    p.line(lineX, lineY - 16, lineX + 46, lineY - 15, kFencePost);
    for (int i = 0; i < 3; ++i) {
        const int cx = lineX + 8 + i * 13;
        p.rect(cx, lineY - 15, cx + random.range(5, 8), lineY - 15 + random.range(5, 8), kCloth[random.range(0, 2)]);
    }
}

void paintPark(Paint& p, TileRandom& random, const LotArea& a) {
    fillGrass(p, a);
    // A gravel path crossing the park, wandering a bit.
    int pathY = random.range(a.y0 + 40, a.y1 - 40);
    for (int x = a.x0; x < a.x1; ++x) {
        if (x % 12 == 0) pathY += random.range(-1, 1);
        for (int row = -6; row <= 6; ++row) {
            const uint32_t noise = texelNoise(x, pathY + row);
            p.put(x, pathY + row, (noise & 3u) == 0u ? kGravel : kDirt[noise % 3u]);
        }
    }
    for (int i = 0; i < 30; ++i) weedTuft(p, random.range(a.x0, a.x1), random.range(a.y0, a.y1), random);
    puddle(p, random.range(a.x0 + 30, a.x1 - 30), random.range(a.y0 + 20, a.y1 - 20), random.range(10, 22), random.range(4, 7));
    const int trees = random.range(3, 6);
    int y = a.y0 + 26;
    for (int i = 0; i < trees; ++i) {
        deadTree(p, random.range(a.x0 + 20, a.x1 - 20), y, random);
        y += (a.y1 - a.y0 - 30) / trees;
    }
    bench(p, random.range(a.x0 + 30, a.x1 - 30), pathY - 9);
    if (random.chance(60)) bench(p, random.range(a.x0 + 30, a.x1 - 30), pathY + 16);
    boards(p, random.range(a.x0 + 30, a.x1 - 30), random.range(a.y0 + 20, a.y1 - 10), random);
    rubblePile(p, random.range(a.x0 + 20, a.x1 - 20), a.y1 - 12, 6, random);
}

void paintFoundation(Paint& p, TileRandom& random, const LotArea& a, LightList& lights) {
    fillDirt(p, a);
    LotArea slab{a.x0 + random.range(14, 30), a.y0 + random.range(26, 36), a.x1 - random.range(14, 30), a.y1 - random.range(14, 24)};
    fillFloor(p, slab, Floor::Concrete);
    // The slab's edge stands a little proud of the dirt.
    p.rect(slab.x0, slab.y1, slab.x1, slab.y1 + 3, kConcreteFloor[0]);
    p.blendRect(slab.x0, slab.y1 + 3, slab.x1, slab.y1 + 5, kPropShadow);
    // Rebar sticking out of the slab in rows, where walls once rose.
    for (int x = slab.x0 + 6; x < slab.x1 - 4; x += random.range(10, 18)) {
        const int tall = random.range(3, 9);
        p.rect(x, slab.y0 - tall, x + 1, slab.y0 + 1, kRebar);
        p.put(x + 1, slab.y0 - tall, kRebar);
    }
    for (int y = slab.y0 + 10; y < slab.y1; y += random.range(14, 24)) {
        const int tall = random.range(3, 8);
        p.rect(slab.x0 + 2, y - tall, slab.x0 + 3, y + 1, kRebar);
        p.rect(slab.x1 - 3, y - tall, slab.x1 - 2, y + 1, kRebar);
    }
    scatterFloorDebris(p, random, slab, (slab.x1 - slab.x0) * (slab.y1 - slab.y0) / 700);
    puddle(p, random.range(slab.x0 + 20, slab.x1 - 20), random.range(slab.y0 + 12, slab.y1 - 8), random.range(8, 16), random.range(3, 5));
    const WallLook look{kBrick, kBrickTop, false};
    wallFace(p, random, look, slab.x0, slab.x0 + random.range(30, 70), slab.y0 + 1, random.range(8, 14), false, true);
    wallAlong(p, random, look, slab.x1 - 5, slab.y0 + 2, slab.y0 + random.range(24, 60), random.range(8, 14));
    const int piles = random.range(2, 4);
    for (int i = 0; i < piles; ++i) {
        rubblePile(p, random.range(slab.x0, slab.x1), random.range(slab.y0 + 8, slab.y1 + 2), random.range(7, 13), random);
    }
    boards(p, random.range(slab.x0 + 20, slab.x1 - 20), random.range(slab.y0 + 12, slab.y1 - 6), random);
    tires(p, a.x0 + random.range(10, 24), a.y1 - random.range(8, 14));
    fenceAcross(p, a.x0 + 2, a.x1 - 2, a.y1 - 2, random);
    if (random.chance(40)) burningBarrel(p, random.range(a.x0 + 12, a.x1 - 12), a.y0 + random.range(14, 22), random, lights);
}

}  // namespace

void paintLot(Paint& paint, TileRandom& random, const LotArea& lot, LightList& lights) {
    const int roll = random.range(0, 99);
    if (roll < 34) paintRuin(paint, random, lot, false, lights);
    else if (roll < 54) paintRuin(paint, random, lot, true, lights);
    else if (roll < 66) paintFoundation(paint, random, lot, lights);
    else if (roll < 78) paintCamp(paint, random, lot, lights);
    else if (roll < 90) paintParkingLot(paint, random, lot, lights);
    else paintPark(paint, random, lot);
}

}  // namespace ls::maps::city
