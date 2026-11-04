#include "components/Maps/City/Props/Props.h"

#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::city {

using namespace theme;

void LightList::add(int x, int y, float height, LightKind kind) {
    if (count >= kMaxLightsPerChunk) return;
    // Only the chunk that holds the light's foot lists it, so it isn't drawn twice.
    if (x < area->left() || x >= area->right() || y < area->top() || y >= area->bottom()) return;
    Light& light = lights[count++];
    light.world = Vec2{static_cast<float>(x) * kChunkWorldSize / static_cast<float>(kChunkWidth),
                       static_cast<float>(y) * kChunkWorldSize / static_cast<float>(kChunkHeight)};
    light.height = height;
    light.kind = kind;
}

void contactShadow(Paint& p, int x, int y, int rx, int ry) { p.blendOval(x, y, rx, ry, kPropShadow); }

void barrel(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 6, 2);
    const int height = 11;
    for (int row = 0; row < height; ++row) {
        for (int col = -4; col <= 4; ++col) {
            const int shade = col < -2 ? 2 : (col > 2 ? 0 : 1);
            Color color = kRust[shade];
            if (row == 2 || row == 7) color = kBarrelBand;
            if ((texelNoise(x + col, y - row) & 15u) == 0u) color = kRust[0];
            p.put(x + col, y - row, color);
        }
    }
    p.oval(x, y - height, 4, 2, kRust[2]);
    p.oval(x, y - height, 3, 1, kCoal);
    if (random.chance(30)) p.put(x + 1, y - 4, kCoal);
}

void burningBarrel(Paint& p, int x, int y, TileRandom& random, LightList& lights) {
    barrel(p, x, y, random);
    lights.add(x, y, 14.0f, LightKind::Fire);
}

void streetLamp(Paint& p, int x, int y, bool armToRight, bool broken, LightList& lights) {
    contactShadow(p, x, y, 4, 2);
    const int height = broken ? 22 : 38;
    const int direction = armToRight ? 1 : -1;
    for (int row = 0; row < height; ++row) {
        p.put(x, y - row, kMetalProp[1]);
        p.put(x + 1, y - row, kMetalProp[0]);
    }
    p.rect(x - 1, y - 3, x + 3, y, kMetalProp[0]);
    if (broken) {
        // Snapped off: a jagged top, and the rest lying on the ground beside it.
        p.put(x, y - height, kMetalProp[0]);
        p.line(x + 6 * direction, y + 1, x + 22 * direction, y - 3, kMetalProp[0]);
        p.line(x + 6 * direction, y, x + 22 * direction, y - 4, kMetalProp[1]);
        return;
    }
    const int armY = y - height;
    p.line(x, armY, x + 8 * direction, armY - 2, kMetalProp[1]);
    p.line(x, armY + 1, x + 8 * direction, armY - 1, kMetalProp[0]);
    const int headX = x + 9 * direction;
    p.rect(headX - 2, armY - 3, headX + 3, armY - 1, kLampHead);
    p.rect(headX - 1, armY - 1, headX + 2, armY, kLampGlass);
    lights.add(headX, y, static_cast<float>(height + 2), LightKind::StreetLamp);
}

void stopSign(Paint& p, int x, int y, int lean) {
    contactShadow(p, x, y, 3, 1);
    const int height = 22;
    for (int row = 0; row < height; ++row) {
        p.put(x + lean * row / height, y - row, kMetalProp[1]);
    }
    // A small octagon, faded by years of weather.
    const int cx = x + lean;
    const int cy = y - height - 4;
    for (int oy = -5; oy <= 5; ++oy) {
        const int half = 5 - std::max(0, std::abs(oy) - 3);
        for (int ox = -half; ox <= half; ++ox) {
            const bool edge = std::abs(oy) == 5 || std::abs(ox) == half;
            p.put(cx + ox, cy + oy, edge ? kSignWhite : kSignRed);
        }
    }
    p.rect(cx - 3, cy - 1, cx + 4, cy + 1, kSignWhite);
    p.put(cx + 3, cy + 2, kSignRedDark);
    p.put(cx - 2, cy - 3, kSignRedDark);
}

void brokenSign(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 5, 2);
    const int lean = random.range(-6, 6);
    const int height = 18;
    for (int row = 0; row < height; ++row) {
        p.put(x + lean * row / height, y - row, kMetalProp[0]);
        p.put(x + 1 + lean * row / height, y - row, kMetalProp[1]);
    }
    const int cx = x + lean;
    const int cy = y - height - 3;
    const Color face = random.chance(50) ? kSignBlue : kSignRedDark;
    const int skew = random.range(-2, 2);
    for (int oy = -4; oy <= 4; ++oy) {
        const int shift = skew * oy / 4;
        for (int ox = -9; ox <= 9; ++ox) {
            if (ox > 6 && oy < 0) continue;  // A corner torn away.
            const bool edge = std::abs(oy) == 4 || std::abs(ox) == 9;
            p.put(cx + ox + shift, cy + oy, edge ? kSignWhite : face);
        }
    }
    p.rect(cx - 5, cy - 1, cx + 5, cy, kSignWhite);
    p.put(cx - 2, cy + 2, kCoal);
    p.put(cx + 4, cy - 2, kCoal);
}

void carSide(Paint& p, int x, int y, TileRandom& random) {
    const Color* paint = kCarPaints[random.range(0, 3)];
    const int half = 28;
    p.blendRect(x - half, y - 2, x + half + 1, y + 3, kPropShadow);
    const int facing = random.chance(50) ? 1 : -1;

    // Lower body.
    for (int row = 0; row < 9; ++row) {
        const int inset = row < 2 ? 1 : 0;
        for (int col = -half + inset; col <= half - inset; ++col) {
            Color color = row < 3 ? paint[0] : (row < 6 ? paint[1] : paint[2]);
            // Rust eats the lower panels in patches.
            const uint32_t noise = texelNoise((x + col) >> 2, (y - row) >> 1);
            if (row < 5 && (noise & 7u) == 0u) color = kRust[(noise >> 3) & 1u];
            p.put(x + col, y - 3 - row, color);
        }
    }
    // Hood and trunk tops catch the light: a band of the top of the car.
    p.rect(x - half + 1, y - 13, x + half, y - 11, paint[2]);
    // Cabin, offset toward the back of the car.
    const int cabinLeft = x - 14 - facing * 3;
    const int cabinRight = x + 14 - facing * 3;
    for (int row = 0; row < 9; ++row) {
        const int inset = row / 2;
        for (int col = cabinLeft + inset; col <= cabinRight - inset; ++col) {
            const bool roof = row >= 7;
            const bool pillar = col == x - facing * 3 || col <= cabinLeft + inset + 1 || col >= cabinRight - inset - 1;
            Color color = roof ? paint[2] : (pillar ? paint[1] : kCarGlass);
            if (!roof && !pillar && (texelNoise(col >> 1, row >> 1) & 7u) == 0u) color = kCarGlassShine;
            p.put(col, y - 13 - row, color);
        }
    }
    p.rect(cabinLeft + 4, y - 22, cabinRight - 3, y - 20, paint[2]);
    // Broken side window: a dark jagged hole.
    p.rect(x - facing * 10, y - 19, x - facing * 10 + 4, y - 16, kCoal);
    // Wheels, one of them gone flat.
    p.oval(x - 18, y - 2, 5, 4, kTire);
    p.oval(x - 18, y - 2, 2, 2, kMetalProp[0]);
    p.oval(x + 18, y - 1, 5, 3, kTire);
    p.oval(x + 18, y - 1, 2, 1, kMetalProp[0]);
    // Lights and a door seam.
    p.rect(x + facing * half - (facing > 0 ? 2 : 0), y - 10, x + facing * half + (facing > 0 ? 1 : 3), y - 7, kCoal);
    p.line(x - facing * 2, y - 12, x - facing * 2, y - 4, paint[0]);
}

void carEnd(Paint& p, int x, int y, TileRandom& random) {
    // A car on a road running up the screen, seen from behind and above: rear panel,
    // trunk, rear window, roof, windshield and hood, going away up the screen.
    const Color* paint = kCarPaints[random.range(0, 3)];
    const int half = 15;
    p.blendRect(x - half - 1, y - 2, x + half + 2, y + 3, kPropShadow);
    // Wheels stick out at the corners, under the body. One of them has gone flat.
    p.rect(x - half - 1, y - 8, x - half + 3, y, kTire);
    p.rect(x + half - 2, y - 7, x + half + 2, y, kTire);
    p.rect(x - half - 1, y - 40, x - half + 2, y - 33, kTire);
    p.rect(x + half - 1, y - 40, x + half + 2, y - 33, kTire);

    const auto band = [&](int fromRow, int toRow, int inset, Color color) {
        p.rect(x - half + inset, y - toRow, x + half + 1 - inset, y - fromRow, color);
    };
    band(37, 46, 2, paint[1]);    // Hood.
    band(44, 46, 4, paint[2]);    // The hood's front edge catches the light.
    band(31, 37, 3, kCarGlass);   // Windshield.
    band(20, 31, 3, paint[2]);    // Roof.
    band(16, 20, 2, kCarGlass);   // Rear window.
    band(9, 16, 1, paint[2]);     // Trunk lid.
    band(2, 9, 0, paint[0]);      // Rear panel.
    // Body sides tie it together as one shape, and rust eats them from below.
    for (int row = 9; row < 38; ++row) {
        const Color side = (texelNoise(x, y - row) & 7u) == 0u ? kRust[1] : paint[0];
        p.put(x - half + (row > 31 ? 1 : 0), y - row, side);
        p.put(x + half - (row > 31 ? 1 : 0), y - row, side);
        p.put(x - half + 1 + (row > 31 ? 1 : 0), y - row, paint[1]);
        p.put(x + half - 1 - (row > 31 ? 1 : 0), y - row, paint[1]);
    }
    // Lights, a plate, a shine on the glass, and dents.
    p.rect(x - half + 1, y - 8, x - half + 6, y - 5, kSignRedDark);
    p.rect(x + half - 5, y - 8, x + half, y - 5, kCoal);
    p.rect(x - 3, y - 6, x + 4, y - 3, kSignWhite);
    p.line(x - half + 5, y - 19, x - half + 8, y - 17, kCarGlassShine);
    p.line(x + 2, y - 36, x + 5, y - 32, kCarGlassShine);
    p.rect(x - 5, y - 27, x + 1, y - 25, paint[1]);
    p.line(x, y - 45, x, y - 38, paint[0]);
    for (int i = 0; i < 6; ++i) {
        const int rustX = x + random.range(-half + 1, half - 1);
        const int rustY = y - random.range(3, 44);
        p.rect(rustX, rustY, rustX + random.range(1, 3), rustY + 1, kRust[random.range(0, 1)]);
    }
}

void tent(Paint& p, int x, int y, TileRandom& random) {
    const Color* fabric = kTentFabric[random.range(0, 2)];
    const int half = random.range(13, 18);
    const int height = random.range(13, 17);
    const int depth = 7;
    p.blendOval(x, y, half + 4, 3, kPropShadow);
    // The long roof slope, running back from the front gable.
    for (int row = 0; row <= height; ++row) {
        const int width = half * (height - row) / height;
        // Right side: the roof seen running away.
        for (int col = 0; col <= depth * 3; ++col) {
            p.put(x + width + col, y - row - col / 3, fabric[(row & 3) == 3 ? 1 : 0]);
        }
    }
    // Front gable, with its dark opening.
    for (int row = 0; row <= height; ++row) {
        const int width = half * (height - row) / height;
        for (int col = -width; col <= width; ++col) {
            const int opening = (height - row) * half / height / 2;
            Color color = col < 0 ? fabric[2] : fabric[1];
            if (std::abs(col) < opening && row < height - 2) color = kTentDark;
            if (col == 0 && row >= height - 3) color = fabric[0];
            p.put(x + col, y - row, color);
        }
    }
    // Patches, ridge pole and guy ropes.
    p.rect(x - half / 2 - 2, y - height / 2, x - half / 2 + 1, y - height / 2 + 2, fabric[0]);
    p.line(x, y - height - 1, x + depth * 3, y - height - 1 - depth, fabric[2]);
    p.line(x - half - 3, y + 1, x - half + 4, y - 4, kFencePost);
    p.line(x + half + depth * 3 + 3, y - 5, x + half + depth * 3 - 3, y - depth - 2, kFencePost);
}

void campfire(Paint& p, int x, int y, LightList& lights) {
    p.oval(x, y, 7, 3, kAsh);
    for (int i = 0; i < 8; ++i) {
        static constexpr int kRingX[8] = {-7, -5, 0, 5, 7, 5, 0, -5};
        static constexpr int kRingY[8] = {0, -2, -3, -2, 0, 2, 3, 2};
        p.rect(x + kRingX[i] - 1, y + kRingY[i] - 1, x + kRingX[i] + 1, y + kRingY[i] + 1, kRubble[1]);
    }
    p.line(x - 4, y + 1, x + 3, y - 1, kBeamWood[0]);
    p.line(x - 3, y - 1, x + 4, y + 1, kBeamWood[1]);
    p.oval(x, y, 3, 1, kCoal);
    lights.add(x, y, 2.0f, LightKind::Fire);
}

void crate(Paint& p, int x, int y, TileRandom& random) {
    const int half = random.range(5, 7);
    const int tall = half + 2;
    const int top = 4;
    contactShadow(p, x, y, half + 2, 2);
    p.rect(x - half, y - tall, x + half + 1, y + 1, kPlank[1]);
    p.rect(x - half, y - tall - top, x + half + 1, y - tall, kPlank[2]);
    for (int col = -half; col <= half; col += 3) {
        p.rect(x + col, y - tall, x + col + 1, y + 1, kPlankGap);
    }
    p.rect(x - half, y - tall, x + half + 1, y - tall + 1, kPlankGap);
    p.rect(x - half, y, x + half + 1, y + 1, kPlank[0]);
    if (random.chance(40)) p.rect(x - 2, y - tall - top + 1, x + 3, y - tall, kPlankGap);
}

void boards(Paint& p, int x, int y, TileRandom& random) {
    const int count = random.range(3, 6);
    for (int i = 0; i < count; ++i) {
        const int ox = random.range(-14, 14);
        const int oy = random.range(-5, 5);
        const int length = random.range(10, 22);
        const int fall = random.range(-5, 5);
        const Color color = kPlank[random.range(0, 2)];
        p.line(x + ox, y + oy + 1, x + ox + length, y + oy + fall + 1, kPropShadow);
        p.line(x + ox, y + oy, x + ox + length, y + oy + fall, color);
        p.line(x + ox, y + oy - 1, x + ox + length, y + oy + fall - 1, kPlank[2]);
    }
}

void sandbags(Paint& p, int x, int y, int count, TileRandom& random) {
    contactShadow(p, x + count * 4, y + 1, count * 5, 2);
    for (int layer = 0; layer < 2; ++layer) {
        for (int i = 0; i < count - layer; ++i) {
            const int bx = x + i * 8 + layer * 4 + random.range(-1, 1);
            const int by = y - layer * 4;
            p.oval(bx + 4, by - 2, 4, 2, kSandbag[1]);
            p.rect(bx + 2, by - 4, bx + 6, by - 3, kSandbag[2]);
            p.rect(bx + 2, by - 1, bx + 7, by, kSandbag[0]);
        }
    }
}

void tires(Paint& p, int x, int y) {
    contactShadow(p, x, y, 8, 3);
    p.oval(x, y - 1, 7, 3, kTire);
    p.oval(x, y - 4, 7, 3, kCoal);
    p.oval(x, y - 7, 7, 3, kTire);
    p.oval(x, y - 7, 3, 1, kAsphaltCrack);
}

void barrier(Paint& p, int x, int y, TileRandom& random) {
    const int half = random.range(14, 20);
    p.blendRect(x - half, y - 1, x + half + 1, y + 2, kPropShadow);
    p.rect(x - half, y - 8, x + half + 1, y, kConcreteFloor[1]);
    p.rect(x - half + 1, y - 11, x + half, y - 8, kConcreteFloor[2]);
    p.rect(x - half, y - 3, x + half + 1, y, kConcreteFloor[0]);
    for (int col = -half; col < half; col += 8) {
        p.rect(x + col, y - 6, x + col + 4, y - 4, kPaintYellow);
    }
    // A broken end, showing rebar.
    const int end = random.chance(50) ? -half : half;
    p.rect(x + end - 2, y - 11, x + end + 2, y - 5, kFloorStain);
    p.put(x + end, y - 13, kRebar);
    p.put(x + end, y - 12, kRebar);
}

void sofa(Paint& p, int x, int y, TileRandom& random) {
    const Color color = kCloth[random.range(0, 2)];
    const Color dark{static_cast<unsigned char>(color.r * 3 / 4), static_cast<unsigned char>(color.g * 3 / 4), static_cast<unsigned char>(color.b * 3 / 4), 255};
    contactShadow(p, x, y, 13, 2);
    p.rect(x - 12, y - 6, x + 13, y, dark);        // Front of the seat.
    p.rect(x - 12, y - 10, x + 13, y - 6, color);  // Seat.
    p.rect(x - 12, y - 16, x + 13, y - 10, dark);  // Backrest.
    p.rect(x - 12, y - 17, x + 13, y - 16, color);
    p.rect(x - 14, y - 12, x - 11, y, color);      // Arms.
    p.rect(x + 12, y - 12, x + 15, y, color);
    p.rect(x - 3, y - 9, x + 3, y - 7, kPlankGap);  // A burst cushion.
    p.put(x - 6, y - 13, kCoal);
    p.put(x + 6, y - 14, kCoal);
}

void table(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 11, 2);
    const int tilt = random.range(-1, 1);
    p.rect(x - 10, y - 4, x - 8, y, kPlank[0]);
    p.rect(x + 8, y - 4 + tilt, x + 10, y, kPlank[0]);
    p.rect(x - 10, y - 11, x + 11, y - 6, kPlank[2]);
    p.rect(x - 10, y - 6, x + 11, y - 4, kPlank[0]);
    p.line(x - 5, y - 11, x - 3, y - 6, kPlankGap);
    p.line(x + 4, y - 11, x + 5, y - 6, kPlankGap);
}

void bedFrame(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y + 1, 11, 2);
    p.rect(x - 10, y - 14, x + 10, y - 3, kMetalProp[0]);
    p.rect(x - 9, y - 13, x + 9, y - 3, kCloth[random.range(0, 2)]);
    p.rect(x - 8, y - 12, x - 1, y - 9, kSignWhite);
    p.rect(x - 11, y - 18, x - 9, y, kMetalProp[1]);
    p.rect(x + 9, y - 18, x + 11, y, kMetalProp[1]);
    p.rect(x - 2, y - 7, x + 4, y - 5, kPlankGap);
}

void bench(Paint& p, int x, int y) {
    contactShadow(p, x, y, 11, 2);
    p.rect(x - 10, y - 3, x - 8, y, kMetalProp[0]);
    p.rect(x + 8, y - 3, x + 10, y, kMetalProp[0]);
    p.rect(x - 11, y - 6, x + 12, y - 3, kPlank[1]);
    p.rect(x - 11, y - 11, x + 12, y - 8, kPlank[2]);
    p.rect(x - 9, y - 8, x - 8, y - 6, kMetalProp[0]);
    p.rect(x + 8, y - 8, x + 9, y - 6, kMetalProp[0]);
}

void deadTree(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 7, 2);
    const int tall = random.range(20, 30);
    // A trunk that narrows, and a few bare branches.
    for (int row = 0; row < tall; ++row) {
        const int half = row < tall / 3 ? 2 : 1;
        const int lean = row / 6;
        for (int col = -half; col <= half; ++col) {
            const int shade = col < 0 ? 2 : (col > 0 ? 0 : 1);
            p.put(x + col + lean, y - row, kTreeBark[shade]);
        }
    }
    for (int i = 0; i < 5; ++i) {
        const int fromRow = tall / 2 + i * tall / 10;
        const int side = i % 2 == 0 ? 1 : -1;
        const int length = random.range(6, 14);
        const int rise = random.range(2, 8);
        const int startX = x + fromRow / 6;
        p.line(startX, y - fromRow, startX + side * length, y - fromRow - rise, kTreeBark[1]);
        p.line(startX + side * length / 2, y - fromRow - rise / 2, startX + side * (length / 2 + 4), y - fromRow - rise / 2 - 5, kTreeBark[0]);
    }
}

void fenceAcross(Paint& p, int x0, int x1, int y, TileRandom& random) {
    constexpr int kTall = 15;
    constexpr int kPostGap = 22;
    p.blendRect(x0, y, x1, y + 2, kPropShadow);
    int gapStart = 1 << 30;
    int gapEnd = -1;
    if (random.chance(70)) {
        gapStart = random.range(x0, x1 - 20);
        gapEnd = gapStart + random.range(12, 34);
    }
    for (int x = x0; x < x1; ++x) {
        if (x >= gapStart && x < gapEnd) continue;
        const int relative = x - x0;
        const int sag = (relative / kPostGap) % 3 == 1 ? 1 : 0;
        for (int row = 0; row < kTall - sag; ++row) {
            // Diagonal mesh: two sets of crossing lines make the diamonds.
            if ((relative + row) % 4 == 0 || (relative - row + 400) % 4 == 0) p.blend(x, y - row, kFenceMesh);
        }
        p.put(x, y - kTall + sag, kFencePost);
    }
    for (int x = x0; x <= x1; x += kPostGap) {
        if (x >= gapStart && x < gapEnd) continue;
        const int lean = (x / kPostGap) % 4 == 0 ? 1 : 0;
        for (int row = 0; row <= kTall + 1; ++row) p.put(x + lean * row / 8, y - row, kFencePost);
        p.put(x, y, kMetalProp[0]);
    }
}

void fenceAlong(Paint& p, int x, int y0, int y1, TileRandom& random) {
    constexpr int kTall = 15;
    constexpr int kPostGap = 14;
    int gapStart = 1 << 30;
    int gapEnd = -1;
    if (random.chance(60)) {
        gapStart = random.range(y0, y1 - 12);
        gapEnd = gapStart + random.range(8, 22);
    }
    // A fence running up the screen is seen nearly edge on: a thin band of mesh.
    for (int y = y0; y < y1; ++y) {
        if (y >= gapStart && y < gapEnd) continue;
        if ((y & 1) == 0) {
            p.blend(x, y - kTall, kFenceMesh);
            p.blend(x + 1, y - kTall + 2, kFenceMesh);
        }
        p.blend(x + 2, y, kPropShadow);
    }
    for (int y = y0; y < y1; y += kPostGap) {
        if (y >= gapStart && y < gapEnd) continue;
        for (int row = 0; row <= kTall; ++row) p.put(x, y - row, kFencePost);
    }
}

void rubblePile(Paint& p, int x, int y, int radius, TileRandom& random) {
    // Lower rocks first, stacked toward the middle, so the pile looks like a mound.
    p.blendOval(x, y + 1, radius + 2, radius / 2 + 1, kPropShadow);
    const int count = radius * radius / 3 + 8;
    for (int i = 0; i < count; ++i) {
        const int angleX = random.range(-radius, radius);
        const int reach = radius - std::abs(angleX) * 2 / 3;
        const int ox = angleX;
        const int oy = random.range(-reach / 2, reach / 2);
        const int rise = (radius - std::abs(ox)) * 2 / 3 + (reach / 2 - oy) / 2;
        const int size = random.range(1, 3);
        const Color color = kRubble[random.range(0, 3)];
        const int bx = x + ox;
        const int by = y + oy - rise;
        p.rect(bx - size, by - size, bx + size, by, color);
        p.rect(bx - size, by, bx + size, by + 1, kRubble[0]);
    }
}

void looseBricks(Paint& p, int x, int y, int radius, int count, TileRandom& random) {
    for (int i = 0; i < count; ++i) {
        const int bx = x + random.range(-radius, radius);
        const int by = y + random.range(-radius / 2, radius / 2);
        const Color color = random.chance(60) ? kBrick[random.range(0, 2)] : kRubble[random.range(0, 3)];
        p.rect(bx, by, bx + random.range(2, 4), by + 2, color);
        p.put(bx, by + 2, kRubble[0]);
    }
}

void beam(Paint& p, int x0, int y0, int x1, int y1) {
    p.line(x0, y0 + 3, x1, y1 + 3, kPropShadow);
    p.line(x0, y0 + 2, x1, y1 + 2, kBeamWood[0]);
    p.line(x0, y0 + 1, x1, y1 + 1, kBeamWood[1]);
    p.line(x0, y0, x1, y1, kBeamWood[2]);
}

void weedTuft(Paint& p, int x, int y, TileRandom& random) {
    const int blades = random.range(2, 5);
    for (int i = 0; i < blades; ++i) {
        const int ox = random.range(-3, 3);
        const int tall = random.range(1, 4);
        const Color color = kWeed[random.range(0, 2)];
        p.line(x + ox, y, x + ox + random.range(-1, 1), y - tall, color);
    }
    p.put(x, y, kWeed[0]);
}

void puddle(Paint& p, int x, int y, int rx, int ry) {
    p.oval(x, y, rx, ry, kPuddle);
    p.oval(x - rx / 4, y - ry / 4, rx / 2, std::max(1, ry / 3), kPuddleShine);
}

}  // namespace ls::maps::city
