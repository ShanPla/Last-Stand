#include "components/Maps/SharedProps/SharedProps.h"

#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps {

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

void contactShadow(Paint& p, int x, int y, int rx, int ry) {
    // Leans a little right, the way the sun's shadows fall, and is darkest right under the foot.
    p.blendOval(x + 1, y, rx + 1, ry + 1, kPropShadow);
    p.blendOval(x, y, std::max(1, rx * 2 / 3), std::max(1, ry - 1), kGroundShadowCore);
}

void post(Paint& p, int x, int y, int height, int width, int lean, const Color tones[3]) {
    for (int row = 0; row < height; ++row) {
        const int left = x + lean * row / std::max(1, height);
        for (int col = 0; col < width; ++col) {
            // Lit edge, body, then the side in shade.
            const Color color = col == 0 ? tones[2] : (col == width - 1 ? tones[0] : tones[1]);
            p.put(left + col, y - row, color);
        }
    }
    // A cap catches the light from above.
    p.put(x + lean, y - height, tones[2]);
    if (width > 2) p.put(x + lean + 1, y - height, tones[2]);
}

void standingBox(Paint& p, int x0, int x1, int footY, int height, int depth, Color front) {
    const int bottom = footY + 1;  // Rows run up to here, not including it.
    const Color top = shade(front, kShadeTopPercent);
    const Color side = shade(front, kShadeSidePercent);
    // Going one row back up the screen moves half a column right: that slant is what shows the
    // right side and the top together, as the tents and walls do.
    for (int t = 1; t <= depth; ++t) {
        const int column = x1 - 1 + (t + 1) / 2;
        for (int row = bottom - height - t; row < bottom - t; ++row) p.put(column, row, side);
    }
    for (int t = 1; t <= depth; ++t) {
        for (int x = x0 + t / 2; x < x1 + t / 2; ++x) p.put(x, bottom - height - t, top);
    }
    for (int row = bottom - height; row < bottom; ++row) {
        // Darker near the ground, where less light reaches.
        const Color color = row >= bottom - 2 ? shade(front, 84) : front;
        for (int x = x0; x < x1; ++x) p.put(x, row, color);
    }
    // The near edge of the top is the brightest line, which is what makes it read as a top.
    for (int x = x0; x < x1; ++x) p.put(x, bottom - height - 1, shade(top, 112));
}

void barrel(Paint& p, int x, int y, TileRandom& random) {
    contactShadow(p, x, y, 6, 2);
    CoverMark cover(p, x, y, CoverKind::Box);
    const int height = 12;
    for (int row = 0; row < height; ++row) {
        for (int col = -4; col <= 4; ++col) {
            // A drum is round, so its light falls off across it: bright left, dark right edge.
            const int shade = col < -2 ? 2 : (col > 2 ? 0 : 1);
            Color color = kRust[shade];
            if (col == 4) color = kCoal;
            if (row == 2 || row == 8) color = col > 2 ? kCoal : kBarrelBand;
            if ((texelNoise(x + col, y - row) & 15u) == 0u) color = kRust[0];
            p.put(x + col, y - row, color);
        }
    }
    // The lid, seen from above: a lit rim around the dark inside.
    p.oval(x, y - height, 4, 2, shade(kRust[2], kShadeTopPercent));
    p.oval(x, y - height, 3, 1, kCoal);
    p.put(x - 3, y - height - 1, shade(kRust[2], kShadeTopPercent + 12));
    if (random.chance(30)) p.put(x + 1, y - 4, kCoal);
}

void burningBarrel(Paint& p, int x, int y, TileRandom& random, LightList& lights) {
    barrel(p, x, y, random);
    lights.add(x, y, 14.0f, LightKind::Fire);
}

void tent(Paint& p, int x, int y, TileRandom& random) {
    const Color* fabric = kTentFabric[random.range(0, 2)];
    const int half = random.range(13, 18);
    const int height = random.range(13, 17);
    const int depth = 7;
    p.blendOval(x, y, half + 4, 3, kPropShadow);
    p.blendOval(x + depth, y - 1, half, 2, kGroundShadowCore);
    CoverMark cover(p, x, y, CoverKind::Tent);
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
    CoverMark cover(p, x, y, CoverKind::Box);
    standingBox(p, x - half, x + half + 1, y, tall + 1, top, kPlank[1]);
    for (int col = -half; col <= half; col += 3) {
        p.rect(x + col, y - tall, x + col + 1, y + 1, kPlankGap);
    }
    p.rect(x - half, y - tall, x + half + 1, y - tall + 1, kPlankGap);
    p.rect(x - half, y, x + half + 1, y + 1, kPlank[0]);
    if (random.chance(40)) p.rect(x - 2 + top / 2, y - tall - top + 1, x + 3 + top / 2, y - tall, kPlankGap);
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
    CoverMark cover(p, x + count * 4, y, CoverKind::Box);
    for (int layer = 0; layer < 2; ++layer) {
        for (int i = 0; i < count - layer; ++i) {
            const int bx = x + i * 8 + layer * 4 + random.range(-1, 1);
            const int by = y - layer * 4;
            p.oval(bx + 4, by - 2, 4, 2, kSandbag[1]);
            p.rect(bx + 2, by - 4, bx + 6, by - 3, shade(kSandbag[2], 112));
            p.rect(bx + 2, by - 1, bx + 7, by, kSandbag[0]);
        }
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

}  // namespace ls::maps
