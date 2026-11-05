#include "components/Maps/Layout/WastelandLayout/WastelandLayout.h"

#include "data/Balance.h"

#include <algorithm>
#include <cstdlib>

namespace ls::maps::wasteland {

namespace {

constexpr uint32_t kSaltFields = 301;
constexpr int kStepsPerRun = 3;

// A value that holds steady for a run of steps, then eases to the next run's value over
// one step. Roads stay dead straight for a long way, and bends have no sharp corner.
template <typename ValueOfRun>
int eased(int along, int step, ValueOfRun valueOfRun) {
    const int k = floorDiv(along, step);
    const int run = floorDiv(k, kStepsPerRun);
    const int value = valueOfRun(run);
    if (k - run * kStepsPerRun != 0) return value;
    const int previous = valueOfRun(run - 1);
    if (previous == value) return value;
    const int t = (along - k * step) * 1024 / step;
    const int smooth = ((t * t) >> 10) * (3072 - 2 * t) >> 10;
    return previous + (value - previous) * smooth / 1024;
}

int bendOfRun(uint64_t seed, const RoadFamily& f, int index, int run) {
    TileRandom random(seed, index, run, f.salt + kSaltBend);
    return random.chance(50) ? 0 : random.range(-f.reach, f.reach);
}

int burialOfRun(uint64_t seed, const RoadFamily& f, int index, int run) {
    TileRandom random(seed, index, run, f.salt + kSaltBurial);
    const int roll = random.range(0, 99);
    return roll < 50 ? 0 : (roll < 78 ? 70 : 255);
}

// A stretch of road can be littered with a pileup, or nearly empty.
int wreckLevel(uint64_t seed, const RoadFamily& f, int index, int along) {
    const int stretch = floorDiv(along, f.across ? 1344 : 784);
    TileRandom random(seed, index, stretch, f.salt + kSaltStretch);
    const int roll = random.range(0, 99);
    return roll < balance::kHighwayPileupStretchPercent ? 3 : (roll < balance::kHighwayPileupStretchPercent + balance::kHighwayLitteredStretchPercent ? 1 : 0);
}

}  // namespace

Desert desertFor(uint64_t seed) {
    TileRandom random(seed, 0, 0, kSaltFields);
    constexpr int kSlide = 1 << 20;
    Desert d{};
    d.seed = seed;
    d.ax = random.range(0, kSlide);
    d.ay = random.range(0, kSlide);
    d.bx = random.range(0, kSlide);
    d.by = random.range(0, kSlide);
    d.windX = random.range(0, kSlide);
    d.windY = random.range(0, kSlide);
    return d;
}

int fieldA(const Desert& d, int x, int y) { return (softNoise(x + d.ax, y + d.ay, 10, 9) * 3 + softNoise(x + d.bx, y + d.ay, 7, 6)) / 4; }
int fieldB(const Desert& d, int x, int y) { return (softNoise(x + d.bx, y + d.by, 10, 9) * 3 + softNoise(x + d.ax, y + d.by, 7, 6)) / 4; }

Zone classifyZone(int a, int b) {
    if (a > kDunesAbove) return Zone::Dunes;
    if (a < kFlatsBelow) return b < 128 ? Zone::Pan : Zone::Reg;
    if (b > 150) return Zone::Scrub;
    if (b < 104) return Zone::RedEarth;
    return Zone::Sand;
}

Zone zoneAt(const Desert& d, int x, int y) { return classifyZone(fieldA(d, x, y), fieldB(d, x, y)); }

RoadStyle styleOf(uint64_t seed, const RoadFamily& f, int index) {
    TileRandom random(seed, index, 0, f.salt + kSaltStyle);
    RoadStyle style{};
    style.divided = random.chance(22);
    style.half = style.divided ? f.dividedHalf : f.halfWidth;
    style.centerLine = random.range(0, 2);
    style.poles = random.chance(70);
    style.polesOnLowSide = random.chance(50);
    return style;
}

int roadBase(uint64_t seed, const RoadFamily& f, int index) {
    TileRandom random(seed, index, 0, f.salt + kSaltBase);
    return index * f.spacing + random.range(-f.jitter, f.jitter);
}

int roadCenter(uint64_t seed, const RoadFamily& f, int index, int along) {
    return roadBase(seed, f, index) + eased(along, f.step, [&](int run) { return bendOfRun(seed, f, index, run); });
}

int roadBurial(uint64_t seed, const RoadFamily& f, int index, int along) {
    return eased(along, f.step, [&](int run) { return burialOfRun(seed, f, index, run); });
}

int roadHalfWidth(uint64_t seed, const RoadFamily& f, int index) { return styleOf(seed, f, index).half; }

bool nearAnyRoad(uint64_t seed, int x, int y, int marginX, int marginY) {
    for (int index = firstRoadNear(kAcrossRoads, y - marginY); index <= lastRoadNear(kAcrossRoads, y + marginY); ++index) {
        if (std::abs(roadCenter(seed, kAcrossRoads, index, x) - y) <= roadHalfWidth(seed, kAcrossRoads, index) + marginY) return true;
    }
    for (int index = firstRoadNear(kUpRoads, x - marginX); index <= lastRoadNear(kUpRoads, x + marginX); ++index) {
        if (std::abs(roadCenter(seed, kUpRoads, index, y) - x) <= roadHalfWidth(seed, kUpRoads, index) + marginX) return true;
    }
    return false;
}

RoadPoint roadPoint(uint64_t seed, const RoadFamily& f, int index, int along, int off) {
    const int c = roadCenter(seed, f, index, along);
    return {along, c, f.across ? along : c + off, f.across ? c + off : along};
}

RoadSlot decideRoadSlot(uint64_t seed, const RoadFamily& f, int index, const RoadStyle& style, int slot, TileRandom& random) {
    RoadSlot result;
    const int len = slotLength(f);
    result.along = slot * len + random.range(0, len - 1);
    const int burial = roadBurial(seed, f, index, result.along);
    const int half = style.half;
    const int roll = random.range(0, 99);
    // Wrecks on the road itself, more of them in a bad stretch.
    const int level = wreckLevel(seed, f, index, result.along);
    if (burial > 120) return result;
    const int chance = level == 3 ? balance::kHighwayWreckPercentPileup : (level == 1 ? balance::kHighwayWreckPercentLittered : balance::kHighwayWreckPercentEmpty);
    if (roll >= chance) {
        result.skidMarks = roll < chance + 6;
        return result;
    }
    const int lane = (random.chance(50) ? -1 : 1) * (style.divided ? (half + f.median) / 2 : half / 2);
    const RoadPoint at = roadPoint(seed, f, index, result.along, lane + random.range(-3, 3) + (f.across ? 7 : 0));
    const int pick = random.range(0, 99);
    if (f.across) {
        result.sprite = pick < 10 ? CarSprite::BusSide : (pick < 18 ? CarSprite::TankerSide : (pick < 34 ? CarSprite::WreckOnRoof : CarSprite::WreckSide));
    } else {
        result.sprite = pick < 10 ? CarSprite::BusEnd : CarSprite::WreckEnd;
    }
    result.x = at.x;
    result.y = at.y;
    result.wreck = clearOfStart(makeObstacle(result.sprite, at.x, at.y));
    return result;
}

SiteSpot siteSpot(uint64_t seed, int i, int j) {
    TileRandom place(seed, i, j, kSaltCell);
    const int x = i * kCellWidth + place.range(kSiteReachX / 2, kCellWidth - kSiteReachX / 2);
    const int y = j * kCellHeight + place.range(kSiteReachY / 2, kCellHeight - kSiteReachY / 2);
    const int roll = place.range(0, 99);
    return {x, y, roll};
}

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

SiteKind siteKindAt(const Desert& d, const SiteSpot& spot) {
    if (nearAnyRoad(d.seed, spot.x, spot.y, kSiteReachX + 20, kSiteReachY + 20)) return SiteKind::None;
    return pickSite(zoneAt(d, spot.x, spot.y), spot.roll);
}

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

int planWreckYard(const SiteSpot& spot, TileRandom& r, Placed* items) {
    int count = 0;
    const int wrecks = r.range(5, 9);
    for (int i = 0; i < wrecks; ++i) items[count++] = {spot.x + r.range(-170, 170), spot.y + r.range(-64, 72), r.range(0, 9)};
    sortByRow(items, count);
    return count;
}

bool wreckYardSprite(int kind, CarSprite* sprite) {
    if (kind < 5) *sprite = CarSprite::WreckSide;
    else if (kind < 7) *sprite = CarSprite::WreckOnRoof;
    else if (kind == 7) *sprite = CarSprite::BusSide;
    else return false;
    return true;
}

RoadStop roadStopAt(uint64_t seed, const RoadFamily& f, int index, int stretch) {
    RoadStop stop;
    const int stretchLength = stopStretchLength(f);
    TileRandom place(seed, index, stretch, f.salt + kSaltRoadside);
    const int along = stretch * stretchLength + place.range(stretchLength / 5, stretchLength * 4 / 5);
    const int roll = place.range(0, 99);
    const bool highSide = !f.across && place.chance(50);
    if (roll >= 52) return stop;
    const int c = roadCenter(seed, f, index, along);
    const int half = roadHalfWidth(seed, f, index);
    if (roadBurial(seed, f, index, along) > 40) return stop;
    const int margin = f.across ? 160 : 90;
    if (roadCenter(seed, f, index, along - margin) != c || roadCenter(seed, f, index, along + margin) != c) return stop;
    if (f.across) {
        // Always on the far side, so the forecourt faces the road and the camera.
        stop.x0 = along - kStopWidth / 2;
        stop.y0 = c - half - kStopHeight - 1;
    } else {
        stop.x0 = highSide ? c + half + 2 : c - half - kStopWidth - 2;
        stop.y0 = along - kStopHeight / 2;
    }
    // Another road through the lot would look wrong, so the stop moves on.
    const int centerX = stop.x0 + kStopWidth / 2;
    const int centerY = stop.y0 + kStopHeight / 2;
    const RoadFamily& other = f.across ? kUpRoads : kAcrossRoads;
    const int otherPerp = other.across ? centerY : centerX;
    for (int k = firstRoadNear(other, otherPerp - 300); k <= lastRoadNear(other, otherPerp + 300); ++k) {
        const int oc = roadCenter(seed, other, k, other.across ? centerX : centerY);
        const int clear = (other.across ? kStopHeight / 2 : kStopWidth / 2) + roadHalfWidth(seed, other, k) + 24;
        if (std::abs(oc - otherPerp) <= clear) return stop;
    }
    stop.exists = true;
    stop.gas = roll < 40;
    return stop;
}

CarPlan gasStationCar(uint64_t seed, const RoadFamily& f, int index, int stretch, const RoadStop& stop) {
    CarPlan car;
    TileRandom random(seed, index, stretch, f.salt + kSaltGasCar);
    if (!stop.gas || !random.chance(balance::kGasStationCarPercent)) return car;
    car.x = stop.x0 + random.range(30, kStopWidth - 30);
    car.y = stop.y0 + kStopHeight - random.range(6, 16);
    car.looks = random;
    car.exists = clearOfStart(makeObstacle(CarSprite::WreckSide, car.x, car.y));
    return car;
}

namespace {

void collectRoadWrecks(uint64_t seed, const RoadFamily& f, int tileX, int tileY, ObstacleList& list) {
    // How far a road's wreck can stand from the road's center, across it.
    constexpr int kReachAcrossRoad = 170;
    const int texelReachX = static_cast<int>(kObstacleReachWorld * kTexelsPerWorldX) + 1;
    const int texelReachY = static_cast<int>(kObstacleReachWorld * kTexelsPerWorldY) + 1;
    const int left = tileX * kChunkWidth - texelReachX;
    const int right = (tileX + 1) * kChunkWidth + texelReachX;
    const int top = tileY * kChunkHeight - texelReachY;
    const int bottom = (tileY + 1) * kChunkHeight + texelReachY;
    const int perpLow = (f.across ? top : left) - kReachAcrossRoad;
    const int perpHigh = (f.across ? bottom : right) + kReachAcrossRoad;
    const int alongLow = (f.across ? left : top) - kWreckReachAlong;
    const int alongHigh = (f.across ? right : bottom) + kWreckReachAlong;
    const int len = slotLength(f);
    for (int index = firstRoadNear(f, perpLow); index <= lastRoadNear(f, perpHigh); ++index) {
        const int base = roadBase(seed, f, index);
        if (base + f.reach < perpLow || base - f.reach > perpHigh) continue;
        const RoadStyle style = styleOf(seed, f, index);
        for (int slot = floorDiv(alongLow, len); slot <= floorDiv(alongHigh, len); ++slot) {
            TileRandom stream = slotStream(seed, f, index, slot, 1);
            const RoadSlot decided = decideRoadSlot(seed, f, index, style, slot, stream);
            if (decided.wreck) list.add(makeObstacle(decided.sprite, decided.x, decided.y));
        }
    }
}

void collectWreckYards(uint64_t seed, const Desert& d, int tileX, int tileY, ObstacleList& list) {
    const int i0 = floorDiv(tileX * kChunkWidth - kSiteReachX - kCellWidth, kCellWidth);
    const int i1 = floorDiv((tileX + 1) * kChunkWidth + kSiteReachX, kCellWidth);
    const int j0 = floorDiv(tileY * kChunkHeight - kSiteReachY - kCellHeight, kCellHeight);
    const int j1 = floorDiv((tileY + 1) * kChunkHeight + kSiteReachY, kCellHeight);
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const SiteSpot spot = siteSpot(seed, i, j);
            if (siteKindAt(d, spot) != SiteKind::Wrecks) continue;
            TileRandom r(seed, i, j, kSaltPaint);
            Placed items[kMaxPlaced];
            const int count = planWreckYard(spot, r, items);
            for (int n = 0; n < count; ++n) {
                CarSprite sprite;
                if (!wreckYardSprite(items[n].kind, &sprite)) continue;
                const Obstacle obstacle = makeObstacle(sprite, items[n].x, items[n].y);
                if (clearOfStart(obstacle)) list.add(obstacle);
            }
        }
    }
}

void collectGasStationCars(uint64_t seed, const RoadFamily& f, int tileX, int tileY, ObstacleList& list) {
    const int stretchLength = stopStretchLength(f);
    const int reachPerp = (f.across ? kStopHeight : kStopWidth) + 90;
    const int perpLow = (f.across ? tileY * kChunkHeight : tileX * kChunkWidth) - reachPerp;
    const int perpHigh = (f.across ? (tileY + 1) * kChunkHeight : (tileX + 1) * kChunkWidth) + reachPerp;
    const int alongLow = (f.across ? tileX * kChunkWidth : tileY * kChunkHeight) - stretchLength;
    const int alongHigh = (f.across ? (tileX + 1) * kChunkWidth : (tileY + 1) * kChunkHeight) + stretchLength;
    for (int index = firstRoadNear(f, perpLow); index <= lastRoadNear(f, perpHigh); ++index) {
        const int base = roadBase(seed, f, index);
        if (base + f.reach < perpLow || base - f.reach > perpHigh) continue;
        for (int stretch = floorDiv(alongLow, stretchLength); stretch <= floorDiv(alongHigh, stretchLength); ++stretch) {
            const RoadStop stop = roadStopAt(seed, f, index, stretch);
            if (!stop.exists) continue;
            const CarPlan car = gasStationCar(seed, f, index, stretch, stop);
            if (car.exists) list.add(makeObstacle(CarSprite::WreckSide, car.x, car.y));
        }
    }
}

}  // namespace

void collectObstacles(uint64_t seed, int tileX, int tileY, ObstacleList& list) {
    const Desert desert = desertFor(seed);
    collectRoadWrecks(seed, kAcrossRoads, tileX, tileY, list);
    collectRoadWrecks(seed, kUpRoads, tileX, tileY, list);
    collectWreckYards(seed, desert, tileX, tileY, list);
    collectGasStationCars(seed, kAcrossRoads, tileX, tileY, list);
    collectGasStationCars(seed, kUpRoads, tileX, tileY, list);
}

}  // namespace ls::maps::wasteland
