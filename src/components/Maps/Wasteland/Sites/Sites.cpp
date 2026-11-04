#include "components/Maps/Wasteland/Sites/Sites.h"

#include "components/Maps/Wasteland/Props/Props.h"
#include "components/Maps/Wasteland/Roads/Roads.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::wasteland {

using namespace theme;

namespace {

enum Salt : uint32_t { kSaltCell = 401, kSaltRoadside = 409, kSaltPaint = 419 };

constexpr WallLook kCinderWall{kCinder, kCinderTop, 0, 4};
constexpr WallLook kAdobeWall{kAdobe, kAdobeTop, 0, 0};
constexpr WallLook kRustWall{kCorrugated, kCorrugated[2], 4, 0};
constexpr WallLook kGreyWall{kCorrugatedGrey, kCorrugatedGrey[2], 4, 0};

// Objects in a cluster are placed first and drawn after sorting by row, so the ones
// nearer the camera always overlap the ones behind them.
struct Placed {
    int x, y, kind;
};
constexpr int kMaxPlaced = 24;

void sortByRow(Placed* items, int count) {
    for (int i = 1; i < count; ++i) {
        const Placed item = items[i];
        int k = i - 1;
        while (k >= 0 && items[k].y > item.y) {
            items[k + 1] = items[k];
            --k;
        }
        items[k + 1] = item;
    }
}

void concreteSlab(Paint& p, int x0, int y0, int x1, int y1) {
    // Cast in big panels, each with its own tone. The desert is taking it back: skipping
    // a texel leaves the ground showing, so the slab has no ruled edge and no flat color.
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const uint32_t noise = texelNoise(x, y);
            const int edge = std::min(std::min(x - x0, x1 - 1 - x), std::min(y - y0, y1 - 1 - y));
            const int edgeBoost = edge < 10 ? (10 - edge) * 8 : 0;
            const int sand = (softNoise(x + y, x - y, 6, 3) + softNoise(x, y, 5, 4)) / 2 + edgeBoost + static_cast<int>((noise >> 5) & 15u);
            if (sand > 172) continue;
            const uint32_t panel = texelNoise(x >> 6, y >> 5);
            Color color = kApron[(panel + ((noise & 7u) == 0u ? 1u : 0u)) % 3u];
            if ((((x & 63) == 0 || (y & 31) == 0)) && (noise & 3u) != 0u) color = kApronJoint;
            p.put(x, y, color);
        }
    }
}

void slabCracks(Paint& p, TileRandom& random, int x0, int y0, int x1, int y1, int count) {
    for (int i = 0; i < count; ++i) {
        int x = random.range(x0, x1 - 1);
        int y = random.range(y0, y1 - 1);
        const int steps = random.range(6, 22);
        for (int s = 0; s < steps; ++s) {
            if (x >= x0 && x < x1 && y >= y0 && y < y1) {
                p.put(x, y, kApronJoint);
                p.put(x + 1, y + 1, kApron[2]);
            }
            x += random.range(0, 2) - (i & 1);
            y += random.range(-1, 1);
        }
    }
}

void stains(Paint& p, TileRandom& random, int x0, int y0, int x1, int y1, int count) {
    for (int i = 0; i < count; ++i) {
        groundStain(p, random.range(x0, x1), random.range(y0, y1), random.range(5, 16), random.range(2, 5), kOilStain);
    }
}

void sandOver(Paint& p, TileRandom& random, int x0, int y0, int x1, int y1, int count) {
    for (int i = 0; i < count; ++i) {
        const int rx = random.range(8, 24);
        sandMound(p, random.range(x0, x1), random.range(y0, y1), rx, std::max(2, rx / 5));
    }
}

// --- Gas station --------------------------------------------------------------

// A low ruin behind a cracked forecourt. Its canopy came down years ago: the sheet lies
// across the pumps, and only the stubs of its posts still stand.
void gasStation(Paint& p, TileRandom& r, int x0, int y0, int x1, int y1, LightList& lights) {
    const int w = x1 - x0;
    const int shopBack = y0 + 22;
    const int shopFront = y0 + 58;
    concreteSlab(p, x0, y0, x1, y1);
    slabCracks(p, r, x0, shopFront, x1, y1, 7);
    stains(p, r, x0 + 10, shopFront + 6, x1 - 10, y1 - 8, 6);

    // The shop, on the back of the lot.
    const int shopLeft = x0 + r.range(14, 40);
    const int shopRight = shopLeft + r.range(110, 140);
    p.rect(shopLeft, shopBack, shopRight, shopFront, kFloorDark);
    for (int i = 0; i < 6; ++i) {
        // Shelves that fell over, and sand that came in through the windows.
        const int sx = r.range(shopLeft + 8, shopRight - 24);
        const int sy = r.range(shopBack + 4, shopFront - 6);
        if (i < 3) p.rect(sx, sy, sx + r.range(10, 22), sy + 3, kCorrugatedGrey[r.range(0, 1)]);
        else sandMound(p, sx, sy, r.range(8, 16), r.range(2, 4));
    }
    roofSheet(p, r, shopLeft + r.range(10, 30), shopBack + r.range(2, 8), r.range(30, 50), r.range(10, 16));
    faceWall(p, r, kCinderWall, shopLeft, shopRight, shopBack, 18);
    signText(p, shopLeft + r.range(10, 40), shopBack - 12, "NO FOOD", kSignRedDark, 2);
    sideWall(p, r, kCinderWall, shopLeft, shopBack, shopFront, 16);
    sideWall(p, r, kCinderWall, shopRight - 4, shopBack, shopFront, 16);
    // The front was all glass. Now it is a low sill with a gap for the door.
    const int door = shopLeft + r.range(30, 60);
    faceWall(p, r, kCinderWall, shopLeft, door, shopFront, 6);
    faceWall(p, r, kCinderWall, door + 14, shopRight, shopFront, 6);
    for (int i = 0; i < 10; ++i) p.put(r.range(shopLeft, shopRight), shopFront + r.range(1, 5), kDustyGlassShine);

    // Pumps, and the roof that once stood over them. It came down at one end, so its
    // grey top and its striped edge are both in view: that stripe says "gas station".
    const int pumpsY = y1 - r.range(38, 46);
    const int pumpsX = x0 + w / 2 + r.range(-24, 24);
    pumpIsland(p, pumpsX, pumpsY, r);
    const bool signOnRight = r.chance(50);
    const int roofLeft = pumpsX + (signOnRight ? -112 : 14);
    constexpr int kRoofWide = 96;
    constexpr int kRoofDeep = 20;
    const int roofTilt = r.range(4, 9) * (r.chance(50) ? 1 : -1);
    const int roofY = pumpsY + r.range(-14, -4);
    for (int col = 0; col < kRoofWide; ++col) {
        const int top = roofY + roofTilt * col / kRoofWide;
        for (int row = 0; row < kRoofDeep; ++row) {
            const uint32_t noise = texelNoise((roofLeft + col) >> 2, (roofY + row) >> 1);
            Color color = kCorrugatedGrey[(col & 3) == 0 ? 0 : ((col & 3) == 1 ? 2 : 1)];
            if ((noise & 7u) == 0u) color = kRust[(noise >> 3) & 1u];
            p.put(roofLeft + col, top + row, color);
        }
        for (int row = 0; row < 6; ++row) {
            Color color = (((col + 8) >> 4) & 1) != 0 ? kCanopyStripe[0] : kCanopyStripe[1];
            if (row == 5) color = kCoal;
            if ((texelNoise((roofLeft + col) >> 1, row) & 15u) == 0u) color = kRust[1];
            p.put(roofLeft + col, top + kRoofDeep + row, color);
        }
        p.blend(roofLeft + col, top + kRoofDeep + 6, kPropShadow);
        p.blend(roofLeft + col, top + kRoofDeep + 7, kPropShadow);
    }
    // The stubs of the posts that held it up.
    for (int post : {pumpsX - 60, pumpsX + 60}) {
        p.rect(post, pumpsY - 14, post + 3, pumpsY + 2, kMetalProp[0]);
        p.rect(post, pumpsY - 14, post + 1, pumpsY + 2, kMetalProp[1]);
    }

    // The tall sign at the road, and the one lamp that still comes on after dark.
    poleSign(p, signOnRight ? x1 - 26 : x0 + 30, y1 - 6, r.chance(50) ? "GAS" : "FUEL", r);
    lampPost(p, signOnRight ? x0 + 14 : x1 - 18, shopFront + 10, lights);

    // A car that never got its last tank, tires and a burning drum someone left.
    if (r.chance(70)) carSide(p, x0 + r.range(30, w - 30), y1 - r.range(6, 16), r);
    tirePile(p, x0 + r.range(8, 22), y1 - r.range(6, 22), r);
    barrel(p, x1 - r.range(8, 16), shopFront + r.range(14, 30), r);
    barrel(p, x1 - r.range(20, 30), shopFront + r.range(14, 30), r);
    if (r.chance(55)) burningBarrel(p, x0 + w / 2 + r.range(-60, 60), shopFront + r.range(8, 16), r, lights);
    sandOver(p, r, x0, y0, x1, y1, 7);
}

// --- Shacks -------------------------------------------------------------------

void shack(Paint& p, TileRandom& r, int x, int y) {
    // x, y is the middle of the near wall. Low walls only: its roof is on the floor.
    const int half = r.range(24, 38);
    const int depth = r.range(26, 38);
    const WallLook& look = r.chance(40) ? kGreyWall : (r.chance(50) ? kRustWall : kAdobeWall);
    const int left = x - half;
    const int right = x + half;
    const int back = y - depth;
    for (int py = back; py < y; ++py) {
        for (int px = left; px < right; ++px) {
            const int row = py >> 2;
            if (softNoise(px, py, 4, 3) + static_cast<int>(texelNoise(px, py) & 31u) > 170) continue;
            p.put(px, py, (py & 3) == 0 ? kPlankGap : kPlank[(texelNoise(row, (px + row * 7) >> 4) >> 2) % 3u]);
        }
    }
    if (r.chance(75)) roofSheet(p, r, left + r.range(2, 10), back + r.range(2, 6), r.range(half, half * 2 - 10), r.range(8, std::max(9, depth - 8)));
    faceWall(p, r, look, left, right, back, r.range(13, 18));
    sideWall(p, r, look, left, back, y, r.range(8, 12));
    sideWall(p, r, look, right - 4, back, y, r.range(8, 12));
    const int door = x + r.range(-half / 2, half / 3);
    faceWall(p, r, look, left, door, y, r.range(3, 5));
    faceWall(p, r, look, door + 9, right, y, r.range(3, 5));
    if (r.chance(50)) sandMound(p, left + r.range(0, half), y + 1, r.range(8, 18), r.range(2, 3));
}

void shackCluster(Paint& p, TileRandom& r, int cx, int cy, LightList& lights) {
    Placed items[kMaxPlaced];
    int count = 0;
    const int shacks = r.range(3, 5);
    for (int i = 0; i < shacks; ++i) items[count++] = {cx + (i - shacks / 2) * r.range(66, 84) + r.range(-10, 10), cy + r.range(-50, 66), 0};
    for (int i = 0; i < 8; ++i) items[count++] = {cx + r.range(-170, 170), cy + r.range(-60, 80), r.range(1, 6)};
    sortByRow(items, count);
    // The bare trodden ground is a hint of people, before the eye finds the walls.
    groundStain(p, cx, cy + 10, 190, 74, kTrackDark);
    for (int n = 0; n < count; ++n) {
        const Placed& item = items[n];
        switch (item.kind) {
            case 0: shack(p, r, item.x, item.y); break;
            case 1: tirePile(p, item.x, item.y, r); break;
            case 2: crate(p, item.x, item.y, r); break;
            case 3: barrel(p, item.x, item.y, r); break;
            case 4: boards(p, item.x, item.y, r); break;
            case 5: litter(p, item.x, item.y, r); break;
            default: pebbles(p, item.x, item.y, 12, 5, r); break;
        }
    }
    campfire(p, cx + r.range(-30, 30), cy + r.range(4, 24), lights);
    wireFenceAcross(p, cx - r.range(140, 200), cx + r.range(140, 200), cy - 84);
}

// --- Rocks --------------------------------------------------------------------

void outcrop(Paint& p, TileRandom& r, int cx, int cy, bool red) {
    Placed items[kMaxPlaced];
    int count = 0;
    items[count++] = {cx + r.range(-40, 40), cy + r.range(-30, 10), 2};  // The main mass.
    const int shelves = r.range(1, 3);
    for (int i = 0; i < shelves; ++i) items[count++] = {cx + r.range(-130, 130), cy + r.range(-44, 40), 0};
    const int boulders = r.range(8, 14);
    for (int i = 0; i < boulders; ++i) items[count++] = {cx + r.range(-170, 170), cy + r.range(-64, 66), 1};
    sortByRow(items, count);
    for (int i = 0; i < 14; ++i) stone(p, cx + r.range(-190, 190), cy + r.range(-70, 80), r.range(1, 4), r);
    for (int n = 0; n < count; ++n) {
        const Placed& item = items[n];
        if (item.kind == 2) {
            rockShelf(p, item.x, item.y, r.range(90, 118), r.range(24, 32), red, r);
        } else if (item.kind == 0) {
            rockShelf(p, item.x, item.y, r.range(36, 70), r.range(14, 24), red, r);
        } else {
            rockShelf(p, item.x, item.y, r.range(6, 18), r.range(5, 12), red, r);
        }
    }
    pebbles(p, cx, cy + 40, 150, 30, r);
    for (int i = 0; i < 3; ++i) tuft(p, cx + r.range(-160, 160), cy + r.range(-40, 70), r);
    if (r.chance(35)) cowSkull(p, cx + r.range(-130, 130), cy + r.range(30, 70));
}

// --- Dead grove ---------------------------------------------------------------

void grove(Paint& p, TileRandom& r, int cx, int cy) {
    Placed items[kMaxPlaced];
    int count = 0;
    const int trees = r.range(7, 14);
    for (int i = 0; i < trees; ++i) items[count++] = {cx + r.range(-160, 160), cy + r.range(-66, 72), 0};
    for (int i = 0; i < 6; ++i) items[count++] = {cx + r.range(-170, 170), cy + r.range(-66, 72), r.range(1, 3)};
    sortByRow(items, count);
    // Water stood here once: a darker hollow with fallen wood around it.
    groundStain(p, cx, cy, 190, 76, kTrackDark);
    groundStain(p, cx + r.range(-40, 40), cy + r.range(-10, 10), r.range(60, 100), r.range(20, 32), kTrackDark);
    for (int n = 0; n < count; ++n) {
        const Placed& item = items[n];
        switch (item.kind) {
            case 0: deadTree(p, item.x, item.y, r); break;
            case 1: fallenLog(p, item.x, item.y, r); break;
            case 2: stump(p, item.x, item.y); break;
            default: bush(p, item.x, item.y, r); break;
        }
    }
    for (int i = 0; i < 14; ++i) tuft(p, cx + r.range(-170, 170), cy + r.range(-70, 76), r);
}

// --- Bone field ---------------------------------------------------------------

void boneField(Paint& p, TileRandom& r, int cx, int cy) {
    Placed items[kMaxPlaced];
    int count = 0;
    for (int i = 0; i < 16; ++i) items[count++] = {cx + r.range(-180, 180), cy + r.range(-72, 76), r.range(1, 9)};
    sortByRow(items, count);
    wireFenceAcross(p, cx - r.range(100, 200), cx + r.range(80, 200), cy - 86);
    looseBones(p, cx, cy, 170, 26, r);
    for (int n = 0; n < count; ++n) {
        const Placed& item = items[n];
        switch (item.kind) {
            case 1:
            case 2: ribCage(p, item.x, item.y, r.range(4, 7), r.range(7, 11)); break;
            case 3:
            case 4: cowSkull(p, item.x, item.y); break;
            case 5:
            case 6:
            case 7: humanSkeleton(p, item.x, item.y, r); break;
            default: looseBones(p, item.x, item.y, 18, 6, r); break;
        }
    }
    bigSkeleton(p, cx + r.range(-30, 30), cy + r.range(-12, 22), r);
    if (r.chance(50)) warningSign(p, cx + r.range(-160, 160), cy + r.range(-70, -50), r);
}

// --- Wreck yard ---------------------------------------------------------------

void wreckYard(Paint& p, TileRandom& r, int cx, int cy, LightList& lights) {
    Placed items[kMaxPlaced];
    int count = 0;
    const int wrecks = r.range(5, 9);
    for (int i = 0; i < wrecks; ++i) items[count++] = {cx + r.range(-170, 170), cy + r.range(-64, 72), r.range(0, 9)};
    sortByRow(items, count);
    wireFenceAcross(p, cx - 190, cx + 190, cy - 84);
    wireFenceAlong(p, cx - 190, cy - 84, cy + 76);
    for (int n = 0; n < count; ++n) {
        const Placed& item = items[n];
        if (item.kind < 5) carSide(p, item.x, item.y, r);
        else if (item.kind < 7) carOnRoof(p, item.x, item.y, r);
        else if (item.kind == 7) busSide(p, item.x, item.y, r);
        else tirePile(p, item.x, item.y, r);
        sandMound(p, item.x + r.range(-18, 18), item.y + 2, r.range(10, 20), r.range(2, 4));
    }
    if (r.chance(60)) burningBarrel(p, cx + r.range(-120, 120), cy + r.range(-30, 40), r, lights);
    if (r.chance(50)) billboard(p, cx + r.range(-110, 110), cy - 62, "EAT", r);
}

// --- Foundations --------------------------------------------------------------

// A long row of rooms, like an old roadside motel, ground down to its footings.
void foundations(Paint& p, TileRandom& r, int cx, int cy, LightList& lights) {
    const int rooms = r.range(4, 7);
    const int roomWide = r.range(30, 40);
    const int deep = r.range(40, 56);
    const int left = cx - rooms * roomWide / 2;
    const int right = left + rooms * roomWide;
    const int back = cy - deep / 2;
    const int front = back + deep;
    const WallLook& look = r.chance(50) ? kAdobeWall : kCinderWall;
    concreteSlab(p, left, back, right, front);
    slabCracks(p, r, left, back, right, front, 6);
    faceWall(p, r, look, left, right, back, r.range(9, 14));
    for (int i = 0; i <= rooms; ++i) {
        if (r.chance(75)) sideWall(p, r, look, left + i * roomWide - (i == rooms ? 4 : 0), back + 2, front - r.range(0, 12), r.range(5, 9));
    }
    for (int i = 0; i < rooms; ++i) {
        const int roomX = left + i * roomWide + 6;
        if (r.chance(40)) boards(p, roomX + 10, back + r.range(12, deep - 8), r);
        if (r.chance(30)) sandMound(p, roomX + 10, back + r.range(10, deep - 6), r.range(8, 14), r.range(2, 4));
    }
    faceWall(p, r, look, left, right, front, r.range(2, 5));
    for (int i = 0; i < 5; ++i) rubblePile(p, r.range(left, right), r.range(back + 8, front + 10), r.range(4, 8), r);
    if (r.chance(45)) burningBarrel(p, r.range(left + 10, right - 10), back + r.range(10, deep - 6), r, lights);
    signText(p, r.range(left + 6, right - 50), back - 8, "KEEP OUT", kSignRedDark, 3);
}

// --- Camp ---------------------------------------------------------------------

void abandonedCamp(Paint& p, TileRandom& r, int cx, int cy, LightList& lights) {
    groundStain(p, cx, cy, 130, 56, kTrackDark);
    tent(p, cx - r.range(50, 90), cy - r.range(16, 34), r);
    tent(p, cx + r.range(36, 70), cy - r.range(24, 40), r);
    campfire(p, cx, cy, lights);
    sandbags(p, cx - r.range(70, 100), cy + r.range(26, 40), r.range(3, 6), r);
    crate(p, cx + r.range(30, 60), cy + r.range(4, 20), r);
    crate(p, cx + r.range(-30, 10), cy + r.range(24, 40), r);
    barrel(p, cx + r.range(70, 100), cy + r.range(0, 20), r);
    tirePile(p, cx - r.range(20, 50), cy + r.range(8, 20), r);
    humanSkeleton(p, cx + r.range(-30, 30), cy + r.range(14, 28), r);
    tent(p, cx + r.range(4, 40), cy + r.range(40, 54), r);
}

// --- Placing them -------------------------------------------------------------

enum class SiteKind { None, Shacks, Rocks, RedRocks, Grove, Bones, Wrecks, Foundations, Camp };

SiteKind pickSite(Zone zone, int roll) {
    switch (zone) {
        case Zone::Dunes:
            if (roll < 56) return SiteKind::None;
            if (roll < 68) return SiteKind::Bones;
            if (roll < 80) return SiteKind::Wrecks;
            if (roll < 88) return SiteKind::Rocks;
            return SiteKind::Shacks;
        case Zone::Pan:
            if (roll < 26) return SiteKind::None;
            if (roll < 60) return SiteKind::Bones;
            if (roll < 80) return SiteKind::Foundations;
            return SiteKind::Wrecks;
        case Zone::Reg:
            if (roll < 22) return SiteKind::None;
            if (roll < 64) return SiteKind::Rocks;
            if (roll < 78) return SiteKind::Foundations;
            if (roll < 90) return SiteKind::Shacks;
            return SiteKind::Wrecks;
        case Zone::Scrub:
            if (roll < 22) return SiteKind::None;
            if (roll < 66) return SiteKind::Grove;
            if (roll < 86) return SiteKind::Shacks;
            return SiteKind::Camp;
        case Zone::RedEarth:
            if (roll < 22) return SiteKind::None;
            if (roll < 72) return SiteKind::RedRocks;
            if (roll < 86) return SiteKind::Bones;
            return SiteKind::Grove;
        default:
            if (roll < 40) return SiteKind::None;
            if (roll < 56) return SiteKind::Shacks;
            if (roll < 68) return SiteKind::Camp;
            if (roll < 78) return SiteKind::Bones;
            if (roll < 88) return SiteKind::Rocks;
            return SiteKind::Wrecks;
    }
}

// Sites sit inside this box around their center, which is how far a chunk has to look.
constexpr int kSiteReachX = 260;
constexpr int kSiteReachY = 150;
constexpr int kCellWidth = kChunkWidth * 3;
constexpr int kCellHeight = kChunkHeight * 3;

void paintCellSites(Paint& p, const Desert& d, LightList& lights) {
    const int i0 = floorDiv(p.left() - kSiteReachX - kCellWidth, kCellWidth);
    const int i1 = floorDiv(p.right() + kSiteReachX, kCellWidth);
    const int j0 = floorDiv(p.top() - kSiteReachY - kCellHeight, kCellHeight);
    const int j1 = floorDiv(p.bottom() + kSiteReachY, kCellHeight);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            TileRandom place(d.seed, i, j, kSaltCell);
            const int cx = i * kCellWidth + place.range(kSiteReachX / 2, kCellWidth - kSiteReachX / 2);
            const int cy = j * kCellHeight + place.range(kSiteReachY / 2, kCellHeight - kSiteReachY / 2);
            const int roll = place.range(0, 99);
            if (!p.overlaps(cx - kSiteReachX, cy - kSiteReachY, cx + kSiteReachX, cy + kSiteReachY)) continue;
            // Roads win: a site never lands on one, and they have their own stops anyway.
            if (nearAnyRoad(d.seed, cx, cy, kSiteReachX + 20, kSiteReachY + 20)) continue;
            const SiteKind kind = pickSite(zoneAt(d, cx, cy), roll);
            TileRandom r(d.seed, i, j, kSaltPaint);
            switch (kind) {
                case SiteKind::Shacks: shackCluster(p, r, cx, cy, lights); break;
                case SiteKind::Rocks: outcrop(p, r, cx, cy, false); break;
                case SiteKind::RedRocks: outcrop(p, r, cx, cy, true); break;
                case SiteKind::Grove: grove(p, r, cx, cy); break;
                case SiteKind::Bones: boneField(p, r, cx, cy); break;
                case SiteKind::Wrecks: wreckYard(p, r, cx, cy, lights); break;
                case SiteKind::Foundations: foundations(p, r, cx, cy, lights); break;
                case SiteKind::Camp: abandonedCamp(p, r, cx, cy, lights); break;
                case SiteKind::None: break;
            }
        }
    }
}

// Highway stops: gas stations mostly, a few shack clusters. Only on straight, clear road.
constexpr int kStopWidth = 280;
constexpr int kStopHeight = 150;

void paintRoadsideStops(Paint& p, const Desert& d, const RoadFamily& f, LightList& lights) {
    const int stretchLength = f.across ? kChunkWidth * 6 : kChunkHeight * 6;
    const int reachPerp = (f.across ? kStopHeight : kStopWidth) + 90;
    const int perpLow = (f.across ? p.top() : p.left()) - reachPerp;
    const int perpHigh = (f.across ? p.bottom() : p.right()) + reachPerp;
    const int alongLow = (f.across ? p.left() : p.top()) - stretchLength;
    const int alongHigh = (f.across ? p.right() : p.bottom()) + stretchLength;
    for (int index = firstRoadNear(f, perpLow); index <= lastRoadNear(f, perpHigh); ++index) {
        const int base = roadBase(d.seed, f, index);
        if (base + f.reach < perpLow || base - f.reach > perpHigh) continue;
        for (int stretch = floorDiv(alongLow, stretchLength); stretch <= floorDiv(alongHigh, stretchLength); ++stretch) {
            TileRandom place(d.seed, index, stretch, f.salt + kSaltRoadside);
            const int along = stretch * stretchLength + place.range(stretchLength / 5, stretchLength * 4 / 5);
            const int roll = place.range(0, 99);
            const bool highSide = !f.across && place.chance(50);
            if (roll >= 52) continue;
            const int c = roadCenter(d.seed, f, index, along);
            const int half = roadHalfWidth(d.seed, f, index);
            if (roadBurial(d.seed, f, index, along) > 40) continue;
            const int margin = f.across ? 160 : 90;
            if (roadCenter(d.seed, f, index, along - margin) != c || roadCenter(d.seed, f, index, along + margin) != c) continue;
            int x0, y0;
            if (f.across) {
                // Always on the far side, so the forecourt faces the road and the camera.
                x0 = along - kStopWidth / 2;
                y0 = c - half - kStopHeight - 1;
            } else {
                x0 = highSide ? c + half + 2 : c - half - kStopWidth - 2;
                y0 = along - kStopHeight / 2;
            }
            if (!p.overlaps(x0 - 120, y0 - 70, x0 + kStopWidth + 120, y0 + kStopHeight + 70)) continue;
            // Another road through the lot would look wrong, so the stop moves on.
            const int centerX = x0 + kStopWidth / 2;
            const int centerY = y0 + kStopHeight / 2;
            const RoadFamily& other = f.across ? kUpRoads : kAcrossRoads;
            bool blocked = false;
            const int otherPerp = other.across ? centerY : centerX;
            for (int k = firstRoadNear(other, otherPerp - 300); k <= lastRoadNear(other, otherPerp + 300); ++k) {
                const int oc = roadCenter(d.seed, other, k, other.across ? centerX : centerY);
                const int clear = (other.across ? kStopHeight / 2 : kStopWidth / 2) + roadHalfWidth(d.seed, other, k) + 24;
                if (std::abs(oc - otherPerp) <= clear) blocked = true;
            }
            if (blocked) continue;
            TileRandom r(d.seed, index, stretch, f.salt + kSaltRoadside + 1u);
            if (roll < 40) gasStation(p, r, x0, y0, x0 + kStopWidth, y0 + kStopHeight, lights);
            else shackCluster(p, r, centerX, centerY, lights);
        }
    }
}

}  // namespace

void paintSites(Paint& paint, const Desert& desert, LightList& lights) {
    paintCellSites(paint, desert, lights);
    paintRoadsideStops(paint, desert, kAcrossRoads, lights);
    paintRoadsideStops(paint, desert, kUpRoads, lights);
}

}  // namespace ls::maps::wasteland
