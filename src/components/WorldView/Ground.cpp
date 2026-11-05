#include "components/WorldView/Ground.h"

#include "components/Maps/Paint/Paint.h"
#include "components/WorldView/Draw.h"
#include "components/WorldView/Shaders.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ls::worldview {

// One chunk being painted. It only holds pictures while they are on their way to the GPU.
static Color g_pixels[maps::kChunkWidth * maps::kChunkHeight];

namespace {

// Enough for the widest view, plus a ring being painted ahead of the camera.
constexpr int kPoolSize = 24;

// Painted one chunk's width ahead of what can be seen, so it is ready in time.
constexpr float kPaintAheadWorld = 200.0f;

// How far from the picture a chunk can be and still hold a light that shows in it, in world
// units. A pool reaches about 105 from its center at the player's depth, and more at the top
// of the picture, where one art pixel covers more ground. A bulb stands well above its foot.
// Each light is then checked on its own, so being generous here only costs a comparison.
constexpr float kLightReachWorld = 200.0f;

// How far a flame or bulb sprite reaches around its own point, in art pixels.
constexpr float kLightSpriteSide = 24.0f;
constexpr float kLightSpriteTall = 30.0f;

struct Chunk {
    int x = 0;
    int y = 0;
    uint64_t seed = 0;
    bool ready = false;
    uint32_t lastUsed = 0;
    Texture2D texture{};
    maps::Light lights[maps::kMaxLightsPerChunk];
    int lightCount = 0;
    maps::Obstacle cars[maps::kMaxObstaclesPerTile];
    int carCount = 0;
};

Chunk g_pool[kPoolSize];
Color g_halfPixels[(maps::kChunkWidth / 2) * (maps::kChunkHeight / 2)];
bool g_halfResolution = false;
uint32_t g_frame = 0;
uint64_t g_seed = 0;
int g_paintCount = 0;
int g_paintStages = 0;
int g_paintsInOneGo = 0;
maps::MapId g_map = maps::kDefaultMap;  // The map the pool's chunks were painted for.

// A chunk ahead of the camera isn't needed yet, so it is painted a piece per frame: the bare
// ground in a few bands of rows, then the details and the upload. On a slow phone a whole
// chunk takes long enough to drop a frame on every border crossed, and the pieces don't.
// There is only ever one of these, and g_pixels holds its picture so far.
constexpr int kGroundBands = 3;
constexpr int kBandRows = maps::kChunkHeight / kGroundBands + 1;

struct PaintJob {
    bool active = false;
    int x = 0;
    int y = 0;
    int bandsDone = 0;
};

PaintJob g_job;

struct ChunkRange {
    int x0, y0, x1, y1;  // Inclusive.
};

int chunkOf(float world) { return static_cast<int>(std::floor(world / maps::kChunkWorldSize)); }

// Every chunk the camera can see, from the four corners of the target. The top edge is
// the widest in world units, because of the perspective.
ChunkRange visibleChunks(const View& view, float margin) {
    const float w = static_cast<float>(view.width);
    const float h = static_cast<float>(view.height);
    const Vec2 topLeft = unproject(view, -2.0f, -2.0f);
    const Vec2 topRight = unproject(view, w + 2.0f, -2.0f);
    const Vec2 bottomLeft = unproject(view, -2.0f, h + 2.0f);
    const Vec2 bottomRight = unproject(view, w + 2.0f, h + 2.0f);
    const float left = std::min(topLeft.x, bottomLeft.x) - margin;
    const float right = std::max(topRight.x, bottomRight.x) + margin;
    return {chunkOf(left), chunkOf(topLeft.y - margin), chunkOf(right), chunkOf(bottomLeft.y + margin)};
}

Chunk* findChunk(int x, int y, uint64_t seed) {
    for (Chunk& chunk : g_pool) {
        if (chunk.ready && chunk.x == x && chunk.y == y && chunk.seed == seed) return &chunk;
    }
    return nullptr;
}

void loadPoolTextures() {
    const int width = g_halfResolution ? maps::kChunkWidth / 2 : maps::kChunkWidth;
    const int height = g_halfResolution ? maps::kChunkHeight / 2 : maps::kChunkHeight;
    for (Chunk& chunk : g_pool) {
        if (chunk.texture.id != 0) rlUnloadTexture(chunk.texture.id);
        chunk.ready = false;
        chunk.texture.id = rlLoadTexture(nullptr, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        chunk.texture.width = width;
        chunk.texture.height = height;
        chunk.texture.mipmaps = 1;
        chunk.texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        // Nearest filtering keeps the ground's pixels square, matching the sprites.
        SetTextureFilter(chunk.texture, TEXTURE_FILTER_POINT);
        SetTextureWrap(chunk.texture, TEXTURE_WRAP_CLAMP);
    }
}

// Fills in everything about a chunk except its lights, and sends g_pixels to its texture.
void storeChunk(Chunk& chunk, int x, int y, uint64_t seed) {
    g_paintCount++;
    chunk.carCount = maps::obstaclesInTile(g_map, seed, x, y, chunk.cars, maps::kMaxObstaclesPerTile);
    chunk.x = x;
    chunk.y = y;
    chunk.seed = seed;
    chunk.ready = true;
    chunk.lastUsed = g_frame;
    if (g_halfResolution) {
        for (int py = 0; py < maps::kChunkHeight / 2; ++py) {
            for (int px = 0; px < maps::kChunkWidth / 2; ++px) {
                g_halfPixels[py * (maps::kChunkWidth / 2) + px] = g_pixels[py * 2 * maps::kChunkWidth + px * 2];
            }
        }
        UpdateTexture(chunk.texture, g_halfPixels);
    } else {
        UpdateTexture(chunk.texture, g_pixels);
    }
}

// Anything on screen must be there now, so this one is painted whole. It needs g_pixels,
// which ends whatever job was using them.
void paintWhole(Chunk& chunk, int x, int y, uint64_t seed) {
    g_job.active = false;
    g_paintStages++;
    g_paintsInOneGo++;
    chunk.lightCount = maps::renderChunk(g_map, seed, x, y, g_pixels, chunk.lights);
    storeChunk(chunk, x, y, seed);
}

// A slot keeps its old chunk until something needs the room, so after a new run starts it can
// still hold lights from the old seed. The seed check keeps those ghosts out of the picture.
bool lightsCount(const Chunk& chunk, const ChunkRange& range) {
    return chunk.ready && chunk.seed == g_seed && chunk.x >= range.x0 && chunk.x <= range.x1 && chunk.y >= range.y0 && chunk.y <= range.y1;
}

// The chunk that was used longest ago, skipping any that are on screen right now.
Chunk& evictionTarget(const ChunkRange& visible, uint64_t seed) {
    Chunk* best = &g_pool[0];
    bool bestIsFree = false;
    for (Chunk& chunk : g_pool) {
        if (!chunk.ready || chunk.seed != seed) return chunk;
        const bool onScreen = chunk.x >= visible.x0 && chunk.x <= visible.x1 && chunk.y >= visible.y0 && chunk.y <= visible.y1;
        if (onScreen) continue;
        if (!bestIsFree || chunk.lastUsed < best->lastUsed) {
            best = &chunk;
            bestIsFree = true;
        }
    }
    return *best;
}

// Does the next piece of the job. The slot is only chosen for the last piece, so a slot
// keeps showing its old chunk for as long as it can.
void stepPaintJob(const ChunkRange& visible, uint64_t seed) {
    g_paintStages++;
    if (g_job.bandsDone < kGroundBands) {
        const int rowFrom = g_job.bandsDone * kBandRows;
        const int rowTo = std::min(rowFrom + kBandRows, static_cast<int>(maps::kChunkHeight));
        maps::paintGroundRows(g_map, seed, g_job.x, g_job.y, g_pixels, rowFrom, rowTo);
        g_job.bandsDone++;
        return;
    }
    Chunk& chunk = evictionTarget(visible, seed);
    chunk.lightCount = maps::paintDetails(g_map, seed, g_job.x, g_job.y, g_pixels, chunk.lights);
    storeChunk(chunk, g_job.x, g_job.y, seed);
    g_job.active = false;
}

}  // namespace

void initGround() { loadPoolTextures(); }

int groundPaintCount() { return g_paintCount; }

// For the timing hook below. Marks the slot as empty, so the real picture gets painted again.
void uploadSpareForTiming() {
    Chunk& spare = g_pool[kPoolSize - 1];
    spare.ready = false;
    spare.lightCount = 0;
    UpdateTexture(spare.texture, g_pixels);
}

void setGroundHalfResolution(bool half) {
    if (half == g_halfResolution) return;
    g_halfResolution = half;
    if (g_pool[0].texture.id != 0) loadPoolTextures();
}

void updateGround(const View& view, uint64_t seed, maps::MapId map) {
    g_frame++;
    // A new seed makes the picture so far wrong.
    if (seed != g_seed) g_job.active = false;
    g_seed = seed;
    // A new map makes every painted chunk wrong, along with its lights. Only the flags
    // change here: the textures stay, and get painted over.
    if (map != g_map) {
        g_job.active = false;
        g_map = map;
        for (Chunk& chunk : g_pool) {
            chunk.ready = false;
            chunk.lightCount = 0;
        }
    }
    const ChunkRange visible = visibleChunks(view, 0.0f);
    const ChunkRange ahead = visibleChunks(view, kPaintAheadWorld);

    // Anything on screen must be there now. This only paints several at once on the
    // first frame, after a jump, or when a run starts with a new seed.
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            if (Chunk* chunk = findChunk(x, y, seed)) {
                chunk->lastUsed = g_frame;
            } else {
                paintWhole(evictionTarget(visible, seed), x, y, seed);
            }
        }
    }

    // One piece of one chunk per frame ahead of the camera, so walking never causes a hitch.
    bool anyMissing = false;
    bool jobStillNeeded = false;
    int firstMissingX = 0;
    int firstMissingY = 0;
    for (int y = ahead.y0; y <= ahead.y1; ++y) {
        for (int x = ahead.x0; x <= ahead.x1; ++x) {
            if (x >= visible.x0 && x <= visible.x1 && y >= visible.y0 && y <= visible.y1) continue;
            if (Chunk* chunk = findChunk(x, y, seed)) {
                chunk->lastUsed = g_frame;
                continue;
            }
            if (!anyMissing) {
                anyMissing = true;
                firstMissingX = x;
                firstMissingY = y;
            }
            if (g_job.active && g_job.x == x && g_job.y == y) jobStillNeeded = true;
        }
    }
    // Walking away from a chunk being painted drops it, as something nearer needs the time.
    if (!jobStillNeeded) g_job.active = false;
    if (!g_job.active) {
        if (!anyMissing) return;
        g_job = PaintJob{true, firstMissingX, firstMissingY, 0};
    }
    stepPaintJob(visible, seed);
}

namespace {

// project() written as a matrix. The fourth row is the depth, which makes the GPU
// do the same divide per pixel. The target's y runs down from its top edge.
Matrix groundMatrix(const View& view) {
    const float w = static_cast<float>(view.width);
    const float h = static_cast<float>(view.height);
    const float ax = view.anchorX;
    const float ay = view.anchorY;
    const float k = kArtPerWorld;
    const float ky = kArtPerWorld * kTiltSquash;

    Matrix matrix{};
    const float centerX = 2.0f * ax / w - 1.0f;
    const float centerY = 1.0f - 2.0f * ay / h;
    matrix.m0 = 2.0f * k / w;
    matrix.m4 = -kPerspective * centerX;
    matrix.m12 = centerX;
    matrix.m5 = -2.0f * ky / h - kPerspective * centerY;
    matrix.m13 = centerY;
    matrix.m10 = 1.0f;
    matrix.m7 = -kPerspective;
    matrix.m15 = 1.0f;
    return matrix;
}

}  // namespace

void drawGround(const View& view, const DayLook& look) {
    const GroundShader& ground = groundShader();
    const float saturation = look.groundSaturation;
    SetShaderValueMatrix(ground.shader, ground.projection, groundMatrix(view));
    SetShaderValue(ground.shader, ground.tint, look.groundTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(ground.shader, ground.saturation, &saturation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(ground.shader, ground.lift, look.groundLift, SHADER_UNIFORM_VEC3);

    BeginShaderMode(ground.shader);
    const ChunkRange visible = visibleChunks(view, 0.0f);
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            const Chunk* chunk = findChunk(x, y, g_seed);
            if (!chunk) continue;
            const float left = static_cast<float>(x) * maps::kChunkWorldSize - view.focusX;
            const float top = static_cast<float>(y) * maps::kChunkWorldSize - view.focusY;
            const float right = left + maps::kChunkWorldSize;
            const float bottom = top + maps::kChunkWorldSize;
            rlSetTexture(chunk->texture.id);
            rlBegin(RL_QUADS);
            rlColor4ub(255, 255, 255, 255);
            rlTexCoord2f(0.0f, 0.0f);
            rlVertex2f(left, top);
            rlTexCoord2f(0.0f, 1.0f);
            rlVertex2f(left, bottom);
            rlTexCoord2f(1.0f, 1.0f);
            rlVertex2f(right, bottom);
            rlTexCoord2f(1.0f, 0.0f);
            rlVertex2f(right, top);
            rlEnd();
        }
    }
    rlSetTexture(0);
    EndShaderMode();
}

namespace {

// A car's near edge on the ground, where it meets the row of whoever stands in front of it.
float carFeetRow(const View& view, const maps::Obstacle& car) {
    return project(view, Vec2{car.center.x, car.center.y + car.half.y}).y;
}

}  // namespace

void drawCarDepth(const View& view) {
    const CarDepthShader& depth = carDepthShader();
    SetShaderValueMatrix(depth.shader, depth.projection, groundMatrix(view));
    BeginShaderMode(depth.shader);
    rlEnableDepthTest();
    rlColorMask(false, false, false, false);
    const ChunkRange visible = visibleChunks(view, 0.0f);
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            const Chunk* chunk = findChunk(x, y, g_seed);
            if (!chunk || chunk->carCount == 0) continue;
            const int chunkLeft = x * maps::kChunkWidth;
            const int chunkTop = y * maps::kChunkHeight;
            rlSetTexture(chunk->texture.id);
            rlBegin(RL_QUADS);
            for (int i = 0; i < chunk->carCount; ++i) {
                const maps::Obstacle& car = chunk->cars[i];
                // A car over a chunk edge is in both lists, and each draws its own part.
                const maps::TexelRect rect = maps::spriteRect(car);
                const int x0 = std::max(rect.x0, chunkLeft);
                const int y0 = std::max(rect.y0, chunkTop);
                const int x1 = std::min(rect.x1, chunkLeft + maps::kChunkWidth);
                const int y1 = std::min(rect.y1, chunkTop + maps::kChunkHeight);
                if (x0 >= x1 || y0 >= y1) continue;
                const float left = static_cast<float>(x0) / maps::kTexelsPerWorldX - view.focusX;
                const float right = static_cast<float>(x1) / maps::kTexelsPerWorldX - view.focusX;
                const float top = static_cast<float>(y0) / maps::kTexelsPerWorldY - view.focusY;
                const float bottom = static_cast<float>(y1) / maps::kTexelsPerWorldY - view.focusY;
                const float u0 = static_cast<float>(x0 - chunkLeft) / static_cast<float>(maps::kChunkWidth);
                const float u1 = static_cast<float>(x1 - chunkLeft) / static_cast<float>(maps::kChunkWidth);
                const float v0 = static_cast<float>(y0 - chunkTop) / static_cast<float>(maps::kChunkHeight);
                const float v1 = static_cast<float>(y1 - chunkTop) / static_cast<float>(maps::kChunkHeight);
                const float z = depthOfRow(view, carFeetRow(view, car));
                rlColor4ub(255, 255, 255, 255);
                rlTexCoord2f(u0, v0);
                rlVertex3f(left, top, z);
                rlTexCoord2f(u0, v1);
                rlVertex3f(left, bottom, z);
                rlTexCoord2f(u1, v1);
                rlVertex3f(right, bottom, z);
                rlTexCoord2f(u1, v0);
                rlVertex3f(right, top, z);
            }
            rlEnd();
        }
    }
    rlSetTexture(0);
    // Flushes what was queued while the depth test is still on, before it changes back.
    EndShaderMode();
    rlColorMask(true, true, true, true);
    rlDisableDepthTest();
}

int carsOnScreen(const View& view, CarOnScreen* cars) {
    int count = 0;
    const ChunkRange visible = visibleChunks(view, 0.0f);
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            const Chunk* chunk = findChunk(x, y, g_seed);
            if (!chunk) continue;
            for (int i = 0; i < chunk->carCount && count < kMaxCarsOnScreen; ++i) {
                const maps::Obstacle& car = chunk->cars[i];
                const maps::TexelRect rect = maps::spriteRect(car);
                const Vec2 topLeft{static_cast<float>(rect.x0) / maps::kTexelsPerWorldX, static_cast<float>(rect.y0) / maps::kTexelsPerWorldY};
                const Vec2 bottomRight{static_cast<float>(rect.x1) / maps::kTexelsPerWorldX, static_cast<float>(rect.y1) / maps::kTexelsPerWorldY};
                const ScreenPoint a = project(view, topLeft);
                const ScreenPoint b = project(view, Vec2{bottomRight.x, topLeft.y});
                const ScreenPoint c = project(view, Vec2{topLeft.x, bottomRight.y});
                const ScreenPoint d = project(view, bottomRight);
                CarOnScreen shown;
                shown.left = std::min(a.x, c.x);
                shown.right = std::max(b.x, d.x);
                shown.top = a.y;
                shown.bottom = c.y;
                if (shown.right < 0.0f || shown.left > static_cast<float>(view.width) || shown.bottom < 0.0f || shown.top > static_cast<float>(view.height)) continue;
                shown.feetRow = carFeetRow(view, car);
                shown.depth = depthOfRow(view, shown.feetRow);
                shown.boxMin = car.center - car.half;
                shown.boxMax = car.center + car.half;
                cars[count++] = shown;
            }
        }
    }
    return count;
}

void drawLightPools(const View& view, const DayLook& look, uint32_t tick, bool flicker) {
    if (look.lampLevel <= 0.01f) return;
    const ChunkRange visible = visibleChunks(view, kLightReachWorld);
    const SpriteRef& pool = itemSprite(Item::LightPool);
    for (const Chunk& chunk : g_pool) {
        if (!lightsCount(chunk, visible)) continue;
        for (int i = 0; i < chunk.lightCount; ++i) {
            const maps::Light& light = chunk.lights[i];
            const bool fire = light.kind == maps::LightKind::Fire;
            const uint32_t seed = hashPosition(light.world);
            // Dusk only warms up slowly, so lights still look weak until it is really dark.
            // Bulbs buzz now and then, and fires never stop moving.
            float level = look.lampLevel * look.lampLevel;
            if (flicker) {
                if (fire) level *= 0.82f + 0.18f * triangleWave(tick + seed, maps::fireFlickerTicks(g_map) + (seed & 3u));
                else if (((tick / 8u + seed) & 127u) < 3u) level *= 0.55f;
            }
            const ScreenPoint point = project(view, light.world);
            // Lamps are wide, and fires warm and small.
            const float size = point.scale * (fire ? 0.8f : 1.0f);
            // The pool grows with depth, so its margin must too, or it pops at the edges.
            const float halfWide = static_cast<float>(pool.w) * 0.5f * size;
            const float halfTall = static_cast<float>(pool.h) * 0.5f * size;
            if (!onTarget(view, point, halfWide, halfTall, halfTall)) continue;
            const uint8_t row = fire ? kRowFireLight : kRowLampLight;
            poolQuad(pool, point.x, point.y, size, row, level);
            // Multiplying can only double a color, and asphalt is dark, so the bright
            // middle of the pool takes a second, smaller layer.
            poolQuad(pool, point.x, point.y, size * 0.6f, row, level);
        }
    }
}

void drawLightSprites(const View& view, const DayLook& look, uint32_t tick, bool flicker) {
    const ChunkRange visible = visibleChunks(view, kLightReachWorld);
    constexpr Item kFireFrames[4] = {Item::FireA, Item::FireB, Item::FireC, Item::FireD};
    for (const Chunk& chunk : g_pool) {
        if (!lightsCount(chunk, visible)) continue;
        for (int i = 0; i < chunk.lightCount; ++i) {
            const maps::Light& light = chunk.lights[i];
            const ScreenPoint point = project(view, light.world);
            // A flame or bulb stands above its foot, so the foot may be well below the picture.
            const float above = light.height * point.scale + kLightSpriteTall;
            if (!onTarget(view, point, kLightSpriteSide, above, 8.0f)) continue;
            const uint32_t seed = hashPosition(light.world);
            if (light.kind == maps::LightKind::Fire) {
                // Fires burn all day, so these show whatever the time.
                const uint32_t frame = flicker ? (tick / maps::fireFrameTicks(g_map) + seed) & 3u : seed & 3u;
                const SpriteRef& flame = itemSprite(kFireFrames[frame]);
                const int y = roundToInt(point.y - light.height * point.scale);
                spriteQuad(flame, roundToInt(point.x), y, ((seed >> 3) & 1u) != 0u, kRowBase, 0);
            } else if (look.lampLevel > 0.05f) {
                // The bulb comes on with the dusk. Its pole is painted into the ground.
                const auto strength = static_cast<uint8_t>(255.0f * std::min(1.0f, look.lampLevel * 1.5f));
                spriteQuad(itemSprite(Item::LampBulb), roundToInt(point.x), roundToInt(point.y - light.height * point.scale), false, kRowBase, 0, strength);
            }
        }
    }
}

}  // namespace ls::worldview

#ifdef __EMSCRIPTEN__
extern "C" {
// Test hook: paints the chunks around the start and compares them with the simulation's list
// of cars. Every pixel marked as car must be inside some car's picture, and every car wholly
// inside a chunk must have a good amount of marked pixels. This is what proves the painters and
// the layout can't disagree. It repaints, so it is slow, and nothing but the tests calls it.
EMSCRIPTEN_KEEPALIVE const char* ls_check_car_marks(int map, int seed, int reach) {
    using namespace ls;
    static char json[96];
    const maps::MapId id = maps::mapFromIndex(map);
    int cars = 0;
    int strayPixels = 0;
    int fewestMarked = 1 << 30;
    maps::Light lights[maps::kMaxLightsPerChunk];
    worldview::g_job.active = false;  // This paints over the job's picture.
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            maps::renderChunk(id, static_cast<uint64_t>(seed), x, y, worldview::g_pixels, lights);
            maps::Obstacle list[maps::kMaxObstaclesPerTile];
            const int count = maps::obstaclesInTile(id, static_cast<uint64_t>(seed), x, y, list, maps::kMaxObstaclesPerTile);
            const int left = x * maps::kChunkWidth;
            const int top = y * maps::kChunkHeight;
            for (int py = 0; py < maps::kChunkHeight; ++py) {
                for (int px = 0; px < maps::kChunkWidth; ++px) {
                    if (worldview::g_pixels[py * maps::kChunkWidth + px].a != maps::kCarBody) continue;
                    bool inside = false;
                    for (int i = 0; i < count && !inside; ++i) {
                        const maps::TexelRect rect = maps::spriteRect(list[i]);
                        // Sand heaped against a wreck and its pebbles stray a little outside.
                        inside = left + px >= rect.x0 - 24 && left + px < rect.x1 + 24 && top + py >= rect.y0 - 6 && top + py < rect.y1 + 8;
                    }
                    if (!inside) strayPixels++;
                }
            }
            for (int i = 0; i < count; ++i) {
                const maps::TexelRect rect = maps::spriteRect(list[i]);
                if (rect.x0 < left || rect.y0 < top || rect.x1 > left + maps::kChunkWidth || rect.y1 > top + maps::kChunkHeight) continue;
                int marked = 0;
                for (int py = rect.y0; py < rect.y1; ++py) {
                    for (int px = rect.x0; px < rect.x1; ++px) {
                        if (worldview::g_pixels[(py - top) * maps::kChunkWidth + (px - left)].a == maps::kCarBody) marked++;
                    }
                }
                fewestMarked = std::min(fewestMarked, marked);
                cars++;
            }
        }
    }
    std::snprintf(json, sizeof(json), "{\"cars\":%d,\"strayPixels\":%d,\"fewestMarked\":%d}", cars, strayPixels, cars > 0 ? fewestMarked : 0);
    return json;
}

// Test hook: pieces of painting done so far, as a plain number, cheap enough to read every frame.
EMSCRIPTEN_KEEPALIVE int ls_ground_paints() { return ls::worldview::g_paintStages; }

// Test hook: chunks that couldn't wait, and were painted whole in one frame.
EMSCRIPTEN_KEEPALIVE int ls_ground_whole_paints() { return ls::worldview::g_paintsInOneGo; }

// Test hook: paints chunks both ways, whole and in the job's pieces, and counts the pixels
// that differ. It must be zero, or walking would show a different map than jumping does.
EMSCRIPTEN_KEEPALIVE const char* ls_check_staged_paint(int map, int seed, int reach) {
    using namespace ls;
    static char json[64];
    static Color whole[maps::kChunkWidth * maps::kChunkHeight];
    const maps::MapId id = maps::mapFromIndex(map);
    const uint64_t wide = static_cast<uint64_t>(seed);
    worldview::g_job.active = false;
    int chunks = 0;
    int differentPixels = 0;
    int differentLights = 0;
    maps::Light wholeLights[maps::kMaxLightsPerChunk];
    maps::Light stagedLights[maps::kMaxLightsPerChunk];
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            // The buffer starts dirty on purpose, as it does in play.
            const int wholeCount = maps::renderChunk(id, wide, x, y, whole, wholeLights);
            for (int band = 0; band < worldview::kGroundBands; ++band) {
                const int rowFrom = band * worldview::kBandRows;
                maps::paintGroundRows(id, wide, x, y, worldview::g_pixels, rowFrom, std::min(rowFrom + worldview::kBandRows, static_cast<int>(maps::kChunkHeight)));
            }
            const int stagedCount = maps::paintDetails(id, wide, x, y, worldview::g_pixels, stagedLights);
            for (int i = 0; i < maps::kChunkWidth * maps::kChunkHeight; ++i) {
                const Color& a = whole[i];
                const Color& b = worldview::g_pixels[i];
                if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) differentPixels++;
            }
            if (wholeCount != stagedCount) differentLights++;
            chunks++;
        }
    }
    std::snprintf(json, sizeof(json), "{\"chunks\":%d,\"pixels\":%d,\"lights\":%d}", chunks, differentPixels, differentLights);
    return json;
}

// Test hook: times the painting of a square of chunks, one at a time, so the cost of walking
// over a chunk border can be measured on its own. Each picture also goes up to a spare chunk.
EMSCRIPTEN_KEEPALIVE const char* ls_time_chunks(int map, int seed, int reach) {
    using namespace ls;
    static char json[192];
    const maps::MapId id = maps::mapFromIndex(map);
    worldview::g_job.active = false;  // This paints over the job's picture.
    double groundTotal = 0.0, groundMost = 0.0, detailsTotal = 0.0, detailsMost = 0.0, uploadTotal = 0.0, uploadMost = 0.0;
    int count = 0;
    maps::Light lights[maps::kMaxLightsPerChunk];
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            const double start = GetTime();
            maps::paintGroundRows(id, static_cast<uint64_t>(seed), x, y, worldview::g_pixels, 0, maps::kChunkHeight);
            const double grounded = GetTime();
            maps::paintDetails(id, static_cast<uint64_t>(seed), x, y, worldview::g_pixels, lights);
            const double painted = GetTime();
            worldview::uploadSpareForTiming();
            const double uploaded = GetTime();
            groundTotal += grounded - start;
            groundMost = std::max(groundMost, grounded - start);
            detailsTotal += painted - grounded;
            detailsMost = std::max(detailsMost, painted - grounded);
            uploadTotal += uploaded - painted;
            uploadMost = std::max(uploadMost, uploaded - painted);
            count++;
        }
    }
    std::snprintf(json, sizeof(json), "{\"chunks\":%d,\"groundAvg\":%.3f,\"groundMost\":%.3f,\"detailsAvg\":%.3f,\"detailsMost\":%.3f,\"uploadAvg\":%.3f,\"uploadMost\":%.3f}", count, groundTotal * 1000.0 / count,
                  groundMost * 1000.0, detailsTotal * 1000.0 / count, detailsMost * 1000.0, uploadTotal * 1000.0 / count, uploadMost * 1000.0);
    return json;
}
}
#endif
