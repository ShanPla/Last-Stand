#include "components/Maps/Wasteland/Wasteland.h"

#include "components/Maps/Paint/Paint.h"
#include "components/Maps/SharedProps/SharedProps.h"
#include "components/Maps/Wasteland/Roads/Roads.h"
#include "components/Maps/Wasteland/Sites/Sites.h"
#include "components/Maps/Wasteland/Terrain/Terrain.h"

namespace ls::maps::wasteland {

void paintGroundRows(uint64_t seed, int chunkX, int chunkY, Color* pixels, int rowFrom, int rowTo) {
    Paint paint(pixels, chunkX * kChunkWidth, chunkY * kChunkHeight, kChunkWidth, kChunkHeight);
    paintTerrainRows(paint, desertFor(seed), rowFrom, rowTo);
}

int paintDetails(uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* outLights, CrushedCars crushed, CoverList* covers) {
    Paint paint(pixels, chunkX * kChunkWidth, chunkY * kChunkHeight, kChunkWidth, kChunkHeight);
    paint.setCrushed(crushed);
    paint.setCovers(covers);
    LightList lights{outLights, 0, &paint};
    const Desert desert = desertFor(seed);

    // Each layer only covers the one before it where it has something to say, so where
    // sand has blown over a road the real desert still shows through.
    paintTerrainMarks(paint, desert);
    paintRoads(paint, desert, lights);
    paintSites(paint, desert, lights);
    return lights.count;
}

}  // namespace ls::maps::wasteland
