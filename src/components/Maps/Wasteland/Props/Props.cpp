#include "components/Maps/Wasteland/Props/Props.h"

#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::wasteland {

using namespace theme;

namespace {

// Draws in a frame that is tilted a little, so a crashed car does not sit square to the
// road. Each column just slides up or down, which keeps the pixels crisp.
struct Slant {
    Paint& p;
    int x, y, shear;
    int rowAt(int col, int row) const { return y - row + col * shear / 32; }
    void put(int col, int row, Color color) const { p.put(x + col, rowAt(col, row), color); }
    void blend(int col, int row, Color color) const { p.blend(x + col, rowAt(col, row), color); }
    void rect(int col0, int row0, int col1, int row1, Color color) const {
        for (int col = col0; col < col1; ++col) {
            for (int row = row0; row < row1; ++row) put(col, row, color);
        }
    }
};

// Rust grows in patches, and more of it near the ground.
bool rustHere(int x, int row, int y) {
    const uint32_t noise = texelNoise((x + row * 3) >> 2, (y - row) >> 1);
    return row < 6 ? (noise & 3u) == 0u : (noise & 7u) == 0u;
}

void wheel(const Slant& s, int col, bool flat) {
    if (flat) {
        // Only the rim is left, sunk in sand.
        s.rect(col - 3, 0, col + 4, 3, kMetalProp[0]);
        s.rect(col - 2, 2, col + 3, 3, kRust[0]);
        return;
    }
    const int cy = s.rowAt(col, 2);
    s.p.oval(s.x + col, cy, 5, 4, kTire);
    s.p.oval(s.x + col, cy, 2, 2, kMetalProp[0]);
    s.p.put(s.x + col - 1, cy - 1, kRust[1]);
}

// Lower part of a wreck buried by the wind.
void driftAgainst(Paint& p, int x, int y, int rx, TileRandom& random) {
    const int side = random.chance(50) ? 1 : -1;
    sandMound(p, x + side * rx / 2 + random.range(-4, 4), y + 1, rx, random.range(3, 5));
}

const Color* wreckPaint(TileRandom& random, bool burnt) {
    const int pick = random.range(0, 4);
    return burnt ? kWreckBurnt : kWreckPaints[pick];
}

}  // namespace

// --- Small ground texture -----------------------------------------------------

void stone(Paint& p, int x, int y, int size, TileRandom& random) {
    const int h = std::max(1, size * 2 / 3);
    const int tone = random.range(0, 1);
    p.blendOval(x + 1, y + 1, size + 1, h, kPropShadow);
    p.oval(x, y, size, h, kStone[tone + 1]);
    p.rect(x - size / 2, y - h, x + size / 2 + 1, y - h + 1, kStone[tone + 2]);
    p.put(x + size - 1, y, kStone[tone]);
}

void pebbles(Paint& p, int x, int y, int spread, int count, TileRandom& random) {
    for (int i = 0; i < count; ++i) {
        const int px = x + random.range(-spread, spread);
        const int py = y + random.range(-spread / 2, spread / 2);
        p.put(px, py, kStone[random.range(1, 3)]);
        if (random.chance(40)) p.put(px + 1, py, kStone[random.range(1, 2)]);
        p.put(px, py + 1, kStone[0]);
    }
}

void tuft(Paint& p, int x, int y, TileRandom& random) {
    const int blades = random.range(3, 7);
    for (int i = 0; i < blades; ++i) {
        const int ox = random.range(-4, 4);
        const int tall = random.range(2, 6);
        p.line(x + ox / 2, y, x + ox + ox / 2, y - tall, kDryGrass[random.range(0, 2)]);
    }
    p.put(x, y + 1, kDryGrass[0]);
    p.blend(x + 2, y + 1, kPropShadow);
}

void bush(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x + 1, y, 7, 2);
    const int tall = random.range(6, 10);
    for (int i = 0; i < 12; ++i) {
        const int tipX = x + random.range(-8, 8);
        const int tipY = y - random.range(1, tall);
        p.line(x + random.range(-1, 1), y, tipX, tipY, i < 7 ? kTwig : kDeadWood[1]);
        if (random.chance(55)) p.put(tipX, tipY, kDryGrass[random.range(0, 1)]);
        if (random.chance(25)) p.put(tipX + 1, tipY + 1, kDryGrass[0]);
    }
}

void tumbleweed(Paint& p, int x, int y, TileRandom& random) {
    static constexpr int kRimX[8] = {5, 4, 0, -4, -5, -4, 0, 4};
    static constexpr int kRimY[8] = {0, -3, -4, -3, 0, 3, 4, 3};
    contactShadow(p, x + 1, y + 4, 5, 1);
    for (int i = 0; i < 12; ++i) {
        const int a = random.range(0, 7);
        const int b = random.range(0, 7);
        p.line(x + kRimX[a], y + kRimY[a], x + kRimX[b], y + kRimY[b], i < 8 ? kTwig : kDryGrass[0]);
    }
}

void sandMound(Paint& p, int x, int y, int rx, int ry) {
    // A soft heap with no outline: its rim breaks up into grains, so it melts into the
    // ground, and its lit side faces the same way as everything else.
    const int width = std::max(1, rx);
    const int height = std::max(1, ry);
    for (int oy = -height; oy <= height; ++oy) {
        for (int ox = -width; ox <= width; ++ox) {
            const int reach = (ox * ox * 16) / (width * width) + (oy * oy * 16) / (height * height);
            const uint32_t noise = texelNoise(x + ox, y + oy);
            if (reach + static_cast<int>(noise & 7u) > 18) continue;
            const int light = ox * 2 / std::max(1, width / 2) + oy * 4 / height + static_cast<int>((noise >> 3) & 1u);
            p.put(x + ox, y + oy, light <= -2 ? kSand[3] : (light <= 0 ? kSand[2] : (light <= 2 ? kSand[1] : kSandShade)));
        }
    }
}

void tirePile(Paint& p, int x, int y, TileRandom& random) {
    // A low stack, or a few lying flat: the holes and the lit tops are what say "tire".
    const int stack = random.range(1, 3);
    contactShadow(p, x + 1, y + 1, 8, 2);
    CoverMark cover(p, x, y + 2, CoverKind::Box);
    for (int i = 0; i < stack; ++i) {
        const int ty = y - i * 3;
        const int tx = x + random.range(-1, 1);
        p.oval(tx, ty, 7, 3, kTire);
        p.oval(tx, ty - 1, 6, 2, kTireTop);
        p.oval(tx, ty - 1, 3, 1, kCoal);
    }
    if (random.chance(50)) {
        const int ox = x + random.range(10, 16) * (random.chance(50) ? 1 : -1);
        p.oval(ox, y + 2, 6, 3, kTire);
        p.oval(ox, y + 1, 5, 2, kTireTop);
        p.oval(ox, y + 1, 2, 1, kTireInner);
    }
}

void groundStain(Paint& p, int x, int y, int rx, int ry, Color color) {
    // Solid in the middle, then thinning out grain by grain, so no outline ever shows.
    const int x0 = std::max(x - rx, p.left());
    const int x1 = std::min(x + rx, p.right() - 1);
    const int y0 = std::max(y - ry, p.top());
    const int y1 = std::min(y + ry, p.bottom() - 1);
    for (int py = y0; py <= y1; ++py) {
        for (int px = x0; px <= x1; ++px) {
            const int ox = px - x;
            const int oy = py - y;
            const int reach = ox * ox * 32 / (rx * rx) + oy * oy * 32 / (ry * ry);
            if (reach > 32) continue;
            if (reach > 12 && static_cast<int>(texelNoise(px, py) & 31u) < (reach - 12) * 3 / 2) continue;
            p.blend(px, py, color);
        }
    }
}

void litter(Paint& p, int x, int y, TileRandom& random) {
    switch (random.range(0, 3)) {
        case 0:  // A flattened can.
            p.rect(x, y, x + 3, y + 2, kRust[random.range(0, 2)]);
            p.put(x + 1, y, kMetalProp[1]);
            break;
        case 1:  // A rag half under the sand.
            p.rect(x - 2, y, x + 3, y + 1, kCloth[random.range(0, 2)]);
            p.put(x - 1, y + 1, kCloth[0]);
            break;
        case 2:  // Broken glass.
            for (int i = 0; i < 5; ++i) p.put(x + random.range(-3, 3), y + random.range(-1, 1), kDustyGlassShine);
            break;
        default:  // An old plank.
            p.line(x - 4, y + 1, x + 4, y, kPlank[1]);
            p.line(x - 4, y + 2, x + 4, y + 1, kPlankGap);
            break;
    }
}

// --- Rocks --------------------------------------------------------------------

void boulder(Paint& p, int x, int y, int halfWidth, TileRandom& random) {
    rockShelf(p, x, y, halfWidth, std::max(3, halfWidth * 3 / 4), random.chance(30), random);
}

void rockShelf(Paint& p, int x, int y, int halfWidth, int height, bool red, TileRandom& random) {
    const Color* tones = red ? kRockRed : kRock;
    // Each column has its own height, stepping in ledges and falling away at both ends.
    // That gives a blocky broken skyline, which is what makes a lump read as stone.
    constexpr int kMaxColumns = 241;
    int tops[kMaxColumns];
    const int columns = std::min(kMaxColumns, halfWidth * 2 + 1);
    int level = height;
    for (int col = 0; col < columns;) {
        const int run = random.range(3, 10);
        level = std::clamp(level + random.range(-4, 4), height * 3 / 5, height);
        for (int i = 0; i < run && col < columns; ++i) tops[col++] = level;
    }
    for (int col = 0; col < columns; ++col) {
        const int edge = std::min(col, columns - 1 - col);
        if (edge * 2 < tops[col]) tops[col] = edge * 2 + static_cast<int>(texelNoise(x + col, y) & 1u);
    }
    p.blendOval(x + halfWidth / 4, y + 1, halfWidth + 6, std::max(2, height / 4), kPropShadow);
    p.blendOval(x + halfWidth / 6, y + 1, halfWidth * 3 / 4 + 2, std::max(1, height / 6), kGroundShadowCore);
    const int capRows = std::max(2, height / 4);
    // Its face stands on the shelf's foot row, so the whole rock is one cover. The skirt of
    // stones after it lies flat on the ground and covers nothing.
    CoverMark cover(p, x, y, CoverKind::Rock);
    for (int col = 0; col < columns; ++col) {
        const int px = x - halfWidth + col;
        const int top = tops[col];
        const int across = col * 16 / columns;
        // Beds of rock wander up and down, and a crack only runs part of the way down.
        const int sway = softNoise(px, y, 4, 0) >> 5;
        const uint32_t columnHash = texelNoise(px, y);
        const int crackDown = (columnHash & 15u) == 0u ? static_cast<int>((columnHash >> 4) & 7u) + 4 : 0;
        const bool afterStep = col > 2 && tops[col - 3] > top + 1;
        for (int row = 0; row < top; ++row) {
            // The face goes from lit on the left to dark on the right, in hard steps.
            int tone = across < 5 ? 2 : (across < 11 ? 1 : 0);
            const uint32_t noise = texelNoise(px, y - row);
            if ((row + sway) % 6 == 0 && (noise & 3u) != 0u && tone > 0) tone--;
            if ((noise & 15u) == 0u && tone < 2) tone++;
            if (afterStep && tone > 0) tone--;
            if (top - row <= crackDown) tone = 0;
            if (row == 0) tone = 0;
            p.put(px, y - row, tones[tone]);
        }
        // The flat top catches the sun. Only ledges high enough get one.
        if (top < 3) continue;
        const int cap = std::min(capRows, top / 2 + 1);
        for (int row = 0; row < cap; ++row) {
            const uint32_t noise = texelNoise(px, y - top - row);
            p.put(px, y - top - row, (noise & 7u) == 0u ? tones[2] : tones[3]);
        }
        p.put(px, y - top, tones[2]);
    }
    p.endCover();
    // A skirt of fallen stones along the foot.
    const int skirt = halfWidth / 2 + 4;
    for (int i = 0; i < skirt; ++i) {
        const int sx = x + random.range(-halfWidth - 5, halfWidth + 6);
        const int sy = y + random.range(0, 3);
        p.rect(sx, sy, sx + random.range(1, 3), sy + 1, tones[random.range(1, 3)]);
        p.put(sx, sy + 1, tones[0]);
    }
}

// --- Wrecks -------------------------------------------------------------------

void carSide(Paint& p, int x, int y, TileRandom& random) {
    const bool burnt = random.chance(22);
    const bool pickup = random.chance(25);
    const Color* paint = wreckPaint(random, burnt);
    const int facing = random.chance(50) ? 1 : -1;
    const Slant s{p, x, y, random.range(-5, 5)};
    const int half = 27;
    for (int col = -half - 1; col <= half + 1; ++col) {
        for (int row = -2; row < 2; ++row) s.blend(col, row, kPropShadow);
    }
    for (int col = -half + 3; col <= half - 3; ++col) {
        for (int row = -1; row < 1; ++row) s.blend(col, row, kGroundShadowCore);
    }
    const Color top = shade(paint[2], kShadeTopPercent);
    // Lower body, with rust eating the panels. Its far end turns away from the light.
    for (int row = 0; row < 9; ++row) {
        const int inset = row < 2 ? 1 : 0;
        for (int col = -half + inset; col <= half - inset; ++col) {
            Color color = row < 3 ? paint[0] : (row < 6 ? paint[1] : paint[2]);
            if (rustHere(x + col, row, y)) color = kRust[burnt ? 0 : (row < 4 ? 0 : 1)];
            if (col >= half - inset - 1) color = shade(color, kShadeSidePercent);
            s.put(col, row + 3, color);
        }
    }
    s.rect(-half + 1, 3, half, 4, shade(paint[0], 70));
    // Hood and trunk face the sky.
    s.rect(-half + 1, 12, half, 14, top);
    s.rect(-half + 2, 13, half - 1, 14, shade(top, 110));
    // The cabin sits back from the hood. A pickup has an open bed behind a short cab.
    const int cabCenter = pickup ? facing * 8 : -facing * 3;
    const int cabHalf = pickup ? 10 : 14;
    for (int row = 0; row < 9; ++row) {
        const int inset = row / 2;
        for (int col = cabCenter - cabHalf + inset; col <= cabCenter + cabHalf - inset; ++col) {
            const bool roof = row >= 7;
            const bool pillar = col == cabCenter || col <= cabCenter - cabHalf + inset + 1 || col >= cabCenter + cabHalf - inset - 1;
            Color color = roof ? top : (pillar ? paint[1] : (burnt ? kCoal : kDustyGlass));
            if (!roof && !pillar && !burnt && (texelNoise((x + col) >> 1, row >> 1) & 7u) == 0u) color = kDustyGlassShine;
            s.put(col, row + 14, color);
        }
    }
    if (pickup) {
        const int bedLeft = facing > 0 ? -half + 2 : 6;
        s.rect(bedLeft, 10, bedLeft + 20, 13, kCoal);
        s.rect(bedLeft, 13, bedLeft + 20, 14, paint[1]);
    }
    s.rect(-facing * 10, 15, -facing * 10 + 4, 19, kCoal);  // A broken window.
    s.rect(cabCenter - 4, 8, cabCenter - 3, 13, paint[0]);  // The door seam.
    s.rect(facing > 0 ? half - 3 : -half, 7, facing > 0 ? half + 1 : -half + 4, 10, kCoal);
    wheel(s, -17, random.chance(35));
    wheel(s, 17, random.chance(35));
    // Sand heaped on the side the wind comes from.
    if (random.chance(55)) driftAgainst(p, x, y, random.range(10, 20), random);
    pebbles(p, x, y + 3, 26, 5, random);
}

void carOnRoof(Paint& p, int x, int y, TileRandom& random) {
    const Slant s{p, x, y, random.range(-4, 4)};
    const Color* paint = wreckPaint(random, false);
    const int half = 26;
    for (int col = -half - 1; col <= half + 1; ++col) {
        for (int row = -2; row < 2; ++row) s.blend(col, row, kPropShadow);
    }
    for (int col = -half + 3; col <= half - 3; ++col) s.blend(col, 0, kGroundShadowCore);
    // A crushed roof on the ground, and the rusty underside above it.
    for (int col = -14; col <= 14; ++col) {
        for (int row = 0; row < 5; ++row) s.put(col, row, row < 2 ? paint[0] : ((col + 40) % 9 < 2 ? paint[1] : kCoal));
    }
    for (int row = 5; row < 12; ++row) {
        for (int col = -half; col <= half; ++col) {
            Color color = row < 8 ? paint[1] : kRust[(texelNoise((x + col) >> 1, row) & 3u) == 0u ? 0 : 1];
            if (col >= half - 1) color = shade(color, kShadeSidePercent);
            s.put(col, row, color);
        }
    }
    s.rect(-half + 2, 12, half - 1, 14, kRust[0]);
    s.rect(-half + 2, 13, half - 1, 14, shade(kRust[1], kShadeTopPercent));
    s.rect(-14, 11, 15, 13, kCoal);  // The axle tunnel.
    for (int col : {-18, 18}) {
        const int cy = s.rowAt(col, 14);
        p.oval(x + col, cy, 5, 3, kTire);
        p.oval(x + col, cy, 2, 1, kMetalProp[0]);
    }
    if (random.chance(45)) driftAgainst(p, x, y, random.range(10, 18), random);
}

void carEnd(Paint& p, int x, int y, TileRandom& random) {
    // Rear panel, trunk, rear window, roof, windshield and hood, going away up the screen.
    const bool burnt = random.chance(22);
    const Color* paint = wreckPaint(random, burnt);
    const int half = 15;
    const int skew = random.range(-3, 3);
    const auto rowX = [&](int row) { return x + skew * row / 16; };
    p.blendRect(x - half - 1, y - 2, x + half + 2, y + 3, kPropShadow);
    p.blendRect(x - half + 2, y - 1, x + half - 1, y + 2, kGroundShadowCore);
    const Color top = shade(paint[2], kShadeTopPercent);
    const auto band = [&](int fromRow, int toRow, int inset, Color color) {
        for (int row = fromRow; row < toRow; ++row) {
            p.rect(rowX(row) - half + inset, y - row, rowX(row) + half + 1 - inset, y - row + 1, color);
        }
    };
    const bool flat = random.chance(40);
    p.rect(rowX(1) - half - 1, y - 8, rowX(1) - half + 3, y - (flat ? 3 : 0), kTire);
    p.rect(rowX(1) + half - 2, y - 7, rowX(1) + half + 2, y, kTire);
    p.rect(rowX(36) - half - 1, y - 40, rowX(36) - half + 2, y - 33, kTire);
    p.rect(rowX(36) + half - 1, y - 40, rowX(36) + half + 2, y - 33, kTire);
    band(37, 46, 2, paint[1]);
    band(44, 46, 4, top);
    band(31, 37, 3, burnt ? kCoal : kDustyGlass);
    band(20, 31, 3, top);  // The roof faces the sky.
    band(16, 20, 2, burnt ? kCoal : kDustyGlass);
    band(9, 16, 1, paint[2]);
    band(2, 9, 0, paint[0]);
    band(2, 3, 0, shade(paint[0], 70));
    for (int row = 9; row < 38; ++row) {
        const bool rusty = (texelNoise(x, y - row) & 3u) == 0u;
        const int inset = row > 31 ? 1 : 0;
        // Lit on the left, in shade on the right, so the body reads as a box.
        p.put(rowX(row) - half + inset, y - row, rusty ? kRust[1] : paint[1]);
        p.put(rowX(row) + half - inset, y - row, rusty ? kRust[1] : shade(paint[0], kShadeSidePercent));
    }
    p.rect(rowX(5) - half + 1, y - 8, rowX(5) - half + 6, y - 5, kCoal);
    p.rect(rowX(5) + half - 5, y - 8, rowX(5) + half, y - 5, kSignRedDark);
    // Rust spots across the panels, and sand along the windward side.
    for (int i = 0; i < 10; ++i) {
        const int rustX = x + random.range(-half + 1, half - 1);
        const int rustY = y - random.range(3, 44);
        p.rect(rustX, rustY, rustX + random.range(1, 4), rustY + random.range(1, 2), kRust[random.range(0, 1)]);
    }
    if (random.chance(55)) driftAgainst(p, x, y + 1, random.range(10, 18), random);
}

void busSide(Paint& p, int x, int y, TileRandom& random) {
    const Slant s{p, x, y, random.range(-2, 2)};
    const Color* paint = random.chance(70) ? kBusPaint : kWreckPaints[random.range(0, 4)];
    const int half = 48;
    for (int col = -half - 1; col <= half + 1; ++col) {
        for (int row = -2; row < 2; ++row) s.blend(col, row, kPropShadow);
    }
    for (int col = -half + 4; col <= half - 4; ++col) {
        for (int row = -1; row < 1; ++row) s.blend(col, row, kGroundShadowCore);
    }
    for (int col = -half; col <= half; ++col) {
        const bool farEnd = col >= half - 1;
        for (int row = 3; row < 25; ++row) {
            Color color = row < 9 ? paint[0] : (row < 15 ? paint[1] : paint[2]);
            if (row == 13) color = kSignInk;  // The rub rail along the side.
            if (rustHere(x + col, row, y)) color = kRust[row < 8 ? 0 : 1];
            if (farEnd) color = shade(color, kShadeSidePercent);
            s.put(col, row, color);
        }
        // The roof edge faces the sky, the brightest line of the bus.
        s.put(col, 25, shade(paint[2], kShadeTopPercent));
        s.put(col, 26, kRust[(texelNoise((x + col) >> 3, 7) & 3u) == 0u ? 1 : 2]);
        s.put(col, 27, shade(paint[2], kShadeTopPercent));
    }
    // A row of windows, most of them smashed through.
    for (int col = -half + 6; col < half - 8; col += 11) {
        const bool glass = (texelNoise(x + col, y) & 3u) == 0u;
        s.rect(col, 16, col + 8, 23, glass ? kDustyGlass : kCoal);
        if (glass) s.rect(col + 1, 21, col + 3, 22, kDustyGlassShine);
    }
    const int door = random.chance(50) ? half - 7 : -half + 2;
    s.rect(door, 3, door + 5, 23, kCoal);
    wheel(s, -half + 16, random.chance(40));
    wheel(s, half - 18, random.chance(40));
    driftAgainst(p, x + random.range(-30, 30), y, random.range(14, 28), random);
    pebbles(p, x, y + 3, 44, 6, random);
}

void busEnd(Paint& p, int x, int y, TileRandom& random) {
    const Color* paint = random.chance(70) ? kBusPaint : kWreckPaints[random.range(0, 4)];
    const int half = 19;
    p.blendRect(x - half - 1, y - 2, x + half + 2, y + 3, kPropShadow);
    p.blendRect(x - half + 2, y - 1, x + half - 1, y + 2, kGroundShadowCore);
    p.rect(x - half - 1, y - 8, x - half + 3, y, kTire);
    p.rect(x + half - 2, y - 8, x + half + 2, y, kTire);
    // Back panel, facing us, then the long roof running away from us and facing the sky.
    p.rect(x - half, y - 18, x + half + 1, y - 2, paint[0]);
    p.rect(x - half + 2, y - 16, x + half - 1, y - 9, kCoal);
    p.rect(x - 2, y - 16, x + 2, y - 3, paint[1]);
    p.rect(x - half + 1, y - 66, x + half, y - 18, shade(paint[2], kShadeTopPercent));
    p.rect(x - half + 1, y - 19, x + half, y - 18, shade(paint[2], kShadeTopPercent + 14));
    for (int row = 22; row < 66; row += 9) p.rect(x - half + 2, y - row, x + half - 1, y - row + 1, paint[1]);
    p.rect(x - half, y - 66, x - half + 2, y - 18, paint[1]);
    p.rect(x + half - 1, y - 66, x + half + 1, y - 18, shade(paint[0], kShadeSidePercent));
    for (int i = 0; i < 18; ++i) {
        const int rx = x + random.range(-half + 2, half - 3);
        const int ry = y - random.range(19, 65);
        p.rect(rx, ry, rx + random.range(1, 5), ry + random.range(1, 3), kRust[random.range(0, 2)]);
    }
    // The roof caved in near the front.
    p.rect(x - 6, y - 58, x + 8, y - 46, kCoal);
    p.line(x - 6, y - 52, x + 8, y - 50, kRust[1]);
    driftAgainst(p, x, y + 1, random.range(14, 24), random);
}

void tankerSide(Paint& p, int x, int y, TileRandom& random) {
    const Slant s{p, x, y, random.range(-2, 2)};
    const int facing = random.chance(50) ? 1 : -1;
    const int half = 58;
    for (int col = -half - 1; col <= half + 1; ++col) {
        for (int row = -2; row < 2; ++row) s.blend(col, row, kPropShadow);
    }
    for (int col = -half + 4; col <= half - 4; ++col) {
        for (int row = -1; row < 1; ++row) s.blend(col, row, kGroundShadowCore);
    }
    // The tank is a long lying cylinder: hard bands of tone sell the curve.
    const int tankLeft = facing > 0 ? -half : -half + 26;
    const int tankRight = tankLeft + 90;
    for (int col = tankLeft; col < tankRight; ++col) {
        const int end = std::min(col - tankLeft, tankRight - 1 - col);
        const int inset = end < 2 ? 2 - end : 0;
        for (int row = 5 + inset; row < 23 - inset; ++row) {
            int tone = row < 9 ? 0 : (row < 18 ? 1 : 2);
            if (rustHere(x + col, row - 3, y)) {
                s.put(col, row, kRust[row < 12 ? 0 : 1]);
                continue;
            }
            // The top of the curve faces the sky, and the far end of the tank is in shade.
            Color color = kTankerPaint[tone];
            if (row >= 20) color = shade(color, kShadeTopPercent);
            if (end < 3 && col > (tankLeft + tankRight) / 2) color = shade(color, kShadeSidePercent);
            s.put(col, row, color);
        }
    }
    for (int col = tankLeft + 18; col < tankRight - 8; col += 22) s.rect(col, 5, col + 1, 23, kTankerPaint[0]);
    s.rect(tankLeft + 8, 3, tankRight - 6, 5, kMetalProp[0]);
    // The cab, its glass gone.
    const int cabLeft = facing > 0 ? tankRight + 2 : -half;
    const Color* paint = kWreckPaints[random.range(0, 4)];
    s.rect(cabLeft, 3, cabLeft + 22, 13, paint[1]);
    s.rect(cabLeft + (facing > 0 ? 0 : 6), 13, cabLeft + (facing > 0 ? 16 : 22), 24, paint[1]);
    s.rect(cabLeft + (facing > 0 ? 3 : 9), 15, cabLeft + (facing > 0 ? 13 : 19), 21, kCoal);
    s.rect(cabLeft + (facing > 0 ? 0 : 6), 24, cabLeft + (facing > 0 ? 16 : 22), 25, paint[2]);
    wheel(s, tankLeft + 10, random.chance(30));
    wheel(s, tankLeft + 22, random.chance(30));
    wheel(s, tankRight - 12, random.chance(30));
    wheel(s, cabLeft + 11, false);
    // A dark spill where the tank split.
    p.blendOval(x + random.range(-24, 24), y + 6, random.range(18, 30), random.range(3, 5), kOilStain);
    driftAgainst(p, x + random.range(-30, 30), y, random.range(14, 26), random);
}

void wreckFire(Paint& p, int x, int y, LightList& lights) {
    p.blendOval(x, y + 2, 18, 4, kOilStain);
    lights.add(x, y, 12.0f, LightKind::Fire);
}

// --- By the road --------------------------------------------------------------

void telephonePole(Paint& p, int x, int y, int lean, bool snapped) {
    contactShadow(p, x, y, 3, 1);
    const int tall = snapped ? 12 : 38;
    {
        CoverMark cover(p, x, y, CoverKind::Pole);
        // A round wooden pole: a lit edge, its body, and the side in shade.
        for (int row = 0; row < tall; ++row) {
            const int ox = lean * row / 34;
            p.put(x + ox - 1, y - row, shade(kPoleWood[1], 120));
            p.put(x + ox, y - row, kPoleWood[1]);
            p.put(x + ox + 1, y - row, kPoleWood[0]);
        }
        if (snapped) {
            p.put(x, y - tall, kPoleWood[1]);
        } else {
            // The cross arm, lit on top, with its insulators.
            const int topX = x + lean * tall / 34;
            p.rect(topX - 6, y - tall + 3, topX + 8, y - tall + 4, kPoleWood[0]);
            p.rect(topX - 6, y - tall + 2, topX + 8, y - tall + 3, shade(kPoleWood[1], 120));
            p.put(topX - 6, y - tall + 1, kSignPale);
            p.put(topX + 7, y - tall + 1, kSignPale);
            p.put(topX - 6, y - tall, shade(kSignPale, 112));
            p.put(topX + 7, y - tall, shade(kSignPale, 112));
        }
    }
    if (snapped) {
        p.line(x + 4, y + 2, x + 22, y - 1, kPropShadow);
        p.line(x + 4, y + 1, x + 22, y - 2, kPoleWood[0]);
        p.line(x + 4, y, x + 22, y - 3, kPoleWood[1]);
    }
}

void powerWire(Paint& p, int x0, int y0, int x1, int y1) {
    // Two straight halves make a sag, which is all a pixel wire can show.
    const int midX = (x0 + x1) / 2;
    const int midY = (y0 + y1) / 2 + 4;
    int px = x0;
    int py = y0;
    const auto draw = [&](int toX, int toY) {
        const int steps = std::max(std::abs(toX - px), std::abs(toY - py));
        for (int i = 1; i <= steps; ++i) p.blend(px + (toX - px) * i / steps, py + (toY - py) * i / steps, kPowerWire);
        px = toX;
        py = toY;
    };
    draw(midX, midY);
    draw(x1, y1);
}

void roadPost(Paint& p, int x, int y) {
    p.blend(x + 1, y + 1, kPropShadow);
    p.blend(x, y + 1, kGroundShadowCore);
    CoverMark cover(p, x, y, CoverKind::Pole);
    p.rect(x, y - 7, x + 1, y + 1, kSignPale);
    p.rect(x + 1, y - 7, x + 2, y + 1, shade(kSignPale, kShadeSidePercent));
    p.rect(x, y - 3, x + 2, y, kRailMetal[0]);
    p.put(x, y - 6, kMarkerAmber);
}

void guardRailAcross(Paint& p, int x0, int x1, int y, TileRandom& random) {
    int gapStart = random.chance(50) ? random.range(x0, x1) : x1;
    const int gapEnd = gapStart + random.range(8, 26);
    for (int x = x0; x < x1; ++x) p.blend(x, y + 1, kPropShadow);
    CoverMark cover(p, (x0 + x1) / 2, y, CoverKind::Fence);
    for (int x = x0; x < x1; ++x) {
        if (x >= gapStart && x < gapEnd) continue;
        const int sag = (x - x0) * (x1 - x) * 3 / std::max(1, (x1 - x0) * (x1 - x0) / 4);
        const int top = y - 6 + (sag < 2 ? 0 : 1);
        p.put(x, top, kRailMetal[2]);
        p.put(x, top + 1, kRailMetal[1]);
        p.put(x, top + 2, (texelNoise(x >> 1, y) & 3u) == 0u ? kRust[1] : kRailMetal[0]);
        if (((x - x0) & 15) == 2) p.rect(x, top, x + 1, y + 1, kRailMetal[0]);
    }
}

void guardRailAlong(Paint& p, int x, int y0, int y1, TileRandom& random) {
    int gapStart = random.chance(50) ? random.range(y0, y1) : y1;
    const int gapEnd = gapStart + random.range(6, 18);
    // Each row of the rail stands on its own row of ground.
    CoverMark cover(p, x, y0, CoverKind::Fence);
    for (int y = y0; y < y1; ++y) {
        if (y >= gapStart && y < gapEnd) continue;
        p.setCoverFoot(y);
        p.put(x, y - 6, kRailMetal[2]);
        p.put(x + 1, y - 6, kRailMetal[0]);
        p.blend(x + 2, y, kPropShadow);
        if ((y & 7) == 0) p.rect(x, y - 6, x + 1, y + 1, kRailMetal[0]);
    }
}

namespace {

// Letters are 3 wide and 5 tall, one row in each group of three bits, top row first.
struct Glyph {
    char letter;
    uint16_t bits;
};
constexpr Glyph kGlyphs[] = {
    {'A', 0b010101111101101}, {'B', 0b110101110101110}, {'D', 0b110101101101110}, {'E', 0b111100110100111},
    {'F', 0b111100110100100}, {'G', 0b011100101101011}, {'H', 0b101101111101101}, {'I', 0b111010010010111},
    {'K', 0b101101110101101}, {'L', 0b100100100100111}, {'M', 0b101111111101101}, {'N', 0b110101101101101},
    {'O', 0b010101101101010}, {'P', 0b110101110100100}, {'R', 0b110101110101101}, {'S', 0b011100010001110},
    {'T', 0b111010010010010}, {'U', 0b101101101101111}, {'X', 0b101101010101101}, {'0', 0b111101101101111},
    {'5', 0b111100110001110}, {'9', 0b111101111001110},
};

}  // namespace

void signText(Paint& p, int x, int y, const char* words, Color color, int wearOutOf16, int size) {
    int cx = x;
    for (const char* c = words; *c != '\0'; ++c) {
        if (*c == ' ') {
            cx += 3 * size;
            continue;
        }
        for (const Glyph& glyph : kGlyphs) {
            if (glyph.letter != *c) continue;
            for (int row = 0; row < 5; ++row) {
                for (int col = 0; col < 3; ++col) {
                    if (((glyph.bits >> (14 - row * 3 - col)) & 1u) == 0u) continue;
                    if (static_cast<int>(texelNoise(cx + col, y + row) & 15u) < wearOutOf16) continue;
                    p.rect(cx + col * size, y + row * size, cx + (col + 1) * size, y + (row + 1) * size, color);
                }
            }
        }
        cx += 4 * size;
    }
}

namespace {

int textWidth(const char* words) {
    int width = 0;
    for (const char* c = words; *c != '\0'; ++c) width += *c == ' ' ? 3 : 4;
    return std::max(0, width - 1);
}

}  // namespace

void guideSign(Paint& p, int x, int y, const char* words, TileRandom& random) {
    contactShadow(p, x, y, 14, 2);
    CoverMark cover(p, x, y, CoverKind::Sign);
    const int lean = random.range(-2, 2);
    const int half = std::max(12, textWidth(words) / 2 + 3);
    post(p, x - half + 3, y, 15, 2, lean, kMetalProp);
    post(p, x + half - 4, y, 15, 2, lean, kMetalProp);
    const int top = y - 26;
    // The board's thickness shows along its top and its shaded right edge.
    p.rect(x - half + lean + 1, top - 1, x + half + 2 + lean, top, kMetalProp[1]);
    p.rect(x + half + 1 + lean, top, x + half + 2 + lean, top + 11, kMetalProp[0]);
    p.rect(x - half + lean, top, x + half + 1 + lean, top + 12, kSignGreen);
    p.rect(x - half + lean, top, x + half + 1 + lean, top + 1, kSignPale);
    p.rect(x - half + lean, top + 11, x + half + 1 + lean, top + 12, kSignPale);
    p.rect(x - half + lean, top, x - half + 1 + lean, top + 12, kSignPale);
    p.rect(x + half + lean, top, x + half + 1 + lean, top + 12, kSignPale);
    signText(p, x - textWidth(words) / 2 + lean, top + 3, words, kSignPale, 2);
    // Bullet holes, because someone always shoots the signs.
    for (int i = random.range(1, 4); i > 0; --i) p.put(x + lean + random.range(-half + 2, half - 2), top + random.range(2, 9), kSignInk);
}

void warningSign(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 3, 1);
    CoverMark cover(p, x, y, CoverKind::Sign);
    const int lean = random.range(-4, 4);
    post(p, x, y, 14, 2, lean, kMetalProp);
    const int cx = x + lean;
    const int cy = y - 21;
    for (int oy = -7; oy <= 7; ++oy) {
        const int half = 7 - std::abs(oy);
        p.put(cx + half + 1, cy + oy - 1, kMetalProp[0]);
        for (int ox = -half; ox <= half; ++ox) p.put(cx + ox, cy + oy, std::abs(ox) == half ? kSignInk : (ox > half - 3 ? shade(kSignYellow, 86) : kSignYellow));
    }
    // An exclamation mark, or a bend ahead.
    if (random.chance(50)) {
        p.rect(cx, cy - 4, cx + 1, cy + 2, kSignInk);
        p.put(cx, cy + 3, kSignInk);
    } else {
        p.line(cx - 1, cy + 4, cx - 1, cy, kSignInk);
        p.line(cx - 1, cy, cx + 2, cy - 3, kSignInk);
    }
    p.put(cx + 2, cy + 1, kRust[1]);
}

void plateSign(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 3, 1);
    CoverMark cover(p, x, y, CoverKind::Sign);
    const int lean = random.range(-3, 3);
    post(p, x, y, 14, 2, lean, kMetalProp);
    const int cx = x + lean;
    p.rect(cx + 6, y - 28, cx + 7, y - 14, kMetalProp[0]);
    p.rect(cx - 5, y - 28, cx + 6, y - 13, kSignPale);
    p.rect(cx - 5, y - 28, cx + 6, y - 27, kSignInk);
    p.rect(cx - 5, y - 14, cx + 6, y - 13, kSignInk);
    p.rect(cx - 5, y - 28, cx - 4, y - 13, kSignInk);
    p.rect(cx + 5, y - 28, cx + 6, y - 13, kSignInk);
    signText(p, cx - 3, y - 23, random.chance(50) ? "55" : "0", kSignInk, 2);
    p.put(cx + random.range(-3, 3), y - random.range(16, 25), kRust[0]);
}

void billboard(Paint& p, int x, int y, const char* words, TileRandom& random) {
    contactShadow(p, x, y + 1, 30, 2);
    CoverMark cover(p, x, y, CoverKind::Sign);
    const int half = 30;
    const int legs = 7;
    for (int lx : {-20, 20}) {
        p.rect(x + lx, y - legs - 2, x + lx + 1, y + 1, kPoleWood[1]);
        p.rect(x + lx + 1, y - legs - 2, x + lx + 2, y + 1, kPoleWood[0]);
        p.line(x + lx - 5, y, x + lx, y - legs, kPoleWood[1]);
    }
    const int top = y - legs - 26;
    // The frame's top edge faces the sky, and its right end is in shade.
    p.rect(x - half + 1, top - 2, x + half + 2, top - 1, shade(kPoleWood[1], 130));
    p.rect(x + half + 1, top - 1, x + half + 2, top + 24, kPoleWood[0]);
    const Color face = kBillboard[random.range(0, 2)];
    p.rect(x - half, top, x + half + 1, top + 24, face);
    p.rect(x - half, top + 16, x + half + 1, top + 24, kBillboard[1]);
    p.rect(x - half, top - 1, x + half + 1, top, kPoleWood[1]);
    p.rect(x - half, top + 24, x + half + 1, top + 25, kPoleWood[0]);
    signText(p, x - textWidth(words) / 2, top + 5, words, kSignPale, 3);
    // Sun and wind have stripped whole strips of the paper away.
    // Each strip tears off at its own length, which shows the bare boards behind.
    for (int i = 0; i < 4; ++i) {
        const int sx = x + random.range(-half, half - 8);
        const int wide = random.range(3, 8);
        for (int col = 0; col < wide; ++col) {
            const int tear = random.range(4, 14);
            p.rect(sx + col, top + 24 - tear, sx + col + 1, top + 24, kPlank[0]);
        }
    }
    p.rect(x + half - 10, top, x + half + 1, top + 7, kPoleWood[0]);
}

void pumpIsland(Paint& p, int x, int y, TileRandom& random) {
    // A low kerb with two old pumps on it.
    p.blendRect(x - 28, y + 1, x + 29, y + 4, kPropShadow);
    p.blendRect(x - 25, y + 1, x + 26, y + 3, kGroundShadowCore);
    CoverMark cover(p, x, y, CoverKind::Box);
    p.rect(x - 28, y - 7, x + 29, y - 2, shade(kCinder[2], 112));
    p.rect(x - 28, y - 2, x + 29, y + 1, kCinder[0]);
    for (int px : {-14, 14}) {
        if (random.chance(25)) {
            // Knocked over, and lying on the kerb.
            p.rect(x + px - 9, y - 9, x + px + 9, y - 3, kPumpBody[0]);
            p.rect(x + px - 9, y - 10, x + px + 9, y - 8, kPumpBody[2]);
            continue;
        }
        p.rect(x + px - 4, y - 18, x + px + 5, y - 3, kPumpBody[1]);
        p.rect(x + px - 4, y - 18, x + px - 2, y - 3, kPumpBody[2]);
        p.rect(x + px + 3, y - 18, x + px + 5, y - 3, kPumpBody[0]);
        p.rect(x + px - 2, y - 16, x + px + 3, y - 11, kPumpFace);
        p.put(x + px, y - 14, kMarkerAmber);
        p.rect(x + px - 4, y - 20, x + px + 5, y - 18, shade(kPumpBody[2], kShadeTopPercent));
        p.line(x + px + 5, y - 10, x + px + 8, y - 4, kCoal);
    }
}

void lampPost(Paint& p, int x, int y, LightList& lights) {
    contactShadow(p, x, y, 4, 2);
    {
        CoverMark cover(p, x, y, CoverKind::Lamp);
        post(p, x - 1, y, 36, 3, 0, kMetalProp);
        standingBox(p, x - 2, x + 3, y, 3, 2, kMetalProp[1]);
        p.line(x, y - 36, x + 7, y - 38, kMetalProp[2]);
        p.line(x, y - 35, x + 7, y - 37, kMetalProp[0]);
        p.rect(x + 6, y - 41, x + 11, y - 40, shade(kLampHead, kShadeTopPercent));
        p.rect(x + 6, y - 40, x + 11, y - 38, kLampHead);
        p.put(x + 10, y - 39, shade(kLampHead, kShadeSidePercent));
    }
    lights.add(x + 8, y, 38.0f, LightKind::StreetLamp);
}

void poleSign(Paint& p, int x, int y, const char* words, TileRandom& random) {
    // The tallest thing on the map, and still thin: you spot the stop from far off.
    contactShadow(p, x, y, 6, 2);
    CoverMark cover(p, x, y, CoverKind::Sign);
    const int lean = random.range(-2, 2);
    const int half = textWidth(words) + 5;
    for (int offset : {-half + 4, half - 4}) post(p, x + offset, y, 26, 2, lean, kMetalProp);
    const int top = y - 44;
    const int cx = x + lean;
    // The box's lit top and shaded right side.
    p.rect(cx - half + 1, top - 1, cx + half + 2, top, shade(kCanopyStripe[1], 112));
    p.rect(cx + half + 1, top, cx + half + 2, top + 19, shade(kCanopyStripe[0], kShadeSidePercent));
    p.rect(cx - half, top, cx + half + 1, top + 19, kCanopyStripe[0]);
    p.rect(cx - half, top, cx + half + 1, top + 2, kCanopyStripe[1]);
    p.rect(cx - half, top + 17, cx + half + 1, top + 19, kCanopyStripe[1]);
    p.rect(cx - half, top + 19, cx + half + 1, top + 20, kSignInk);
    signText(p, cx - textWidth(words) + 1, top + 4, words, kCanopyStripe[1], 0, 2);
    // Rust running down from the top corner, and a few bullet holes.
    p.rect(cx + half - 4, top, cx + half + 1, top + 6, kRust[0]);
    for (int i = 0; i < 4; ++i) p.put(cx + random.range(-half + 1, half - 1), top + random.range(2, 16), kSignInk);
}

// --- Fences -------------------------------------------------------------------

void wireFenceAcross(Paint& p, int x0, int x1, int y) {
    constexpr int kPostGap = 28;
    const int firstPost = x0 - ((x0 % kPostGap) + kPostGap) % kPostGap;
    for (int px = firstPost; px < x1; px += kPostGap) {
        const uint32_t h = texelNoise(px, y);
        const int lean = static_cast<int>(h & 3u) - 1;
        const bool down = (h & 28u) == 0u;
        const int next = px + kPostGap;
        if (!down) {
            p.put(px, y + 1, kPropShadow);
            CoverMark cover(p, px, y, CoverKind::Fence);
            for (int row = 0; row < 12; ++row) {
                p.put(px + lean * row / 11, y - row, shade(kWirePost, 118));
                p.put(px + 1 + lean * row / 11, y - row, shade(kWirePost, kShadeSidePercent));
            }
        } else {
            p.line(px, y, px + 9, y + 2, kWirePost);
        }
        if (((h >> 5) & 3u) == 0u) continue;  // The wire has gone from this span.
        const bool slack = ((h >> 7) & 1u) != 0u;
        for (int strand = 0; strand < 2; ++strand) {
            const int rowBase = strand == 0 ? 9 : 5;
            for (int wx = px; wx < next; ++wx) {
                const int along = wx - px;
                const int sag = along * (kPostGap - along) * (slack ? 4 : 2) / (kPostGap * kPostGap / 4);
                const int wy = y - rowBase + sag;
                p.blend(wx, wy, kWireStrand);
                if ((wx + strand * 3) % 5 == 0) p.put(wx, wy, kWireBarb);
            }
        }
    }
}

void wireFenceAlong(Paint& p, int x, int y0, int y1) {
    constexpr int kPostGap = 16;
    const int firstPost = y0 - ((y0 % kPostGap) + kPostGap) % kPostGap;
    for (int py = firstPost; py < y1; py += kPostGap) {
        const uint32_t h = texelNoise(x, py);
        if ((h & 28u) != 0u) {
            CoverMark cover(p, x, py, CoverKind::Fence);
            for (int row = 0; row < 12; ++row) {
                p.put(x, py - row, shade(kWirePost, 118));
                p.put(x + 1, py - row, shade(kWirePost, kShadeSidePercent));
            }
        } else {
            p.line(x, py, x + 6, py + 2, kWirePost);
        }
        if (((h >> 5) & 3u) == 0u) continue;
        const int wander = static_cast<int>((h >> 7) & 1u);
        for (int wy = py; wy < py + kPostGap; ++wy) {
            p.blend(x + wander, wy - 9, kWireStrand);
            p.blend(x + wander, wy - 5, kWireStrand);
            if ((wy & 3) == 0) p.put(x + wander, wy - 9, kWireBarb);
        }
    }
}

// --- Trees and wood -----------------------------------------------------------

namespace {

void limb(Paint& p, int x, int y, int dx, int dy, int depth, TileRandom& random) {
    const int endX = x + dx;
    const int endY = y + dy;
    // Thick limbs are round: lit on the left, dark on the right.
    p.line(x, y, endX, endY, depth >= 2 ? kDeadWood[1] : kDeadWood[2]);
    if (depth >= 1) p.line(x, y + 1, endX, endY + 1, kDeadWood[0]);
    if (depth >= 2) p.line(x + 1, y, endX + 1, endY, kDeadWood[0]);
    if (depth >= 3) {
        p.line(x - 1, y, endX - 1, endY, kDeadWood[2]);
        p.line(x - 2, y, endX - 2, endY, shade(kDeadWood[2], 112));
    }
    if (depth == 0) return;
    const int forks = depth >= 2 ? 2 : random.range(1, 2);
    for (int i = 0; i < forks; ++i) {
        const int side = i == 0 ? 1 : -1;
        limb(p, endX, endY, dx * 2 / 3 + side * random.range(1, 5), dy * 2 / 3 - random.range(0, 2), depth - 1, random);
    }
}

}  // namespace

void deadTree(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x + 2, y, 7, 2);
    p.blendRect(x + 4, y, x + random.range(12, 22), y + 1, kPropShadow);
    // Roots showing where the sand blew away lie flat, so they cover nothing.
    p.line(x - 5, y + 1, x, y - 1, kDeadWood[0]);
    p.line(x + 5, y + 1, x + 1, y - 1, kDeadWood[0]);
    CoverMark cover(p, x, y, CoverKind::Tree);
    limb(p, x, y, random.range(-2, 2), -random.range(11, 14), 3, random);
}

void fallenLog(Paint& p, int x, int y, TileRandom& random) {
    const int length = random.range(14, 26);
    const int fall = random.range(-3, 3);
    p.line(x, y + 2, x + length, y + fall + 2, kPropShadow);
    p.line(x, y + 1, x + length, y + fall + 1, kDeadWood[0]);
    p.line(x, y, x + length, y + fall, kDeadWood[1]);
    p.line(x + 1, y - 1, x + length - 1, y + fall - 1, kDeadWood[2]);
    p.line(x + length / 2, y - 1, x + length / 2 + 3, y - 5, kDeadWood[1]);
    p.oval(x, y, 1, 1, kDeadWood[2]);
}

void stump(Paint& p, int x, int y) {
    contactShadow(p, x + 1, y, 5, 1);
    CoverMark cover(p, x, y, CoverKind::Box);
    p.rect(x - 3, y - 5, x + 4, y + 1, kDeadWood[1]);
    p.rect(x - 3, y - 5, x - 1, y + 1, kDeadWood[2]);
    p.rect(x + 2, y - 5, x + 4, y + 1, kDeadWood[0]);
    p.oval(x, y - 6, 3, 1, kDeadWood[2]);
    p.put(x, y - 6, kDeadWood[0]);
}

// --- Bones --------------------------------------------------------------------

void cowSkull(Paint& p, int x, int y) {
    contactShadow(p, x, y + 1, 6, 2);
    // A wide brow, narrowing to a long muzzle.
    p.rect(x - 4, y - 9, x + 5, y - 5, kBone[1]);
    p.rect(x - 3, y - 5, x + 4, y - 3, kBone[1]);
    p.rect(x - 2, y - 3, x + 3, y + 1, kBone[1]);
    p.rect(x - 3, y - 9, x + 4, y - 8, kBone[2]);
    p.rect(x - 1, y - 8, x + 2, y - 3, kBone[2]);
    p.rect(x - 4, y - 7, x - 1, y - 5, kBoneHole);
    p.rect(x + 2, y - 7, x + 5, y - 5, kBoneHole);
    p.put(x - 1, y - 1, kBoneHole);
    p.put(x + 1, y - 1, kBoneHole);
    // Horns, curving up from the sides of the head.
    p.line(x - 5, y - 8, x - 9, y - 9, kBone[0]);
    p.line(x - 9, y - 9, x - 10, y - 13, kBone[1]);
    p.line(x + 5, y - 8, x + 9, y - 9, kBone[0]);
    p.line(x + 9, y - 9, x + 10, y - 13, kBone[1]);
}

void ribCage(Paint& p, int x, int y, int ribs, int size) {
    contactShadow(p, x + ribs * 2, y + 1, ribs * 3 + 3, 2);
    const int gap = size >= 12 ? 6 : 4;
    p.line(x - 2, y - size - 1, x + ribs * gap + 2, y - size, kBone[2]);
    p.line(x - 2, y - size, x + ribs * gap + 2, y - size + 1, kBone[0]);
    for (int i = 0; i < ribs; ++i) {
        const int rx = x + i * gap;
        const int tall = size - (i * 3 / ribs);
        // Each rib bows out and back in to the ground. The far one is drawn dimmer.
        p.line(rx - 2, y - size, rx - 3, y - tall / 2 - 2, kBone[0]);
        p.line(rx - 3, y - tall / 2 - 2, rx - 1, y - 3, kBone[0]);
        p.line(rx + 1, y - size, rx + 2, y - tall / 2, kBone[1]);
        p.line(rx + 2, y - tall / 2, rx + 1, y, kBone[2]);
    }
}

void bigSkeleton(Paint& p, int x, int y, TileRandom& random) {
    // Something huge died here long ago: a long spine, a basket of ribs and a skull.
    const int ribs = random.range(8, 11);
    const int size = random.range(14, 18);
    ribCage(p, x - ribs * 3, y, ribs, size);
    const int tailStart = x + ribs * 3;
    for (int i = 0; i < 9; ++i) {
        const int vx = tailStart + i * 4;
        const int vy = y - size + 1 + i * i / 6;
        p.rect(vx, vy, vx + 3, vy + 2, kBone[i % 2 == 0 ? 1 : 2]);
        p.put(vx + 1, vy + 2, kBone[0]);
    }
    cowSkull(p, x - ribs * 3 - 12, y + 4);
    looseBones(p, x, y + 6, 40, 8, random);
}

void humanSkeleton(Paint& p, int x, int y, TileRandom& random) {
    const int flip = random.chance(50) ? 1 : -1;
    p.blendRect(x - 8, y + 1, x + 9, y + 2, kPropShadow);
    // Skull.
    p.rect(x - flip * 9 - 2, y - 3, x - flip * 9 + 2, y + 1, kBone[2]);
    p.put(x - flip * 9, y - 2, kBoneHole);
    // Spine and ribs.
    p.line(x - flip * 7, y - 1, x + flip * 2, y - 1, kBone[1]);
    for (int i = 0; i < 3; ++i) {
        const int rx = x - flip * (5 - i * 2);
        p.rect(rx, y - 3, rx + 1, y + 2, kBone[i == 0 ? 2 : 1]);
    }
    // Arms flung out, and legs.
    p.line(x - flip * 6, y - 2, x - flip * 3, y - 6, kBone[0]);
    p.line(x - flip * 6, y, x - flip * 2, y + 3, kBone[1]);
    p.line(x + flip * 2, y - 1, x + flip * 9, y - 3, kBone[1]);
    p.line(x + flip * 2, y, x + flip * 8, y + 2, kBone[0]);
}

void looseBones(Paint& p, int x, int y, int spread, int count, TileRandom& random) {
    for (int i = 0; i < count; ++i) {
        const int bx = x + random.range(-spread, spread);
        const int by = y + random.range(-spread / 2, spread / 2);
        const int length = random.range(2, 7);
        const int fall = random.range(-2, 2);
        p.line(bx, by + 1, bx + length, by + fall + 1, kBone[0]);
        p.line(bx, by, bx + length, by + fall, kBone[random.range(1, 2)]);
        p.put(bx, by - 1, kBone[1]);
        p.put(bx + length, by + fall + 1, kBone[1]);
    }
}

// --- Low walls ----------------------------------------------------------------

namespace {

Color wallTexel(const WallLook& look, int x, int row, int y) {
    const uint32_t noise = texelNoise(x, y - row);
    if (look.ribEvery > 0) {
        // Sheet metal: ribs of light and shade, with rust running down from the top.
        const int rib = (x & (look.ribEvery - 1));
        const int tone = rib == 0 ? 0 : (rib == 1 ? 2 : 1);
        return (noise & 31u) == 0u ? look.tones[0] : look.tones[tone];
    }
    if (look.courseRows > 0 && (row % look.courseRows) == 0) return look.tones[0];
    if (look.courseRows > 0 && ((x + (row / look.courseRows) * 4) & 7) == 0) return look.tones[0];
    return look.tones[noise % 3u];
}

}  // namespace

void faceWall(Paint& p, TileRandom& random, const WallLook& look, int x0, int x1, int footY, int maxHeight) {
    // The whole face stands on one row, so it is one cover. Its shadow is only mixed in, and
    // mixing never marks a pixel, so the shadow on the ground covers nothing.
    CoverMark cover(p, (x0 + x1) / 2, footY, CoverKind::Wall);
    int x = x0;
    while (x < x1) {
        const int run = random.range(3, 14);
        const int roll = random.range(0, 9);
        const int height = roll < 2 ? 0 : (roll < 5 ? random.range(2, std::max(2, maxHeight / 2)) : random.range(std::max(1, maxHeight / 2), maxHeight));
        for (int i = 0; i < run && x < x1; ++i, ++x) {
            const int depth = std::min(3, height / 3);
            for (int row = 1; row <= depth; ++row) p.blend(x, footY + row, kWallShade);
            if (height == 0) continue;
            // The end of a run turns away from the light where the next run is lower.
            const bool end = i == run - 1 || x == x1 - 1;
            for (int row = 0; row < height; ++row) {
                Color color = wallTexel(look, x, row, footY);
                if (row < 2) color = shade(color, 75);
                if (end && row > 1) color = shade(color, kShadeSidePercent + 10);
                p.put(x, footY - row, color);
            }
            // The top faces the sky and has thickness.
            p.put(x, footY - height, shade(look.top, 110));
            p.put(x, footY - height - 1, look.top);
            p.put(x, footY - height - 2, shade(look.top, 92));
        }
    }
}

void sideWall(Paint& p, TileRandom& random, const WallLook& look, int x, int y0, int y1, int maxHeight) {
    constexpr int kThick = 4;
    // Each row of its top stands on its own row of ground, so its foot moves along with it.
    CoverMark cover(p, x + kThick / 2, y0, CoverKind::WallAlong);
    int y = y0;
    while (y < y1) {
        const int run = random.range(6, 18);
        const int roll = random.range(0, 9);
        const int height = roll < 2 ? 0 : (roll < 6 ? random.range(2, std::max(2, maxHeight / 2)) : random.range(std::max(1, maxHeight / 2), maxHeight));
        const int end = std::min(y1, y + run);
        for (int row = y; row < end && height > 0; ++row) {
            for (int col = 0; col < std::min(3, height / 2); ++col) p.blend(x + kThick + col + 1, row, kWallShade);
        }
        for (int row = y; row < end && height > 0; ++row) {
            p.setCoverFoot(row);
            for (int col = 0; col < kThick; ++col) {
                Color color = (texelNoise(x + col, row) & 7u) == 0u ? look.tones[2] : look.top;
                if (col == 0) color = shade(color, 112);
                if (col == kThick - 1) color = shade(color, kShadeSidePercent + 12);
                p.put(x + col, row - height, color);
            }
            p.put(x + kThick, row - height + 1, shade(look.tones[0], kShadeSidePercent));
        }
        // Where the wall steps down toward us, its broken end shows its face.
        if (height > 0) {
            p.setCoverFoot(end - 1);
            for (int row = 0; row < height; ++row) {
                for (int col = 0; col < kThick; ++col) p.put(x + col, end - 1 - row, shade(wallTexel(look, x + col, row, end), row < 2 ? 75 : 100));
            }
        }
        y = end;
    }
}

void roofSheet(Paint& p, TileRandom& random, int x, int y, int w, int h) {
    // A fallen sheet of roof, tilted, with its ribs running down the slope.
    const int tilt = random.range(-3, 3);
    const Color* tones = random.chance(60) ? kCorrugated : kCorrugatedGrey;
    p.blendRect(x + 2, y + h, x + w + 2, y + h + 2, kPropShadow);
    for (int col = 0; col < w; ++col) {
        const int top = y + tilt * col / std::max(1, w);
        for (int row = 0; row < h; ++row) {
            const int rib = (col & 3) == 0 ? 0 : ((col & 3) == 1 ? 2 : 1);
            const uint32_t noise = texelNoise((x + col) >> 2, (y + row) >> 1);
            p.put(x + col, top + row, (noise & 7u) == 0u ? kRust[0] : tones[rib]);
        }
    }
}

}  // namespace ls::maps::wasteland
