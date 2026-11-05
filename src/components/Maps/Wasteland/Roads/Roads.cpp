#include "components/Maps/Wasteland/Roads/Roads.h"

#include "components/Maps/Wasteland/Props/Props.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::wasteland {

using namespace theme;

namespace {

// One road near the chunk being painted, with its center worked out once per column or
// row of the chunk, because the painting loops ask for it constantly.
struct Span {
    const RoadFamily* family;
    int index;
    RoadStyle style;
    int center[kChunkWidth];
    unsigned char burial[kChunkWidth];
};

constexpr int kMaxSpans = 6;

int alongOf(const RoadFamily& f, int x, int y) { return f.across ? x : y; }
int perpOf(const RoadFamily& f, int x, int y) { return f.across ? y : x; }

// Local index along the chunk, for a span's tables.
int tableIndex(const Paint& p, const RoadFamily& f, int along) { return along - (f.across ? p.left() : p.top()); }
int tableSize(const RoadFamily& f) { return f.across ? kChunkWidth : kChunkHeight; }

// Sand beats asphalt where the blown streaks are strong, near the edges, and wherever the
// road is being buried. Skipping the asphalt leaves the real desert underneath showing.
bool sandCovers(const Desert& d, int x, int y, int edgeDistance, int burial, uint32_t noise) {
    const int edgeBoost = edgeDistance < 10 ? (10 - edgeDistance) * 4 : 0;
    const int level = driftNoise(d, x, y) + edgeBoost + burial * 3 / 4 + static_cast<int>((noise >> 5) & 15u);
    return level > 182;
}

Color asphaltTexel(int x, int y, int laneWear, uint32_t noise) {
    int tone = (softNoise(x, y, 5, 4) + static_cast<int>((noise >> 5) & 31u) - 15) * 3 >> 8;
    if (laneWear > 0 && (noise & 3u) != 0u) tone++;
    if ((noise & 31u) == 0u) return kOldAsphaltLit;
    if ((noise & 31u) == 1u) return kOldAsphaltCrack;
    return kOldAsphalt[std::clamp(tone, 0, 2)];
}

bool insideOtherRoad(const Span* spans, int count, const Span& self, const Paint& p, int x, int y) {
    for (int i = 0; i < count; ++i) {
        const Span& other = spans[i];
        if (&other == &self || other.family == self.family) continue;
        const int along = tableIndex(p, *other.family, alongOf(*other.family, x, y));
        if (along < 0 || along >= tableSize(*other.family)) continue;
        if (std::abs(perpOf(*other.family, x, y) - other.center[along]) <= other.style.half + 3) return true;
    }
    return false;
}

// The gravel strip beside the road. It only speckles over the desert, so the ground's
// own color carries through.
void paintShoulder(Paint& p, const Span& s) {
    const RoadFamily& f = *s.family;
    const int alongLow = f.across ? p.left() : p.top();
    const int perpLow = f.across ? p.top() : p.left();
    const int perpHigh = f.across ? p.bottom() : p.right();
    for (int i = 0; i < tableSize(f); ++i) {
        if (s.burial[i] >= 250) continue;
        const int along = alongLow + i;
        const int c = s.center[i];
        for (int side = -1; side <= 1; side += 2) {
            for (int off = s.style.half + 1; off <= s.style.half + f.shoulder; ++off) {
                const int perp = c + side * off;
                if (perp < perpLow || perp >= perpHigh) continue;
                const int x = f.across ? along : perp;
                const int y = f.across ? perp : along;
                const uint32_t noise = texelNoise(x, y);
                const int chance = (s.style.half + f.shoulder - off) * 4 / f.shoulder + 1;
                if (static_cast<int>(noise & 7u) >= chance || s.burial[i] > 40) continue;
                p.put(x, y, (noise & 56u) == 0u ? kStone[2] : kReg[(noise >> 6) % 3u]);
            }
        }
    }
}

bool flakeOff(int x, int y, uint32_t wear) { return (texelNoise(x, y) & 15u) < wear; }

void paintAsphalt(Paint& p, const Desert& d, const Span& s, const Span* spans, int spanCount) {
    const RoadFamily& f = *s.family;
    const RoadStyle& style = s.style;
    const int alongLow = f.across ? p.left() : p.top();
    const int perpLow = f.across ? p.top() : p.left();
    const int perpHigh = f.across ? p.bottom() : p.right();
    const int dashPeriod = f.across ? 48 : 28;
    const int dashOn = f.across ? 26 : 15;
    const int lineHalf = f.across ? 0 : 1;
    for (int i = 0; i < tableSize(f); ++i) {
        const int burial = s.burial[i];
        if (burial >= 250) continue;
        const int along = alongLow + i;
        const int c = s.center[i];
        // The edge is eaten back unevenly, and a whole lane fades out now and then.
        const int bite = softNoise(along, s.index * 97, 3, 0) >> 6;
        const int from = std::max(perpLow, c - style.half);
        const int to = std::min(perpHigh - 1, c + style.half);
        const bool paintWorn = softNoise(along + s.index * 13, s.index, 7, 0) < 80;
        for (int perp = from; perp <= to; ++perp) {
            const int off = perp - c;
            const int abs = std::abs(off);
            const int edge = style.half - abs;
            if (edge < bite) continue;
            if (style.divided && abs <= f.median) continue;
            const int x = f.across ? along : perp;
            const int y = f.across ? perp : along;
            const uint32_t noise = texelNoise(x, y);
            if (sandCovers(d, x, y, edge, burial, noise)) continue;
            // Tires polish two strips in each lane.
            const int lane = style.half / 2;
            const int wearBand = std::abs(abs - lane);
            Color color = asphaltTexel(x, y, wearBand > lane / 5 && wearBand < lane * 3 / 5 ? 1 : 0, noise);
            // Paint goes last, and only in flakes.
            if (!paintWorn && !insideOtherRoad(spans, spanCount, s, p, x, y)) {
                if (edge >= 3 && edge <= 3 + lineHalf && !flakeOff(x, y, 7u)) color = kFadedWhite;
                const int dashSpot = floorMod(along + s.index * 7, dashPeriod);
                if (style.divided) {
                    const int laneLine = (style.half + f.median) / 2;
                    if (std::abs(abs - laneLine) <= lineHalf && dashSpot < dashOn && !flakeOff(x, y, 5u)) color = kFadedWhite;
                } else if (style.centerLine == 0) {
                    if (abs <= lineHalf && dashSpot < dashOn && !flakeOff(x, y, 4u)) color = kFadedYellow;
                } else if (style.centerLine == 1) {
                    const int gap = lineHalf + 1;
                    if (abs >= gap && abs <= gap + lineHalf && !flakeOff(x, y, 5u)) color = kFadedYellow;
                } else if (abs <= lineHalf && dashSpot < dashOn / 2 && !flakeOff(x, y, 9u)) {
                    color = kFadedWhite;
                }
            }
            p.put(x, y, color);
        }
    }
}

// --- Damage -------------------------------------------------------------------

// Marks that may only land on bare road. A point outside this chunk can't be seen here,
// so it doesn't matter what the answer is for it.
struct RoadPen {
    Paint& p;
    const Desert& d;
    const Span& s;

    bool onRoad(int x, int y) const {
        const RoadFamily& f = *s.family;
        const int i = tableIndex(p, f, alongOf(f, x, y));
        if (i < 0 || i >= tableSize(f)) return false;
        const int off = std::abs(perpOf(f, x, y) - s.center[i]);
        const int edge = s.style.half - off;
        if (edge < 2 || (s.style.divided && off <= f.median)) return false;
        return !sandCovers(d, x, y, edge, s.burial[i], texelNoise(x, y));
    }
    void put(int x, int y, Color color) const {
        if (onRoad(x, y)) p.put(x, y, color);
    }
    void blend(int x, int y, Color color) const {
        if (onRoad(x, y)) p.blend(x, y, color);
    }
};

void crack(const RoadPen& pen, TileRandom& random, int x, int y, int steps, bool alongX, int depth) {
    int cx = x;
    int cy = y;
    for (int i = 0; i < steps; ++i) {
        pen.put(cx, cy, kOldAsphaltCrack);
        pen.put(cx + 1, cy + 1, kOldAsphaltLit);
        const int run = random.range(1, 4);
        for (int j = 0; j < run; ++j) {
            cx += alongX ? 1 : random.range(-1, 1);
            cy += alongX ? random.range(-1, 1) : 1;
            pen.put(cx, cy, kOldAsphaltCrack);
            if (j % 2 == 0) pen.put(cx + 1, cy + 1, kOldAsphaltLit);
        }
        if (depth > 0 && random.chance(14)) crack(pen, random, cx, cy, steps / 3, !alongX, depth - 1);
    }
}

// Old repairs: wiggly lines of black tar along a crack, the signature of a long highway.
void tarSnake(const RoadPen& pen, TileRandom& random, int x, int y, int length, bool alongX) {
    int cx = x;
    int cy = y;
    for (int i = 0; i < length; ++i) {
        pen.put(cx, cy, kTarSnake);
        pen.put(cx + (alongX ? 0 : 1), cy + (alongX ? 1 : 0), kTarSnake);
        if (alongX) {
            cx++;
            if ((i & 7) == 0) cy += random.range(-1, 1);
        } else {
            cy++;
            if ((i & 3) == 0) cx += random.range(-2, 2);
        }
    }
}

void pothole(const RoadPen& pen, TileRandom& random, int x, int y) {
    const int rx = random.range(3, 7);
    const int ry = std::max(2, rx / 2);
    for (int oy = -ry - 1; oy <= ry + 1; ++oy) {
        for (int ox = -rx - 1; ox <= rx + 1; ++ox) {
            const int outer = ox * ox * ry * ry + oy * oy * rx * rx;
            const int limit = rx * rx * ry * ry;
            if (outer <= limit / 2) pen.put(x + ox, y + oy, kPothole);
            else if (outer <= limit) pen.put(x + ox, y + oy, kOldAsphaltCrack);
            else if (outer <= limit * 3 / 2 && oy > 0) pen.put(x + ox, y + oy, kOldAsphaltLit);
        }
    }
    // Potholes fill with blown sand.
    if (random.chance(55)) {
        for (int ox = -rx / 2; ox <= rx / 2; ++ox) pen.put(x + ox, y + ry / 2, kSand[1]);
    }
}

void skidMarks(const RoadPen& pen, TileRandom& random, int x, int y, int length, bool alongX) {
    const int gap = alongX ? 7 : 12;
    const int drift = random.range(-6, 6);
    for (int i = 0; i < length; ++i) {
        if ((texelNoise(i, x) & 7u) == 0u) continue;
        const int bend = drift * i / std::max(1, length);
        const int px = alongX ? x + i : x + bend;
        const int py = alongX ? y + bend : y + i;
        pen.blend(px, py, kSkidMark);
        pen.blend(alongX ? px : px + gap, alongX ? py + gap : py, kSkidMark);
    }
}

void asphaltPatch(const RoadPen& pen, int x, int y, int w, int h) {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            const bool border = py == y || px == x;
            pen.put(px, py, border ? kOldAsphaltCrack : ((texelNoise(px, py) & 7u) == 0u ? kOldAsphalt[0] : kOldAsphaltPatch));
        }
    }
}

void paintDamage(Paint& p, const Desert& d, const Span& s) {
    const RoadFamily& f = *s.family;
    const int len = slotLength(f);
    constexpr int kReach = 120;  // The longest crack or tar line a slot can start.
    const int low = f.across ? p.left() : p.top();
    const int high = f.across ? p.right() : p.bottom();
    const RoadPen pen{p, d, s};
    const int half = s.style.half;
    for (int slot = floorDiv(low - kReach, len); slot <= floorDiv(high + kReach, len); ++slot) {
        TileRandom random(d.seed, s.index, slot, f.salt + kSaltDamage);
        const int along = slot * len + random.range(0, len - 1);
        const int c = roadCenter(d.seed, f, s.index, along);
        const int off = random.range(-half + 2, half - 2);
        const int x = f.across ? along : c + off;
        const int y = f.across ? c + off : along;
        const int roll = random.range(0, 99);
        if (roll < 34) crack(pen, random, x, y, random.range(4, 12), random.chance(f.across ? 40 : 60), 2);
        else if (roll < 50) tarSnake(pen, random, x, y, random.range(20, 60), f.across);
        else if (roll < 62) pothole(pen, random, x, y);
        else if (roll < 72) asphaltPatch(pen, x, y, f.across ? random.range(16, 40) : random.range(16, 34), f.across ? random.range(6, 14) : random.range(10, 26));
    }
}

// --- Roadside furniture -------------------------------------------------------

struct Pole {
    bool exists, snapped;
    int x, y, lean;
};

// Telephone poles march along one side, every few slots.
Pole poleOf(uint64_t seed, const RoadFamily& f, int index, const RoadStyle& style, int number) {
    const int gap = f.across ? 176 : 96;
    TileRandom random(seed, index, number, f.salt + kSaltLine);
    const int along = number * gap;
    if (!style.poles || roadBurial(seed, f, index, along) > 200) return {false, false, 0, 0, 0};
    const int side = style.polesOnLowSide ? 1 : -1;
    const int off = side * (style.half + f.shoulder + (f.across ? 12 : 22));
    const RoadPoint at = roadPoint(seed, f, index, along, off);
    const int roll = random.range(0, 99);
    if (nearAnyRoad(seed, at.x, at.y, 0, 0)) return {false, false, 0, 0, 0};
    return {roll >= 12, roll < 30, at.x, at.y, random.range(-3, 3)};
}

void paintPoles(Paint& p, const Desert& d, const Span& s) {
    const RoadFamily& f = *s.family;
    if (!s.style.poles) return;
    const int gap = f.across ? 176 : 96;
    const int low = f.across ? p.left() : p.top();
    const int high = f.across ? p.right() : p.bottom();
    for (int n = floorDiv(low - gap - 40, gap); n <= floorDiv(high + gap + 40, gap); ++n) {
        const Pole a = poleOf(d.seed, f, s.index, s.style, n);
        if (!a.exists) continue;
        // Up the road first, so nearer poles overlap farther ones.
        const Pole b = poleOf(d.seed, f, s.index, s.style, n + 1);
        if (b.exists && !a.snapped && !b.snapped) {
            powerWire(p, a.x + a.lean - 6, a.y - 33, b.x + b.lean - 6, b.y - 33);
            powerWire(p, a.x + a.lean + 7, a.y - 33, b.x + b.lean + 7, b.y - 33);
        }
        telephonePole(p, a.x, a.y, a.lean, a.snapped);
    }
}

constexpr const char* kRoadSignWords[] = {"NO GAS", "EXIT", "FUEL 90", "TURN BACK", "NO FOOD", "DEAD END", "KEEP OUT", "EAT"};

// On the road itself: the layout decides what stands here, and its stream paints the looks.
void furnishRoad(Paint& p, const Desert& d, const Span& s, int slot, LightList& lights) {
    const RoadFamily& f = *s.family;
    TileRandom random = slotStream(d.seed, f, s.index, slot, 1);
    const RoadSlot decided = decideRoadSlot(d.seed, f, s.index, s.style, slot, random);
    if (decided.skidMarks) {
        const RoadPen pen{p, d, s};
        const int half = s.style.half;
        const RoadPoint at = roadPoint(d.seed, f, s.index, decided.along, random.range(-half / 2, half / 2));
        skidMarks(pen, random, at.x, at.y, random.range(24, 70), f.across);
    }
    if (!decided.wreck) return;
    {
        CarMark mark(p, decided.sprite, decided.x, decided.y);
        switch (decided.sprite) {
            case CarSprite::BusSide: busSide(p, decided.x, decided.y, random); break;
            case CarSprite::TankerSide: tankerSide(p, decided.x, decided.y, random); break;
            case CarSprite::WreckOnRoof: carOnRoof(p, decided.x, decided.y, random); break;
            case CarSprite::BusEnd: busEnd(p, decided.x, decided.y, random); break;
            case CarSprite::WreckEnd: carEnd(p, decided.x, decided.y, random); break;
            default: carSide(p, decided.x, decided.y, random); break;
        }
    }
    // One wreck in a while is still burning, which gives the night its few fires. The boss
    // stamps out the fire when it flattens the car. The chance is still drawn, so that nothing
    // painted after this changes.
    const bool burning = random.chance(9);
    if (burning && !p.crushed().holds(decided.x, decided.y)) wreckFire(p, decided.x, decided.y, lights);
}

void furnishSlot(Paint& p, const Desert& d, const Span& s, int slot, int layer, LightList& lights) {
    if (layer == 1) {
        furnishRoad(p, d, s, slot, lights);
        return;
    }
    const RoadFamily& f = *s.family;
    const int len = slotLength(f);
    TileRandom random = slotStream(d.seed, f, s.index, slot, layer);
    const int along = slot * len + random.range(0, len - 1);
    const int burial = roadBurial(d.seed, f, s.index, along);
    const int half = s.style.half;
    const int roll = random.range(0, 99);
    // Beside the road, above it for layer 0 and below it for layer 2.
    const int side = layer == 0 ? -1 : 1;
    const int off = side * (half + f.shoulder + random.range(1, f.across ? 8 : 14));
    const RoadPoint at = roadPoint(d.seed, f, s.index, along, off);
    if (burial > 200 && roll > 3) return;
    if (nearAnyRoad(d.seed, at.x, at.y, 0, 0)) return;  // Nothing stands on a crossing road.
    if (roll < 26) {
        if (burial < 120) roadPost(p, at.x, at.y);
    } else if (roll < 32) {
        const int span = f.across ? random.range(30, 90) : random.range(20, 50);
        if (f.across) guardRailAcross(p, at.x, at.x + span, at.y, random);
        else guardRailAlong(p, at.x, at.y, at.y + span, random);
    } else if (roll == 32) {
        // Signs are rare on purpose: this is a long lonely road, not a city street.
        guideSign(p, at.x, at.y, kRoadSignWords[random.range(0, 7)], random);
    } else if (roll == 33) {
        warningSign(p, at.x, at.y, random);
    } else if (roll == 34) {
        plateSign(p, at.x, at.y, random);
    } else if (roll < 38) {
        tirePile(p, at.x, at.y, random);
    } else if (roll < 41) {
        barrel(p, at.x, at.y, random);
    } else if (roll < 43) {
        litter(p, at.x, at.y, random);
    } else if (roll == 43) {
        humanSkeleton(p, at.x, at.y, random);
    } else if (roll < 50) {
        tuft(p, at.x, at.y, random);
    } else if (roll == 50) {
        const int boardX = f.across ? at.x : at.x + side * 46;
        const int boardY = f.across ? at.y + side * 30 : at.y;
        if (!nearAnyRoad(d.seed, boardX, boardY, 34, 8)) billboard(p, boardX, boardY, kRoadSignWords[random.range(0, 7)], random);
    } else if (roll == 51) {
        // Someone keeps a fire going by the road. The light is the only one for miles.
        burningBarrel(p, at.x, at.y, random, lights);
    } else if (roll < 56) {
        sandMound(p, at.x, at.y, random.range(8, 20), random.range(2, 4));
    }
}

void paintFurniture(Paint& p, const Desert& d, const Span& s, LightList& lights) {
    const RoadFamily& f = *s.family;
    const int len = slotLength(f);
        const int low = (f.across ? p.left() : p.top()) - kWreckReachAlong;
    const int high = (f.across ? p.right() : p.bottom()) + kWreckReachAlong;
    const int slot0 = floorDiv(low, len);
    const int slot1 = floorDiv(high, len);
    if (f.across) {
        // Back to front: the far roadside, then the road, then the near roadside.
        for (int layer = 0; layer < 3; ++layer) {
            if (layer == (s.style.polesOnLowSide ? 2 : 0)) paintPoles(p, d, s);
            for (int slot = slot0; slot <= slot1; ++slot) furnishSlot(p, d, s, slot, layer, lights);
        }
    } else {
        // Going up the screen, each slot is nearer than the last as the row grows.
        paintPoles(p, d, s);
        for (int slot = slot0; slot <= slot1; ++slot) {
            for (int layer = 0; layer < 3; ++layer) furnishSlot(p, d, s, slot, layer, lights);
        }
    }
}

// --- Crossings ----------------------------------------------------------------

void paintCrossing(Paint& p, const Desert& d, const Span& across, const Span& up, LightList& lights) {
    // Each road's position depends on the other's, but they change so slowly that two
    // rounds of asking are enough to find where they meet.
    int x = roadCenter(d.seed, *up.family, up.index, roadBase(d.seed, *across.family, across.index));
    int y = roadCenter(d.seed, *across.family, across.index, x);
    x = roadCenter(d.seed, *up.family, up.index, y);
    y = roadCenter(d.seed, *across.family, across.index, x);
    const int halfX = up.style.half;
    const int halfY = across.style.half;
    if (!p.overlaps(x - halfX - 110, y - halfY - 80, x + halfX + 110, y + halfY + 80)) return;
    if (roadBurial(d.seed, *across.family, across.index, x) > 150 || roadBurial(d.seed, *up.family, up.index, y) > 150) return;
    TileRandom random(d.seed, across.index, up.index, kSaltCrossing + 503u);
    const RoadPen pen{p, d, across};
    crack(pen, random, x + random.range(-20, 20), y, 12, true, 2);
    guideSign(p, x + halfX + 22, y - halfY - 14, kRoadSignWords[random.range(0, 7)], random);
    if (random.chance(60)) warningSign(p, x - halfX - 18, y - halfY - 12, random);
    if (random.chance(60)) plateSign(p, x + halfX + 16, y + halfY + 22, random);
    if (random.chance(35)) burningBarrel(p, x - halfX - 22, y + halfY + 18, random, lights);
    else tirePile(p, x - halfX - 22, y + halfY + 18, random);
}

}  // namespace

void paintRoads(Paint& p, const Desert& d, LightList& lights) {
    // Furniture stands up to this far from the road's edge, so a road just outside the
    // chunk can still reach into it.
    constexpr int kFurnitureReachAcross = 160;
    constexpr int kFurnitureReachUp = 200;

    Span spans[kMaxSpans];
    int spanCount = 0;
    for (const RoadFamily* family : {&kAcrossRoads, &kUpRoads}) {
        const RoadFamily& f = *family;
        const int reach = f.across ? kFurnitureReachAcross : kFurnitureReachUp;
        const int low = (f.across ? p.top() : p.left()) - reach;
        const int high = (f.across ? p.bottom() : p.right()) + reach;
        for (int index = firstRoadNear(f, low); index <= lastRoadNear(f, high); ++index) {
            if (spanCount == kMaxSpans) break;
            const int base = roadBase(d.seed, f, index);
            if (base + f.reach < low || base - f.reach > high) continue;
            Span& s = spans[spanCount++];
            s.family = &f;
            s.index = index;
            s.style = styleOf(d.seed, f, index);
            const int alongLow = f.across ? p.left() : p.top();
            for (int i = 0; i < tableSize(f); ++i) {
                s.center[i] = roadCenter(d.seed, f, index, alongLow + i);
                s.burial[i] = static_cast<unsigned char>(roadBurial(d.seed, f, index, alongLow + i));
            }
        }
    }
    for (int i = 0; i < spanCount; ++i) paintShoulder(p, spans[i]);
    for (int i = 0; i < spanCount; ++i) paintAsphalt(p, d, spans[i], spans, spanCount);
    for (int i = 0; i < spanCount; ++i) paintDamage(p, d, spans[i]);
    for (int i = 0; i < spanCount; ++i) paintFurniture(p, d, spans[i], lights);
    for (int i = 0; i < spanCount; ++i) {
        if (!spans[i].family->across) continue;
        for (int k = 0; k < spanCount; ++k) {
            if (!spans[k].family->across) paintCrossing(p, d, spans[i], spans[k], lights);
        }
    }
}

}  // namespace ls::maps::wasteland
