#include "components/Maps/Wasteland/Terrain/Terrain.h"

#include "components/Maps/Wasteland/Props/Props.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::wasteland {

using namespace theme;

namespace {

enum Salt : uint32_t { kSaltScatter = 307, kSaltTracks = 311, kSaltFences = 313 };

// Large fields are only worked out every 4 texels. Speckle hides the steps, and a chunk
// paints about 16 times fewer noise lookups.
constexpr int kFieldStepShift = 2;
constexpr int kFieldColumns = kChunkWidth >> kFieldStepShift;
constexpr int kFieldRows = kChunkHeight >> kFieldStepShift;

Color rampTone(const Color* ramp, int count, int blotch, uint32_t noise) {
    int tone = (blotch * count) >> 8;
    if ((noise & 15u) == 0u) tone--;
    if ((noise & 15u) == 1u) tone++;
    return ramp[std::clamp(tone, 0, count - 1)];
}

// Long dunes: each crest has a steep shaded face downwind and a long lit back, which
// gets wind ripples. A slow wander in the position keeps the crests from looking ruled.
constexpr int kDunePeriod = 390;

// Like softNoise, but it eases in and out of each grid cell. softNoise blends in straight
// lines, which would put a sharp kink in a dune's crest at every cell edge.
int curvedNoise(int x, int y, int shiftX, int shiftY) {
    const int u = (x & ((1 << shiftX) - 1)) * 256 >> shiftX;
    const int v = (y & ((1 << shiftY) - 1)) * 256 >> shiftY;
    const int easeU = (u * u * (768 - 2 * u)) >> 16;
    const int easeV = (v * v * (768 - 2 * v)) >> 16;
    const int cellX = x >> shiftX;
    const int cellY = y >> shiftY;
    const int a = static_cast<int>(texelNoise(cellX, cellY) & 255u);
    const int b = static_cast<int>(texelNoise(cellX + 1, cellY) & 255u);
    const int c = static_cast<int>(texelNoise(cellX, cellY + 1) & 255u);
    const int e = static_cast<int>(texelNoise(cellX + 1, cellY + 1) & 255u);
    const int top = a * (256 - easeU) + b * easeU;
    const int bottom = c * (256 - easeU) + e * easeU;
    return (top * (256 - easeV) + bottom * easeV) >> 16;
}

Color duneTexel(const Desert& d, int x, int y, int strength, int blotch, uint32_t noise) {
    const int wander = curvedNoise(x + d.ay, y + d.bx, 8, 7) * 2 + curvedNoise(x + d.bx, y + d.ay, 6, 5) / 2;
    const int phase = 3 * x + 2 * y + wander;
    const int t = floorMod(phase, kDunePeriod);
    const int grain = static_cast<int>((noise >> 4) & 15u);
    const int depth = std::min(strength * 3, 50);  // Crests fade out at the edge of the dunes.
    if (t + grain < depth / 3) return kSandLee;
    if (t + grain < depth) return kSandShade;
    if (t > kDunePeriod - 8 && depth > 30) return kSandCrest;
    const int ripple = x + 2 * y + (softNoise(x + d.bx, y + d.ax, 5, 4) >> 1);
    if ((ripple & 15) < 2 && (noise & 3u) != 0u) return kSand[0];
    // The back of the dune gets slowly lighter toward the crest.
    return rampTone(kSand, 4, std::clamp(blotch / 2 + t * 128 / kDunePeriod, 0, 255), noise);
}

void paintBase(Paint& p, const Desert& d) {
    unsigned char a[kFieldRows][kFieldColumns];
    unsigned char b[kFieldRows][kFieldColumns];
    unsigned char blotch[kFieldRows][kFieldColumns];
    for (int row = 0; row < kFieldRows; ++row) {
        const int y = p.top() + (row << kFieldStepShift);
        for (int col = 0; col < kFieldColumns; ++col) {
            const int x = p.left() + (col << kFieldStepShift);
            a[row][col] = static_cast<unsigned char>(fieldA(d, x, y));
            b[row][col] = static_cast<unsigned char>(fieldB(d, x, y));
            blotch[row][col] = static_cast<unsigned char>(softNoise(x + d.windX, y + d.windY, 5, 4));
        }
    }
    for (int ly = 0; ly < kChunkHeight; ++ly) {
        const int y = p.top() + ly;
        const int row = ly >> kFieldStepShift;
        for (int lx = 0; lx < kChunkWidth; ++lx) {
            const int x = p.left() + lx;
            const int col = lx >> kFieldStepShift;
            const uint32_t noise = texelNoise(x, y);
            const int jitter = static_cast<int>((noise >> 5) & 31u) - 15;
            const int fieldValueA = a[row][col] + jitter;
            const int patch = std::clamp(blotch[row][col] + jitter, 0, 255);
            Color color;
            switch (classifyZone(fieldValueA, b[row][col] + jitter)) {
                case Zone::Dunes: color = duneTexel(d, x, y, fieldValueA - kDunesAbove, patch, noise); break;
                case Zone::Pan: color = rampTone(kPan, 3, patch, noise); break;
                case Zone::Reg:
                    color = rampTone(kReg, 3, patch, noise);
                    if ((noise & 31u) == 2u) color = kStone[1];
                    if ((noise & 63u) == 3u) color = kStone[2];
                    if ((noise & 31u) == 4u) color = kStone[0];
                    break;
                case Zone::Scrub: color = rampTone(kScrubEarth, 3, patch, noise); break;
                case Zone::RedEarth: color = rampTone(kRedEarth, 3, patch, noise); break;
                default:
                    color = rampTone(kSand, 4, patch, noise);
                    // Wind leaves faint lines on flat sand too.
                    if (((x + 2 * y + (patch >> 1)) & 31) == 0 && (noise & 3u) != 0u) color = kSand[0];
                    break;
            }
            p.put(x, y, color);
        }
    }
}

// --- Cracked ground -----------------------------------------------------------

// Dried mud breaks into plates. Each corner of a grid is pushed a random way, and joining
// neighbors gives uneven plates that fit together, for very little work.
constexpr int kPlateWidth = 26;
constexpr int kPlateHeight = 15;

struct Corner {
    int x, y;
    uint32_t bits;
};

Corner plateCorner(const Desert& d, int i, int j) {
    const uint32_t h = mix(texelNoise(i + d.ax, j + d.ay) + static_cast<uint32_t>(d.seed));
    const int jx = static_cast<int>(h & 31u) - 15;
    const int jy = static_cast<int>((h >> 5) & 15u) - 7;
    return {i * kPlateWidth + jx * 3 / 4, j * kPlateHeight + jy * 3 / 4, h >> 9};
}

void crackLine(Paint& p, int x0, int y0, int x1, int y1, uint32_t bits, Color dark, Color lit) {
    // Bend at a midpoint, so plates don't have ruler-straight sides.
    const int mx = (x0 + x1) / 2 + static_cast<int>(bits & 3u) - 1;
    const int my = (y0 + y1) / 2 + static_cast<int>((bits >> 2) & 3u) - 1;
    p.line(x0 + 1, y0 + 1, mx + 1, my + 1, lit);
    p.line(mx + 1, my + 1, x1 + 1, y1 + 1, lit);
    p.line(x0, y0, mx, my, dark);
    p.line(mx, my, x1, y1, dark);
}

void paintCracks(Paint& p, const Desert& d) {
    const int i0 = floorDiv(p.left() - 40, kPlateWidth);
    const int i1 = floorDiv(p.right() + 40, kPlateWidth);
    const int j0 = floorDiv(p.top() - 24, kPlateHeight);
    const int j1 = floorDiv(p.bottom() + 24, kPlateHeight);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const Corner c = plateCorner(d, i, j);
            const Zone zone = zoneAt(d, c.x, c.y);
            const bool pan = zone == Zone::Pan;
            // Patches of finer, fainter cracking show up in the plainer land too.
            const bool crazed = !pan && zone != Zone::Dunes && softNoise(c.x + d.by, c.y + d.bx, 8, 7) > 158;
            if (!pan && !crazed) continue;
            const Color dark = pan ? kPanCrack : kPanCrackFaint;
            const Color lit = pan ? kPanCrackLit : kSand[3];
            const Corner right = plateCorner(d, i + 1, j);
            const Corner down = plateCorner(d, i, j + 1);
            const int skip = pan ? 2 : 5;
            if (static_cast<int>(c.bits & 7u) >= skip) crackLine(p, c.x, c.y, right.x, right.y, c.bits >> 3, dark, lit);
            if (static_cast<int>((c.bits >> 8) & 7u) >= skip) crackLine(p, c.x, c.y, down.x, down.y, c.bits >> 11, dark, lit);
        }
    }
}

// --- Scatter ------------------------------------------------------------------

// Small things, placed by a hash of a grid cell, so that every chunk that can see one
// draws the same one in the same place.
constexpr int kScatterWidth = 36;
constexpr int kScatterHeight = 22;

void scatterItem(Paint& p, TileRandom& random, Zone zone, int x, int y) {
    const int roll = random.range(0, 99);
    switch (zone) {
        case Zone::Dunes:
            if (roll < 7) tuft(p, x, y, random);
            else if (roll < 11) pebbles(p, x, y, 8, 3, random);
            else if (roll < 12) stone(p, x, y, 2, random);
            break;
        case Zone::Sand:
            if (roll < 16) pebbles(p, x, y, 10, 5, random);
            else if (roll < 26) stone(p, x, y, random.range(1, 3), random);
            else if (roll < 42) tuft(p, x, y, random);
            else if (roll < 44) bush(p, x, y, random);
            else if (roll < 46) litter(p, x, y, random);
            break;
        case Zone::Pan:
            if (roll < 9) pebbles(p, x, y, 8, 3, random);
            else if (roll < 13) stone(p, x, y, random.range(1, 2), random);
            else if (roll < 15) litter(p, x, y, random);
            else if (roll < 17) looseBones(p, x, y, 6, 2, random);
            break;
        case Zone::Reg:
            if (roll < 34) pebbles(p, x, y, 12, 7, random);
            else if (roll < 62) stone(p, x, y, random.range(1, 4), random);
            else if (roll < 66) boulder(p, x, y, random.range(4, 8), random);
            else if (roll < 70) tuft(p, x, y, random);
            break;
        case Zone::Scrub:
            if (roll < 38) tuft(p, x, y, random);
            else if (roll < 54) bush(p, x, y, random);
            else if (roll < 66) pebbles(p, x, y, 10, 4, random);
            else if (roll < 68) tumbleweed(p, x, y, random);
            else if (roll < 74) stone(p, x, y, random.range(1, 2), random);
            break;
        case Zone::RedEarth:
            if (roll < 20) stone(p, x, y, random.range(1, 4), random);
            else if (roll < 36) pebbles(p, x, y, 10, 5, random);
            else if (roll < 42) tuft(p, x, y, random);
            else if (roll < 45) boulder(p, x, y, random.range(4, 7), random);
            break;
    }
}

void paintScatter(Paint& p, const Desert& d) {
    constexpr int kReach = 24;  // The widest small item, so one over a seam is never cut off.
    const int i0 = floorDiv(p.left() - kReach, kScatterWidth);
    const int i1 = floorDiv(p.right() + kReach, kScatterWidth);
    const int j0 = floorDiv(p.top() - kReach, kScatterHeight);
    const int j1 = floorDiv(p.bottom() + kReach, kScatterHeight);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            TileRandom random(d.seed, i, j, kSaltScatter);
            const int x = i * kScatterWidth + random.range(0, kScatterWidth - 1);
            const int y = j * kScatterHeight + random.range(0, kScatterHeight - 1);
            scatterItem(p, random, zoneAt(d, x, y), x, y);
        }
    }
}

// --- Tire tracks --------------------------------------------------------------

// How far a track can end up from where it starts, which is how far a chunk must look.
constexpr int kTrackReachX = 340;
constexpr int kTrackReachY = 280;

// Someone drove out here, a long time ago. Two soft ruts, slowly curving.
void tireTrack(Paint& p, TileRandom& random, int startX, int startY) {
    const int steps = random.range(90, 300);
    const bool mostlyAcross = random.chance(60);
    // Headings in 1/256 of a texel per step.
    int dirX = mostlyAcross ? (random.chance(50) ? 256 : -256) : random.range(-90, 90);
    int dirY = mostlyAcross ? random.range(-60, 60) : (random.chance(50) ? 180 : -180);
    const int turnX = mostlyAcross ? 0 : random.range(-1, 1);
    const int turnY = mostlyAcross ? random.range(-1, 1) : 0;
    const int gapX = mostlyAcross ? 0 : 8;
    const int gapY = mostlyAcross ? 5 : 0;
    int fx = startX * 256;
    int fy = startY * 256;
    if (!p.overlaps(startX - kTrackReachX, startY - kTrackReachY, startX + kTrackReachX, startY + kTrackReachY)) return;
    for (int i = 0; i < steps; ++i) {
        fx += dirX;
        fy += dirY;
        dirX += turnX;
        dirY += turnY;
        const int x = fx >> 8;
        const int y = fy >> 8;
        if ((i & 3) == 3) continue;  // Tread breaks the rut into dashes.
        // The track fades toward its ends, where the wind has filled it in.
        const int fade = std::min(i, steps - i);
        if (fade < 30 && (texelNoise(x, y) & 31u) > static_cast<uint32_t>(fade)) continue;
        p.blend(x, y, kTrackDark);
        p.blend(x + gapX, y + gapY, kTrackDark);
        p.blend(x + (gapX ? 1 : 0), y + (gapY ? 1 : 0), kTrackLit);
        p.blend(x + gapX + (gapX ? 1 : 0), y + gapY + (gapY ? 1 : 0), kTrackLit);
    }
}

void paintTracks(Paint& p, const Desert& d) {
    // A track is up to one chunk wide and two tall, so this is how far a chunk must look.
    const int i0 = floorDiv(p.left() - kTrackReachX, kChunkWidth);
    const int i1 = floorDiv(p.right() + kTrackReachX, kChunkWidth);
    const int j0 = floorDiv(p.top() - kTrackReachY, kChunkHeight);
    const int j1 = floorDiv(p.bottom() + kTrackReachY, kChunkHeight);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            TileRandom random(d.seed, i, j, kSaltTracks);
            for (int n = 0; n < 2; ++n) {
                const bool present = random.chance(45);
                const int x = i * kChunkWidth + random.range(0, kChunkWidth - 1);
                const int y = j * kChunkHeight + random.range(0, kChunkHeight - 1);
                TileRandom own(d.seed, x, y, kSaltTracks + 1);
                if (present) tireTrack(p, own, x, y);
            }
        }
    }
}

// --- Fences -------------------------------------------------------------------

// Old ranch fences run for a long way across the open land, with no reason left.
constexpr int kFenceCellWidth = kChunkWidth * 3;
constexpr int kFenceCellHeight = kChunkHeight * 3;

void paintFences(Paint& p, const Desert& d) {
    const int i0 = floorDiv(p.left() - kFenceCellWidth, kFenceCellWidth);
    const int i1 = floorDiv(p.right(), kFenceCellWidth);
    const int j0 = floorDiv(p.top() - kFenceCellHeight, kFenceCellHeight);
    const int j1 = floorDiv(p.bottom() + 16, kFenceCellHeight);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            TileRandom random(d.seed, i, j, kSaltFences);
            if (!random.chance(45)) continue;
            const int x = i * kFenceCellWidth + random.range(0, kFenceCellWidth / 2);
            const int y = j * kFenceCellHeight + random.range(0, kFenceCellHeight / 2);
            if (random.chance(60)) {
                const int x1 = x + random.range(280, kFenceCellWidth);
                if (p.overlaps(x - 40, y - 16, x1 + 40, y + 8)) wireFenceAcross(p, std::max(x, p.left() - 32), std::min(x1, p.right() + 32), y);
            } else {
                const int y1 = y + random.range(160, kFenceCellHeight);
                if (p.overlaps(x - 2, y - 40, x + 10, y1 + 40)) wireFenceAlong(p, x, std::max(y, p.top() - 32), std::min(y1, p.bottom() + 32));
            }
        }
    }
}

}  // namespace

int driftNoise(const Desert& d, int x, int y) {
    // Sliding along the diagonal, the noise changes slowly. Across it, quickly. That
    // stretches the blobs into streaks that lie along the wind.
    const int along = x + y + d.windX;
    const int across = x - y + d.windY;
    const int streaks = (softNoise(along, across, 7, 4) * 2 + softNoise(along + 311, across - 97, 5, 3)) / 3;
    // Some stretches of road catch far more sand than others.
    return streaks + (softNoise(x + d.windY, y + d.windX, 8, 7) - 128) / 2;
}

void paintTerrain(Paint& paint, const Desert& desert) {
    paintBase(paint, desert);
    paintCracks(paint, desert);
    paintTracks(paint, desert);
    paintScatter(paint, desert);
    paintFences(paint, desert);
}

}  // namespace ls::maps::wasteland
