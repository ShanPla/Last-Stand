#include "components/WorldView/Ground.h"

#include "components/WorldView/Draw.h"
#include "components/WorldView/Shaders.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

// Enough for the widest view, plus a ring being painted ahead of the camera.
constexpr int kPoolSize = 24;

// Painted one chunk's width ahead of what can be seen, so it is ready in time.
constexpr float kPaintAheadWorld = 200.0f;

constexpr float kPoolReachWorld = 120.0f;
constexpr int kFlickerPeriodTicks = 7;

struct Chunk {
    int x = 0;
    int y = 0;
    uint64_t seed = 0;
    bool ready = false;
    uint32_t lastUsed = 0;
    Texture2D texture{};
    maps::Light lights[maps::kMaxLightsPerChunk];
    int lightCount = 0;
};

Chunk g_pool[kPoolSize];
Color g_pixels[maps::kChunkWidth * maps::kChunkHeight];
Color g_halfPixels[(maps::kChunkWidth / 2) * (maps::kChunkHeight / 2)];
bool g_halfResolution = false;
uint32_t g_frame = 0;
uint64_t g_seed = 0;

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

void paintChunk(Chunk& chunk, int x, int y, uint64_t seed) {
    chunk.lightCount = maps::renderChunk(maps::MapId::City, seed, x, y, g_pixels, chunk.lights);
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

}  // namespace

void initGround() { loadPoolTextures(); }

void setGroundHalfResolution(bool half) {
    if (half == g_halfResolution) return;
    g_halfResolution = half;
    if (g_pool[0].texture.id != 0) loadPoolTextures();
}

void updateGround(const View& view, uint64_t seed) {
    g_frame++;
    g_seed = seed;
    const ChunkRange visible = visibleChunks(view, 0.0f);
    const ChunkRange ahead = visibleChunks(view, kPaintAheadWorld);

    // Anything on screen must be there now. This only paints several at once on the
    // first frame, after a jump, or when a run starts with a new seed.
    for (int y = visible.y0; y <= visible.y1; ++y) {
        for (int x = visible.x0; x <= visible.x1; ++x) {
            if (Chunk* chunk = findChunk(x, y, seed)) {
                chunk->lastUsed = g_frame;
            } else {
                paintChunk(evictionTarget(visible, seed), x, y, seed);
            }
        }
    }

    // One chunk per frame ahead of the camera, so walking never causes a hitch.
    for (int y = ahead.y0; y <= ahead.y1; ++y) {
        for (int x = ahead.x0; x <= ahead.x1; ++x) {
            if (x >= visible.x0 && x <= visible.x1 && y >= visible.y0 && y <= visible.y1) continue;
            if (Chunk* chunk = findChunk(x, y, seed)) {
                chunk->lastUsed = g_frame;
                continue;
            }
            paintChunk(evictionTarget(visible, seed), x, y, seed);
            return;
        }
    }
}

void drawGround(const View& view, const DayLook& look) {
    const GroundShader& ground = groundShader();
    const float w = static_cast<float>(view.width);
    const float h = static_cast<float>(view.height);
    const float ax = view.anchorX;
    const float ay = view.anchorY;
    const float k = kArtPerWorld;
    const float ky = kArtPerWorld * kTiltSquash;

    // project() written as a matrix. The fourth row is the depth, which makes the GPU
    // do the same divide per pixel. The target's y runs down from its top edge.
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

    const float saturation = look.groundSaturation;
    SetShaderValueMatrix(ground.shader, ground.projection, matrix);
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

void drawLightPools(const View& view, const DayLook& look, uint32_t tick, bool flicker) {
    if (look.lampLevel <= 0.01f) return;
    const ChunkRange visible = visibleChunks(view, kPoolReachWorld);
    for (const Chunk& chunk : g_pool) {
        if (!chunk.ready) continue;
        if (chunk.x < visible.x0 || chunk.x > visible.x1 || chunk.y < visible.y0 || chunk.y > visible.y1) continue;
        for (int i = 0; i < chunk.lightCount; ++i) {
            const maps::Light& light = chunk.lights[i];
            const bool fire = light.kind == maps::LightKind::Fire;
            const uint32_t seed = hashPosition(light.world);
            // Dusk only warms up slowly, so lights still look weak until it is really dark.
            // Bulbs buzz now and then, and fires never stop moving.
            float level = look.lampLevel * look.lampLevel;
            if (flicker) {
                if (fire) level *= 0.82f + 0.18f * triangleWave(tick + seed, kFlickerPeriodTicks + (seed & 3u));
                else if (((tick / 8u + seed) & 127u) < 3u) level *= 0.55f;
            }
            const ScreenPoint point = project(view, light.world);
            if (!onTarget(view, point, 70.0f, 40.0f, 40.0f)) continue;
            // Lamps are wide, and fires warm and small.
            const float size = point.scale * (fire ? 0.8f : 1.0f);
            const uint8_t row = fire ? kRowFireLight : kRowLampLight;
            poolQuad(itemSprite(Item::LightPool), point.x, point.y, size, row, level);
            // Multiplying can only double a color, and asphalt is dark, so the bright
            // middle of the pool takes a second, smaller layer.
            poolQuad(itemSprite(Item::LightPool), point.x, point.y, size * 0.6f, row, level);
        }
    }
}

void drawLightSprites(const View& view, const DayLook& look, uint32_t tick, bool flicker) {
    const ChunkRange visible = visibleChunks(view, 60.0f);
    constexpr Item kFireFrames[4] = {Item::FireA, Item::FireB, Item::FireC, Item::FireD};
    for (const Chunk& chunk : g_pool) {
        if (!chunk.ready) continue;
        if (chunk.x < visible.x0 || chunk.x > visible.x1 || chunk.y < visible.y0 || chunk.y > visible.y1) continue;
        for (int i = 0; i < chunk.lightCount; ++i) {
            const maps::Light& light = chunk.lights[i];
            const ScreenPoint point = project(view, light.world);
            if (!onTarget(view, point, 24.0f, 60.0f, 8.0f)) continue;
            const uint32_t seed = hashPosition(light.world);
            if (light.kind == maps::LightKind::Fire) {
                // Fires burn all day, so these show whatever the time.
                const uint32_t frame = flicker ? (tick / 5u + seed) & 3u : seed & 3u;
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
