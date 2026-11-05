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
#include <utility>

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
    maps::Cover covers[maps::kMaxCoversPerChunk];  // Its standing objects, listed while painting.
    int coverCount = 0;
    int flatCars = 0;    // How many cars of its tile were painted flat.
    bool stale = false;  // On screen, and its picture still shows a car that has gone flat.
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
    bool repaint = false;  // For a chunk on screen whose picture has gone out of date.
};

PaintJob g_job;

// A chunk on screen can't change in one go without a hitch, so its new picture is painted a
// piece per frame like any other, and goes up to one of these spare textures. Only when every
// chunk showing the crushed car is ready do they all swap at once, so a car over a chunk's
// edge never shows half flat. A car's picture can touch at most four chunks.
constexpr int kSpareCount = 4;

struct SparePicture {
    Texture2D texture{};
    bool ready = false;
    int x = 0;
    int y = 0;
    maps::Light lights[maps::kMaxLightsPerChunk];
    int lightCount = 0;
    maps::Cover covers[maps::kMaxCoversPerChunk];
    int coverCount = 0;
};

SparePicture g_spares[kSpareCount];
maps::CrushedCars g_crushed;  // What the simulation said this frame.
int g_crushedSeen = 0;        // How many of those the chunks already know about.
int g_lastSwapSize = 0;       // For the tests: how many chunks changed picture in one go, last time.
int g_coversDropped = 0;      // For the tests: standing objects left off a full list on screen. Must stay 0.
double g_slowestUpdate = 0.0;  // For the timing test: the longest updateGround, in seconds.

// Times updateGround, however it returns.
struct UpdateTimer {
    double started = GetTime();
    ~UpdateTimer() { g_slowestUpdate = std::max(g_slowestUpdate, GetTime() - started); }
};

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

void loadTexture(Texture2D& texture, int width, int height) {
    if (texture.id != 0) rlUnloadTexture(texture.id);
    texture.id = rlLoadTexture(nullptr, width, height, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    texture.width = width;
    texture.height = height;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    // Nearest filtering keeps the ground's pixels square, matching the sprites.
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
}

void loadPoolTextures() {
    const int width = g_halfResolution ? maps::kChunkWidth / 2 : maps::kChunkWidth;
    const int height = g_halfResolution ? maps::kChunkHeight / 2 : maps::kChunkHeight;
    for (Chunk& chunk : g_pool) {
        chunk.ready = false;
        loadTexture(chunk.texture, width, height);
    }
    for (SparePicture& spare : g_spares) {
        spare.ready = false;
        loadTexture(spare.texture, width, height);
    }
}

// Sends g_pixels to a texture.
void uploadPixels(Texture2D& texture) {
    if (g_halfResolution) {
        for (int py = 0; py < maps::kChunkHeight / 2; ++py) {
            for (int px = 0; px < maps::kChunkWidth / 2; ++px) {
                g_halfPixels[py * (maps::kChunkWidth / 2) + px] = g_pixels[py * 2 * maps::kChunkWidth + px * 2];
            }
        }
        UpdateTexture(texture, g_halfPixels);
    } else {
        UpdateTexture(texture, g_pixels);
    }
}

// Fills in everything about a chunk except its lights, and sends g_pixels to its texture.
void storeChunk(Chunk& chunk, int x, int y, uint64_t seed) {
    g_paintCount++;
    // Its list is for the depth pass and the see-through figures, so a flat car stays out.
    const int listed = maps::obstaclesInTile(g_map, seed, x, y, chunk.cars, maps::kMaxObstaclesPerTile);
    chunk.carCount = 0;
    for (int i = 0; i < listed; ++i) {
        if (!g_crushed.holds(chunk.cars[i].texelX, chunk.cars[i].texelY)) chunk.cars[chunk.carCount++] = chunk.cars[i];
    }
    chunk.flatCars = listed - chunk.carCount;
    chunk.stale = false;
    chunk.x = x;
    chunk.y = y;
    chunk.seed = seed;
    chunk.ready = true;
    chunk.lastUsed = g_frame;
    uploadPixels(chunk.texture);
    // This picture is the newest, so one waiting for the same chunk must not replace it.
    for (SparePicture& spare : g_spares) {
        if (spare.x == x && spare.y == y) spare.ready = false;
    }
}

// Anything on screen must be there now, so this one is painted whole. It needs g_pixels,
// which ends whatever job was using them.
void paintWhole(Chunk& chunk, int x, int y, uint64_t seed) {
    g_job.active = false;
    g_paintStages++;
    g_paintsInOneGo++;
    maps::CoverList covers{chunk.covers, 0};
    chunk.lightCount = maps::renderChunk(g_map, seed, x, y, g_pixels, chunk.lights, g_crushed, &covers);
    chunk.coverCount = covers.count;
    storeChunk(chunk, x, y, seed);
}

// Tells the chunks about cars that have gone flat since last frame. The depth pass and the
// see-through figures drop the car at once, since those cost nothing. A chunk on screen is
// marked to be painted again, and one out of sight is just forgotten until it is needed.
void noteNewlyCrushed(const ChunkRange& visible, uint64_t seed) {
    // A shorter list means a new run with the same seed. Flat pictures would now be wrong.
    if (g_crushed.count < g_crushedSeen) {
        for (Chunk& chunk : g_pool) {
            if (chunk.flatCars > 0) chunk.ready = false;
        }
        for (SparePicture& spare : g_spares) spare.ready = false;
        g_crushedSeen = 0;
    }
    for (; g_crushedSeen < g_crushed.count; ++g_crushedSeen) {
        const maps::CarId car = g_crushed.items[g_crushedSeen];
        for (Chunk& chunk : g_pool) {
            if (!chunk.ready || chunk.seed != seed) continue;
            int kept = 0;
            bool touches = false;
            for (int i = 0; i < chunk.carCount; ++i) {
                if (!maps::isCar(car, chunk.cars[i])) {
                    chunk.cars[kept++] = chunk.cars[i];
                    continue;
                }
                // A tile's list reaches past its edges, so it can name a car with no pixels here.
                const maps::TexelRect reach = maps::flatCarReach(chunk.cars[i]);
                touches = reach.x1 > chunk.x * maps::kChunkWidth && reach.x0 < (chunk.x + 1) * maps::kChunkWidth && reach.y1 > chunk.y * maps::kChunkHeight && reach.y0 < (chunk.y + 1) * maps::kChunkHeight;
            }
            if (kept == chunk.carCount) continue;
            chunk.carCount = kept;
            chunk.flatCars++;
            if (!touches) continue;
            const bool onScreen = chunk.x >= visible.x0 && chunk.x <= visible.x1 && chunk.y >= visible.y0 && chunk.y <= visible.y1;
            if (onScreen) chunk.stale = true;
            else chunk.ready = false;
        }
    }
}

void swapInSpares(uint64_t seed) {
    int swapped = 0;
    for (SparePicture& spare : g_spares) {
        if (!spare.ready) continue;
        spare.ready = false;
        Chunk* chunk = findChunk(spare.x, spare.y, seed);
        if (!chunk) continue;
        std::swap(chunk->texture, spare.texture);
        chunk->lightCount = spare.lightCount;
        std::copy(spare.lights, spare.lights + spare.lightCount, chunk->lights);
        chunk->coverCount = spare.coverCount;
        std::copy(spare.covers, spare.covers + spare.coverCount, chunk->covers);
        swapped++;
    }
    if (swapped > 0) g_lastSwapSize = swapped;
}

// The spare already holding this chunk, or a free one. If all are in use the ones ready now
// go up first, which only happens when the boss flattens cars on many chunks in a few frames.
SparePicture& spareFor(int x, int y, uint64_t seed) {
    for (SparePicture& spare : g_spares) {
        if (spare.ready && spare.x == x && spare.y == y) return spare;
    }
    for (SparePicture& spare : g_spares) {
        if (!spare.ready) return spare;
    }
    swapInSpares(seed);
    return g_spares[0];
}

// One piece of painting a chunk that is on screen and out of date. Returns true when it used
// up this frame's painting. These come before the chunks ahead of the camera, because the
// dust of a crush only hides the change for a moment.
bool stepRepaint(uint64_t seed) {
    Chunk* stale = nullptr;
    for (Chunk& chunk : g_pool) {
        if (chunk.ready && chunk.stale && chunk.seed == seed) {
            stale = &chunk;
            break;
        }
    }
    if (!stale) {
        swapInSpares(seed);
        return false;
    }
    if (!g_job.active || !g_job.repaint || g_job.x != stale->x || g_job.y != stale->y) g_job = PaintJob{true, stale->x, stale->y, 0, true};
    g_paintStages++;
    if (g_job.bandsDone < kGroundBands) {
        const int rowFrom = g_job.bandsDone * kBandRows;
        const int rowTo = std::min(rowFrom + kBandRows, static_cast<int>(maps::kChunkHeight));
        maps::paintGroundRows(g_map, seed, g_job.x, g_job.y, g_pixels, rowFrom, rowTo);
        g_job.bandsDone++;
        return true;
    }
    SparePicture& spare = spareFor(g_job.x, g_job.y, seed);
    maps::CoverList covers{spare.covers, 0};
    spare.lightCount = maps::paintDetails(g_map, seed, g_job.x, g_job.y, g_pixels, spare.lights, g_crushed, &covers);
    spare.coverCount = covers.count;
    uploadPixels(spare.texture);
    spare.x = g_job.x;
    spare.y = g_job.y;
    spare.ready = true;
    stale->stale = false;
    g_paintCount++;
    g_job.active = false;
    return true;
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
    maps::CoverList covers{chunk.covers, 0};
    chunk.lightCount = maps::paintDetails(g_map, seed, g_job.x, g_job.y, g_pixels, chunk.lights, g_crushed, &covers);
    chunk.coverCount = covers.count;
    storeChunk(chunk, g_job.x, g_job.y, seed);
    g_job.active = false;
}

}  // namespace

void initGround() { loadPoolTextures(); }

int groundPaintCount() { return g_paintCount; }

int groundLastSwapSize() { return g_lastSwapSize; }

// For the timing hook below. Marks the slot as empty, so the real picture gets painted again.
void uploadSpareForTiming() {
    Chunk& spare = g_pool[kPoolSize - 1];
    spare.ready = false;
    spare.lightCount = 0;
    spare.coverCount = 0;
    UpdateTexture(spare.texture, g_pixels);
}

void setGroundHalfResolution(bool half) {
    if (half == g_halfResolution) return;
    g_halfResolution = half;
    if (g_pool[0].texture.id != 0) loadPoolTextures();
}

void updateGround(const View& view, uint64_t seed, maps::MapId map, maps::CrushedCars crushed) {
    const UpdateTimer timer;
    g_frame++;
    // A new seed makes the picture so far wrong.
    if (seed != g_seed) {
        g_job.active = false;
        for (SparePicture& spare : g_spares) spare.ready = false;
        g_crushedSeen = 0;
    }
    g_seed = seed;
    g_crushed = crushed;
    // A new map makes every painted chunk wrong, along with its lights. Only the flags
    // change here: the textures stay, and get painted over.
    if (map != g_map) {
        g_job.active = false;
        g_map = map;
        for (Chunk& chunk : g_pool) {
            chunk.ready = false;
            chunk.lightCount = 0;
            chunk.coverCount = 0;
        }
        for (SparePicture& spare : g_spares) spare.ready = false;
        g_crushedSeen = 0;
    }
    const ChunkRange visible = visibleChunks(view, 0.0f);
    const ChunkRange ahead = visibleChunks(view, kPaintAheadWorld);
    noteNewlyCrushed(visible, seed);

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

    if (stepRepaint(seed)) return;

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
        g_job = PaintJob{true, firstMissingX, firstMissingY, 0, false};
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
    // Standing objects take part of the gentler grade the characters get.
    float standTint[3];
    for (int i = 0; i < 3; ++i) standTint[i] = lerpf(look.groundTint[i], look.spriteTint[i], theme::kStandingGrade);
    const float standSaturation = lerpf(saturation, 1.0f, theme::kStandingGrade);
    SetShaderValue(ground.shader, ground.standTint, standTint, SHADER_UNIFORM_VEC3);
    SetShaderValue(ground.shader, ground.standSaturation, &standSaturation, SHADER_UNIFORM_FLOAT);

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

// The part of a chunk's texture holding a rectangle of map texels, as a quad at depth z. The
// prop shader ignores z and works out each pixel's own depth instead.
void depthQuad(const View& view, int chunkLeft, int chunkTop, int x0, int y0, int x1, int y1, float z) {
    const float left = static_cast<float>(x0) / maps::kTexelsPerWorldX - view.focusX;
    const float right = static_cast<float>(x1) / maps::kTexelsPerWorldX - view.focusX;
    const float top = static_cast<float>(y0) / maps::kTexelsPerWorldY - view.focusY;
    const float bottom = static_cast<float>(y1) / maps::kTexelsPerWorldY - view.focusY;
    const float u0 = static_cast<float>(x0 - chunkLeft) / static_cast<float>(maps::kChunkWidth);
    const float u1 = static_cast<float>(x1 - chunkLeft) / static_cast<float>(maps::kChunkWidth);
    const float v0 = static_cast<float>(y0 - chunkTop) / static_cast<float>(maps::kChunkHeight);
    const float v1 = static_cast<float>(y1 - chunkTop) / static_cast<float>(maps::kChunkHeight);
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

// Only the rectangles the painters listed are drawn, so the GPU looks at the few pixels that
// can be props and not the whole screen.
void drawPropDepth(const View& view) {
    const PropDepthShader& depth = propDepthShader();
    const float camera[4] = {kArtPerWorld * kTiltSquash, kPerspective, kMinDepth, kDepthMargin};
    const float shape[4] = {view.anchorY, static_cast<float>(view.height), 1.0f / maps::kTexelsPerWorldY, static_cast<float>(maps::kChunkHeight)};
    SetShaderValueMatrix(depth.shader, depth.projection, groundMatrix(view));
    SetShaderValue(depth.shader, depth.camera, camera, SHADER_UNIFORM_VEC4);
    SetShaderValue(depth.shader, depth.view, shape, SHADER_UNIFORM_VEC4);
    BeginShaderMode(depth.shader);
    const ChunkRange visible = visibleChunks(view, 0.0f);
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            const Chunk* chunk = findChunk(x, y, g_seed);
            if (!chunk || chunk->coverCount == 0) continue;
            const int chunkLeft = x * maps::kChunkWidth;
            const int chunkTop = y * maps::kChunkHeight;
            rlSetTexture(chunk->texture.id);
            rlBegin(RL_QUADS);
            for (int i = 0; i < chunk->coverCount; ++i) {
                const maps::Cover& cover = chunk->covers[i];
                depthQuad(view, chunkLeft, chunkTop, chunkLeft + cover.x0, chunkTop + cover.y0, chunkLeft + cover.x1, chunkTop + cover.y1, 0.0f);
            }
            rlEnd();
        }
    }
    rlSetTexture(0);
    EndShaderMode();
}

}  // namespace

void drawCoverDepth(const View& view) {
    rlEnableDepthTest();
    rlColorMask(false, false, false, false);
    drawPropDepth(view);
    const CarDepthShader& depth = carDepthShader();
    SetShaderValueMatrix(depth.shader, depth.projection, groundMatrix(view));
    BeginShaderMode(depth.shader);
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
                depthQuad(view, chunkLeft, chunkTop, x0, y0, x1, y1, depthOfRow(view, carFeetRow(view, car)));
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

int coversOnScreen(const View& view, CoverOnScreen* covers, int capacity) {
    int count = 0;
    const ChunkRange visible = visibleChunks(view, 0.0f);
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            const Chunk* chunk = findChunk(x, y, g_seed);
            if (!chunk) continue;
            const float chunkLeft = static_cast<float>(x * maps::kChunkWidth);
            const float chunkTop = static_cast<float>(y * maps::kChunkHeight);
            for (int i = 0; i < chunk->coverCount; ++i) {
                const maps::Cover& cover = chunk->covers[i];
                const float x0 = (chunkLeft + static_cast<float>(cover.x0)) / maps::kTexelsPerWorldX;
                const float x1 = (chunkLeft + static_cast<float>(cover.x1)) / maps::kTexelsPerWorldX;
                const float y0 = (chunkTop + static_cast<float>(cover.y0)) / maps::kTexelsPerWorldY;
                const float y1 = (chunkTop + static_cast<float>(cover.y1)) / maps::kTexelsPerWorldY;
                const ScreenPoint a = project(view, Vec2{x0, y0});
                const ScreenPoint b = project(view, Vec2{x1, y0});
                const ScreenPoint c = project(view, Vec2{x0, y1});
                const ScreenPoint d = project(view, Vec2{x1, y1});
                CoverOnScreen shown;
                shown.left = std::min(a.x, c.x);
                shown.right = std::max(b.x, d.x);
                shown.top = a.y;
                shown.bottom = c.y;
                if (shown.right < 0.0f || shown.left > static_cast<float>(view.width) || shown.bottom < 0.0f || shown.top > static_cast<float>(view.height)) continue;
                // The same point the depth shader uses: the middle of the foot row.
                const float footY = (chunkTop + static_cast<float>(cover.nearRow) + 0.5f) / maps::kTexelsPerWorldY;
                shown.feetRow = project(view, Vec2{x0, footY}).y;
                shown.depth = depthOfRow(view, shown.feetRow);
                shown.car = false;
                if (count >= capacity) {
                    g_coversDropped++;
                    continue;
                }
                covers[count++] = shown;
            }
        }
    }
    return count;
}

int coversDropped() { return g_coversDropped; }

namespace {

FoundCover g_sceneCover;

bool clearOfCars(maps::MapId map, uint64_t seed, int chunkX, int chunkY, const FoundCover& cover) {
    maps::Obstacle cars[maps::kMaxObstaclesPerTile];
    const int count = maps::obstaclesInTile(map, seed, chunkX, chunkY, cars, maps::kMaxObstaclesPerTile);
    constexpr int kRoom = 24;
    for (int i = 0; i < count; ++i) {
        const maps::TexelRect rect = maps::spriteRect(cars[i]);
        if (rect.x1 + kRoom > cover.x0 && rect.x0 - kRoom < cover.x1 && rect.y1 + kRoom > cover.y0 && rect.y0 - kRoom < cover.y1) return false;
    }
    return true;
}

}  // namespace

FoundCover findCover(maps::MapId map, uint64_t seed, maps::CoverKind kind, int minWide, int minTall) {
    g_job.active = false;  // This paints over the job's picture.
    maps::Light lights[maps::kMaxLightsPerChunk];
    maps::Cover found[maps::kMaxCoversPerChunk];
    constexpr int kEdge = 2;
    for (int ring = 0; ring < 14; ++ring) {
        for (int y = -ring; y <= ring; ++y) {
            for (int x = -ring; x <= ring; ++x) {
                if (std::max(std::abs(x), std::abs(y)) != ring) continue;
                maps::CoverList list{found, 0};
                maps::renderChunk(map, seed, x, y, g_pixels, lights, {}, &list);
                for (int i = 0; i < list.count; ++i) {
                    const maps::Cover& c = found[i];
                    if (c.kind != kind || c.x1 - c.x0 < minWide || c.y1 - c.y0 < minTall) continue;
                    if (c.x0 < kEdge || c.y0 < kEdge || c.x1 > maps::kChunkWidth - kEdge || c.nearRow >= maps::kChunkHeight - kEdge) continue;
                    // Nothing else stands where the scene's zombie will, so a test sees only this
                    // object at work. The zombie is in the middle of a wall, or at a pole's foot.
                    const int middle = kind == maps::CoverKind::Wall ? (c.x0 + c.x1) / 2 : c.footX;
                    bool alone = true;
                    for (int n = 0; n < list.count && alone; ++n) {
                        const maps::Cover& o = found[n];
                        alone = n == i || o.x1 <= middle - 16 || o.x0 >= middle + 16 || o.y1 <= c.nearRow - 50 || o.y0 >= c.nearRow + 16;
                    }
                    if (!alone) continue;
                    const int left = x * maps::kChunkWidth;
                    const int top = y * maps::kChunkHeight;
                    const FoundCover cover{true, left + c.x0, top + c.y0, left + c.x1, top + c.y1, left + c.footX, top + c.nearRow, left + middle};
                    if (clearOfCars(map, seed, x, y, cover)) return cover;
                }
            }
        }
    }
    return {};
}

void setSceneCover(const FoundCover& cover) { g_sceneCover = cover; }

bool sceneCoverOnScreen(const View& view, float out[6]) {
    if (!g_sceneCover.found) return false;
    const ScreenPoint a = project(view, Vec2{static_cast<float>(g_sceneCover.x0) / maps::kTexelsPerWorldX, static_cast<float>(g_sceneCover.y0) / maps::kTexelsPerWorldY});
    const ScreenPoint b = project(view, Vec2{static_cast<float>(g_sceneCover.x1) / maps::kTexelsPerWorldX, static_cast<float>(g_sceneCover.y1) / maps::kTexelsPerWorldY});
    const ScreenPoint spot = project(view, Vec2{static_cast<float>(g_sceneCover.spotX) / maps::kTexelsPerWorldX, (static_cast<float>(g_sceneCover.nearRow) + 0.5f) / maps::kTexelsPerWorldY});
    out[0] = a.x;
    out[1] = a.y;
    out[2] = b.x;
    out[3] = b.y;
    out[4] = spot.x;
    out[5] = spot.y;
    return true;
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

// Test hook: paints the chunks around the start and checks their lists of standing objects.
// Every pixel marked as standing must sit inside some listed rectangle, or a figure behind it
// would be hidden with no silhouette. It also reports the busiest chunk, which must stay under
// the list's size, and how many objects of each kind it saw, so a test knows both maps have them.
EMSCRIPTEN_KEEPALIVE const char* ls_check_cover_marks(int map, int seed, int reach) {
    using namespace ls;
    static char json[256];
    const maps::MapId id = maps::mapFromIndex(map);
    worldview::g_job.active = false;  // This paints over the job's picture.
    maps::Light lights[maps::kMaxLightsPerChunk];
    maps::Cover covers[maps::kMaxCoversPerChunk];
    int strayPixels = 0, markedPixels = 0, busiest = 0, total = 0, walls = 0, trees = 0, lamps = 0;
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            maps::CoverList list{covers, 0};
            maps::renderChunk(id, static_cast<uint64_t>(seed), x, y, worldview::g_pixels, lights, {}, &list);
            busiest = std::max(busiest, list.count);
            total += list.count;
            for (int i = 0; i < list.count; ++i) {
                walls += covers[i].kind == maps::CoverKind::Wall ? 1 : 0;
                trees += covers[i].kind == maps::CoverKind::Tree ? 1 : 0;
                lamps += covers[i].kind == maps::CoverKind::Lamp ? 1 : 0;
            }
            for (int py = 0; py < maps::kChunkHeight; ++py) {
                for (int px = 0; px < maps::kChunkWidth; ++px) {
                    const unsigned char a = worldview::g_pixels[py * maps::kChunkWidth + px].a;
                    if (a == maps::kPlainGround || a == maps::kCarBody) continue;
                    markedPixels++;
                    bool listed = false;
                    for (int i = 0; i < list.count && !listed; ++i) listed = px >= covers[i].x0 && px < covers[i].x1 && py >= covers[i].y0 && py < covers[i].y1;
                    if (!listed) strayPixels++;
                }
            }
        }
    }
    std::snprintf(json, sizeof(json), "{\"covers\":%d,\"busiest\":%d,\"capacity\":%d,\"marked\":%d,\"strayPixels\":%d,\"walls\":%d,\"trees\":%d,\"lamps\":%d}", total, busiest,
                  maps::kMaxCoversPerChunk, markedPixels, strayPixels, walls, trees, lamps);
    return json;
}

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

// Test hook: the slowest updateGround since the last call, in milliseconds.
EMSCRIPTEN_KEEPALIVE double ls_ground_slowest_ms() {
    const double slowest = ls::worldview::g_slowestUpdate * 1000.0;
    ls::worldview::g_slowestUpdate = 0.0;
    return slowest;
}

// Test hook: flattens each car around the start, one at a time, and paints every chunk that
// can show it, with and without the flat car, whole and in the job's pieces. It counts:
// pixels changed outside the car's own reach, which means the rest of the map moved; marked
// pixels left where only this car stood, which would mean it still covers figures; chunks
// that started from a different private picture, which would mean a visible seam; and chunks
// that should show the car but never painted it, which would cut the picture at an edge.
// Test hook: paints each chunk whole, then with all its cars flat, and counts the lights.
EMSCRIPTEN_KEEPALIVE const char* ls_check_wreck_fires(int map, int seed, int reach) {
    using namespace ls;
    static char json[96];
    static Color pixels[maps::kChunkWidth * maps::kChunkHeight];
    const maps::MapId id = maps::mapFromIndex(map);
    worldview::g_job.active = false;
    maps::Light lights[maps::kMaxLightsPerChunk];
    int whole = 0, flat = 0, chunksWithFewer = 0, chunksWithMore = 0;
    for (int y = -reach; y <= reach; ++y) {
        for (int x = -reach; x <= reach; ++x) {
            maps::Obstacle list[maps::kMaxObstaclesPerTile];
            maps::CarId ids[maps::kMaxObstaclesPerTile];
            const int count = maps::obstaclesInTile(id, static_cast<uint64_t>(seed), x, y, list, maps::kMaxObstaclesPerTile);
            for (int n = 0; n < count; ++n) ids[n] = maps::CarId{list[n].texelX, list[n].texelY};
            const int before = maps::renderChunk(id, static_cast<uint64_t>(seed), x, y, pixels, lights);
            const int after = maps::renderChunk(id, static_cast<uint64_t>(seed), x, y, pixels, lights, maps::CrushedCars{ids, count});
            whole += before;
            flat += after;
            if (after < before) chunksWithFewer++;
            if (after > before) chunksWithMore++;
        }
    }
    std::snprintf(json, sizeof(json), "{\"whole\":%d,\"flat\":%d,\"fewer\":%d,\"more\":%d}", whole, flat, chunksWithFewer, chunksWithMore);
    return json;
}

EMSCRIPTEN_KEEPALIVE const char* ls_check_flat_cars(int map, int seed, int reach) {
    using namespace ls;
    static char json[192];
    static Color intact[maps::kChunkWidth * maps::kChunkHeight];
    static Color flat[maps::kChunkWidth * maps::kChunkHeight];
    const maps::MapId id = maps::mapFromIndex(map);
    const uint64_t wide = static_cast<uint64_t>(seed);
    worldview::g_job.active = false;
    maps::Light lights[maps::kMaxLightsPerChunk];

    int cars = 0, overEdge = 0, unchanged = 0, outside = 0, stillMarked = 0, seams = 0, missed = 0, stagedDifferent = 0;
    maps::CarId done[256];
    int doneCount = 0;
    for (int tileY = -reach; tileY <= reach; ++tileY) {
        for (int tileX = -reach; tileX <= reach; ++tileX) {
            maps::Obstacle list[maps::kMaxObstaclesPerTile];
            const int count = maps::obstaclesInTile(id, wide, tileX, tileY, list, maps::kMaxObstaclesPerTile);
            for (int n = 0; n < count && doneCount < 256; ++n) {
                const maps::Obstacle& car = list[n];
                const maps::CarId carId{car.texelX, car.texelY};
                if (maps::CrushedCars{done, doneCount}.holds(car.texelX, car.texelY)) continue;
                done[doneCount++] = carId;
                cars++;
                const maps::CrushedCars crushed{&carId, 1};
                const maps::TexelRect rect = maps::spriteRect(car);
                const maps::TexelRect outer = maps::flatCarReach(car);
                const int chunk0X = static_cast<int>(std::floor(static_cast<float>(outer.x0) / maps::kChunkWidth));
                const int chunk1X = static_cast<int>(std::floor(static_cast<float>(outer.x1) / maps::kChunkWidth));
                const int chunk0Y = static_cast<int>(std::floor(static_cast<float>(outer.y0) / maps::kChunkHeight));
                const int chunk1Y = static_cast<int>(std::floor(static_cast<float>(outer.y1) / maps::kChunkHeight));
                if (static_cast<int>(std::floor(static_cast<float>(rect.x0) / maps::kChunkWidth)) != static_cast<int>(std::floor(static_cast<float>(rect.x1 - 1) / maps::kChunkWidth)) ||
                    static_cast<int>(std::floor(static_cast<float>(rect.y0) / maps::kChunkHeight)) != static_cast<int>(std::floor(static_cast<float>(rect.y1 - 1) / maps::kChunkHeight))) {
                    overEdge++;
                }
                uint32_t firstSum = 0;
                int changed = 0;
                for (int cy = chunk0Y; cy <= chunk1Y; ++cy) {
                    for (int cx = chunk0X; cx <= chunk1X; ++cx) {
                        const int left = cx * maps::kChunkWidth;
                        const int top = cy * maps::kChunkHeight;
                        maps::renderChunk(id, wide, cx, cy, intact, lights);
                        uint32_t sum = 0;
                        maps::setFlatCarProbe(&sum);
                        maps::renderChunk(id, wide, cx, cy, flat, lights, crushed);
                        maps::setFlatCarProbe(nullptr);
                        if (sum != 0u && firstSum != 0u && sum != firstSum) seams++;
                        if (sum != 0u) firstSum = sum;
                        // A flat car spreads and throws bits a few texels past its old picture.
                        constexpr int kStamp = 7;
                        const bool showsCar = rect.x0 - kStamp < left + maps::kChunkWidth && rect.x1 + kStamp > left && rect.y0 < top + maps::kChunkHeight && rect.y1 + kStamp > top;
                        if (showsCar && sum == 0u) missed++;
                        for (int py = 0; py < maps::kChunkHeight; ++py) {
                            for (int px = 0; px < maps::kChunkWidth; ++px) {
                                const Color& a = intact[py * maps::kChunkWidth + px];
                                const Color& b = flat[py * maps::kChunkWidth + px];
                                const int gx = left + px;
                                const int gy = top + py;
                                if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) {
                                    changed++;
                                    if (gx < outer.x0 || gx >= outer.x1 || gy < outer.y0 || gy >= outer.y1) outside++;
                                }
                                if (b.a != maps::kCarBody || gx < rect.x0 || gx >= rect.x1 || gy < rect.y0 || gy >= rect.y1) continue;
                                // Another car in a pile may well stand here, and keeps its mark.
                                bool other = false;
                                maps::Obstacle near[maps::kMaxObstaclesPerTile];
                                const int nearCount = maps::obstaclesInTile(id, wide, cx, cy, near, maps::kMaxObstaclesPerTile);
                                for (int k = 0; k < nearCount && !other; ++k) {
                                    if (maps::isCar(carId, near[k])) continue;
                                    const maps::TexelRect them = maps::flatCarReach(near[k]);
                                    other = gx >= them.x0 && gx < them.x1 && gy >= them.y0 && gy < them.y1;
                                }
                                if (!other) stillMarked++;
                            }
                        }
                        // Whole or in pieces, over a dirty buffer: the same picture.
                        for (int band = 0; band < worldview::kGroundBands; ++band) {
                            const int rowFrom = band * worldview::kBandRows;
                            maps::paintGroundRows(id, wide, cx, cy, worldview::g_pixels, rowFrom, std::min(rowFrom + worldview::kBandRows, static_cast<int>(maps::kChunkHeight)));
                        }
                        maps::paintDetails(id, wide, cx, cy, worldview::g_pixels, lights, crushed);
                        for (int i = 0; i < maps::kChunkWidth * maps::kChunkHeight; ++i) {
                            const Color& a = flat[i];
                            const Color& b = worldview::g_pixels[i];
                            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) stagedDifferent++;
                        }
                    }
                }
                if (changed == 0) unchanged++;
            }
        }
    }
    std::snprintf(json, sizeof(json), "{\"cars\":%d,\"overEdge\":%d,\"unchanged\":%d,\"outside\":%d,\"stillMarked\":%d,\"seams\":%d,\"missed\":%d,\"stagedDifferent\":%d}", cars, overEdge, unchanged, outside, stillMarked, seams, missed,
                  stagedDifferent);
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
