#include "components/WorldView/WorldView.h"

#include "components/Effects/Effects.h"
#include "components/WorldView/Characters.h"
#include "components/WorldView/DayCycle.h"
#include "components/WorldView/Draw.h"
#include "components/WorldView/Ground.h"
#include "components/WorldView/Objects.h"
#include "components/WorldView/Shaders.h"
#include "components/WorldView/Showcase.h"
#include "core/Platform.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ls::worldview {

namespace {

// --- Picture size --------------------------------------------------------------

// The target has about this many rows in landscape, and columns in portrait.
constexpr float kIdealRowsLandscape = 360.0f;
constexpr float kIdealColumnsPortrait = 420.0f;
// How far down the window the feet sit. Perspective shows more ground above
// the player than below, so moving him up evens out how far can be seen each way.
constexpr float kAnchorLandscape = 0.46f;
constexpr float kAnchorPortrait = 0.38f;
// A zombie must not be seen spawning. The edge of the view stays this far inside the
// spawn circle, which also leaves room for half a figure's width.
constexpr float kSpawnMargin = 34.0f;
// A whole-number scale is used when one is within this ratio of the ideal.
constexpr float kWholeScaleTolerance = 1.17f;

// Level of detail. Sprites are one quad each, so this only trims the extra quads:
// the long shadow, shown for the nearest figures.
constexpr int kLongShadowBudget = 500;
constexpr int kLongShadowBudgetFast = 60;
constexpr float kLongShadowNear = 170.0f;  // Art pixels from the soldier.

// Depth sort. Sorting by a coarse bucket is enough: figures in the same bucket are at
// almost the same height on screen, so their order can't be noticed.
constexpr int kSortBuckets = 512;
constexpr float kSortMargin = 260.0f;

constexpr uint8_t kSoldierSeeThrough = 130;

constexpr int kPlayerSlot = kMaxEnemies;  // The player takes one extra slot in the sort.

// A clock that jumps back to under this many ticks means a run has just begun.
constexpr uint32_t kFreshRunTicks = 30;

struct VisibleFigure {
    ScreenPoint feet;
    uint16_t enemyIndex;
};

// Fixed memory, so that drawing never touches the heap.
VisibleFigure g_visible[kMaxEnemies + 1];
uint16_t g_drawOrder[kMaxEnemies + 1];
int g_bucketStarts[kSortBuckets + 1];
// Zombies warning of an attack, in or out of the picture. Filled by the same walk that finds
// the visible figures, so it costs a comparison per zombie and no second loop.
uint16_t g_telegraphs[kMaxEnemies];
int g_telegraphCount = 0;

// Only read by the browser tests, through ls_query_world.
int g_telegraphsShown = 0;
const GameState* g_lastDrawnState = nullptr;

// The last run the renderer saw, so it can notice a new one. App starts runs on its own.
uint64_t g_seenSeed = 0;
uint32_t g_seenTick = 0;

RenderTexture2D g_target{};
int g_targetScreenWidth = 0;
int g_targetScreenHeight = 0;
View g_view;

bool g_ready = false;
bool g_fastMode = false;
int g_quality = 1;
float g_fixedDayTime = -1.0f;

// How far from the anchor the camera can see, in world units, for a target this size.
float farthestVisible(float width, float height, float anchorRow) {
    View probe;
    probe.width = static_cast<int>(width);
    probe.height = static_cast<int>(height);
    probe.anchorX = width * 0.5f;
    probe.anchorY = anchorRow;
    // The top corners are always the farthest, as perspective widens the view up there.
    const Vec2 corner = unproject(probe, width, 0.0f);
    return length(corner);
}

// Finds how many screen pixels one art pixel takes. Characters are always painted 1:1
// in the target, so this also sets how much of the world can be seen.
float chooseScale(int screenWidth, int screenHeight) {
    const bool portrait = screenHeight > screenWidth;
    const float sw = static_cast<float>(screenWidth);
    const float sh = static_cast<float>(screenHeight);
    const float anchor = portrait ? kAnchorPortrait : kAnchorLandscape;
    const float limit = balance::kSpawnRadius - kSpawnMargin;
    const auto seesTooFar = [&](float scale) { return farthestVisible(sw / scale, sh / scale, sh / scale * anchor) > limit; };

    const float ideal = std::max(1.0f, portrait ? sw / kIdealColumnsPortrait : sh / kIdealRowsLandscape);
    float smallest = ideal;
    // Zoom in only as far as the spawn circle needs.
    while (seesTooFar(smallest)) smallest *= 1.01f;

    const float below = std::floor(smallest);
    if (below >= 1.0f && !seesTooFar(below) && smallest / below <= kWholeScaleTolerance) return below;
    const float above = std::ceil(smallest);
    if (above / smallest <= kWholeScaleTolerance) return above;
    return smallest;
}

void fitTargetToWindow() {
    const int screenWidth = GetScreenWidth();
    const int screenHeight = GetScreenHeight();
    if (screenWidth == g_targetScreenWidth && screenHeight == g_targetScreenHeight && g_target.id != 0) return;
    g_targetScreenWidth = screenWidth;
    g_targetScreenHeight = screenHeight;

    const float scale = chooseScale(screenWidth, screenHeight);
    const bool portrait = screenHeight > screenWidth;
    g_view.pixelScale = scale;
    g_view.width = std::max(16, static_cast<int>(std::ceil(static_cast<float>(screenWidth) / scale)));
    g_view.height = std::max(16, static_cast<int>(std::ceil(static_cast<float>(screenHeight) / scale)));
    g_view.anchorX = std::floor(static_cast<float>(g_view.width) * 0.5f);
    g_view.anchorY = std::floor(static_cast<float>(g_view.height) * (portrait ? kAnchorPortrait : kAnchorLandscape));

    if (g_target.id != 0) UnloadRenderTexture(g_target);
    g_target = LoadRenderTexture(g_view.width, g_view.height);
    // The present shader only blends at art pixel edges, so linear filtering is what
    // gives clean pixels at scales that aren't whole numbers. At a whole number scale
    // there is nothing to blend, and nearest filtering keeps the colors exact.
    const bool wholeScale = scale == std::floor(scale);
    SetTextureFilter(g_target.texture, wholeScale ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(g_target.texture, TEXTURE_WRAP_CLAMP);

    if (platform::debugEnabled()) {
        const float sees = farthestVisible(static_cast<float>(g_view.width), static_cast<float>(g_view.height), g_view.anchorY);
        // The page sends stderr to the console, and stdout nowhere.
        std::fprintf(stderr, "world view: target %dx%d, scale %.2f, sees %.0f of %.0f\n", g_view.width, g_view.height, static_cast<double>(scale),
                    static_cast<double>(sees), static_cast<double>(balance::kSpawnRadius));
    }
}

// The camera stays on whole art pixels. Sprites and ground share the same grid, so
// nothing shimmers against anything else as the view scrolls. Screen shake is added
// here in whole art pixels too, so to the renderer a shake is just the camera walking.
// Returns where the camera would be with no shake, which is where the soldier stands.
Vec2 aimCamera(const GameState& state, float alpha) {
    const Vec2 player = lerp(state.player.prevPos, state.player.pos, alpha);
    const float column = std::floor(player.x * kArtPerWorld + 0.5f);
    const float row = std::floor(player.y * kArtPerWorld * kTiltSquash + 0.5f);
    int shakeColumns = 0;
    int shakeRows = 0;
    effects::shakeArtPixels(&shakeColumns, &shakeRows);
    g_view.focusX = (column + static_cast<float>(shakeColumns)) / kArtPerWorld;
    g_view.focusY = (row + static_cast<float>(shakeRows)) / (kArtPerWorld * kTiltSquash);
    return {column / kArtPerWorld, row / (kArtPerWorld * kTiltSquash)};
}

// --- Figures -----------------------------------------------------------------

int collectVisibleZombies(const GameState& state, float alpha, float shadowLength) {
    int visible = 0;
    int flashing = 0;
    bool bossSeen = false;
    g_telegraphCount = 0;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        // Checked before the figure is culled: a warning line must show even when the
        // zombie itself stands outside the picture.
        if (isTelegraphing(enemy)) g_telegraphs[g_telegraphCount++] = static_cast<uint16_t>(i);
        const ScreenPoint feet = project(g_view, lerp(enemy.prevPos, enemy.pos, alpha));
        const float side = figureHalfWidth(enemy) * feet.scale;
        const float above = figureHeight(enemy) * feet.scale;
        // The sun's shadows lean right, so a figure past the left edge can still throw one
        // into the picture, and it needs that much more room on that side only.
        const float leftRoom = side + longShadowReach(enemy, shadowLength) * feet.scale;
        const bool inPicture = feet.x > -leftRoom && feet.x < static_cast<float>(g_view.width) + side && feet.y > -12.0f && feet.y < static_cast<float>(g_view.height) + above;
        if (!inPicture) continue;
        VisibleFigure& figure = g_visible[visible++];
        figure.feet = feet;
        figure.enemyIndex = static_cast<uint16_t>(i);
        if (enemy.flash > 0.0f) flashing++;
        if (enemy.type == EnemyType::Boss) bossSeen = figureTouchesTarget(g_view, state, enemy, feet);
    }
    // The effects need the size of the crowd under fire before any figure is drawn.
    effects::setFlashingCount(flashing);
    effects::noteBossOnScreen(bossSeen, state);
    return visible;
}

// Counting sort by screen height: things further up the screen come first, so nearer
// figures draw over them. It runs in linear time and uses only fixed arrays.
void sortByDepth(int count) {
    const float top = -kSortMargin;
    const float range = static_cast<float>(g_view.height) + kSortMargin * 2.0f;
    const float toBucket = static_cast<float>(kSortBuckets) / range;
    const auto bucketOf = [&](const VisibleFigure& figure) {
        return std::clamp(static_cast<int>((figure.feet.y - top) * toBucket), 0, kSortBuckets - 1);
    };
    std::fill(g_bucketStarts, g_bucketStarts + kSortBuckets + 1, 0);
    for (int i = 0; i < count; ++i) g_bucketStarts[bucketOf(g_visible[i]) + 1]++;
    for (int i = 0; i < kSortBuckets; ++i) g_bucketStarts[i + 1] += g_bucketStarts[i];
    for (int i = 0; i < count; ++i) g_drawOrder[g_bucketStarts[bucketOf(g_visible[i])]++] = static_cast<uint16_t>(i);
}

// --- Passes ------------------------------------------------------------------

void beginFlat() {
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
}

void endFlat() {
    rlEnd();
    rlSetTexture(0);
}

void beginSprites() {
    BeginShaderMode(paletteShader().shader);
    rlSetTexture(atlasTextureId());
    rlBegin(RL_QUADS);
}

void endSprites() {
    rlEnd();
    rlSetTexture(0);
    EndShaderMode();
}

void bindPalette(const DayLook& look) {
    const PaletteShader& palette = paletteShader();
    SetShaderValue(palette.shader, palette.tint, look.spriteTint, SHADER_UNIFORM_VEC3);
    // The palette lives in unit 1. Going straight to the GPU state here keeps raylib's
    // batch from treating it as a texture change and splitting the draw calls.
    rlActiveTextureSlot(1);
    rlEnableTexture(paletteTextureId());
    rlActiveTextureSlot(0);
}

float dayClock(const GameState& state) {
    return g_fixedDayTime >= 0.0f ? g_fixedDayTime : state.timeSeconds();
}

void present(const DayLook& look) {
    const PresentShader& shader = presentShader();
    const float textureSize[2] = {static_cast<float>(g_target.texture.width), static_cast<float>(g_target.texture.height)};
    const float vignette = g_quality == 0 ? 0.0f : look.vignette;
    SetShaderValue(shader.shader, shader.textureSize, textureSize, SHADER_UNIFORM_VEC2);
    SetShaderValue(shader.shader, shader.pixelScale, &g_view.pixelScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader.shader, shader.vignette, &vignette, SHADER_UNIFORM_FLOAT);

    const float width = static_cast<float>(g_view.width) * g_view.pixelScale;
    const float height = static_cast<float>(g_view.height) * g_view.pixelScale;
    // Render textures are stored upside down, so the v coordinates are swapped.
    BeginShaderMode(shader.shader);
    rlSetTexture(g_target.texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0.0f, 1.0f);
    rlVertex2f(0.0f, 0.0f);
    rlTexCoord2f(0.0f, 0.0f);
    rlVertex2f(0.0f, height);
    rlTexCoord2f(1.0f, 0.0f);
    rlVertex2f(width, height);
    rlTexCoord2f(1.0f, 1.0f);
    rlVertex2f(width, 0.0f);
    rlEnd();
    rlSetTexture(0);
    EndShaderMode();
}

}  // namespace

void init() {
    if (g_ready) return;
    const double start = GetTime();
    loadShaders();
    const double shadersDone = GetTime();
    const int atlasRows = buildAtlas();
    const double atlasDone = GetTime();
    initGround();
    fitTargetToWindow();
    g_ready = true;
    if (platform::debugEnabled()) {
        std::fprintf(stderr, "world renderer ready: shaders %.1f ms, atlas %.1f ms using %d of %d rows, ground pool and target %.1f ms\n", (shadersDone - start) * 1000.0,
                     (atlasDone - shadersDone) * 1000.0, atlasRows, kAtlasSize, (GetTime() - atlasDone) * 1000.0);
    }
}

void setFastMode(bool enabled) { g_fastMode = enabled; }
void setFixedDayTime(float seconds) { g_fixedDayTime = seconds; }

void setQuality(int quality) {
    g_quality = quality;
    setGroundHalfResolution(quality == 0);
}

void consumeEvents(const GameState& state) {
    // Before this frame's events, so a first shot in the new run still counts. A seed change
    // is a new run. So is the clock going back to the very start, which catches a restart on
    // the same seed, and the title demo's own loop (which only goes back part way) doesn't.
    const uint32_t tick = state.run.tick;
    if (state.run.seed != g_seenSeed || (tick < g_seenTick && tick < kFreshRunTicks)) resetSoldierAnimation();
    g_seenSeed = state.run.seed;
    g_seenTick = tick;

    for (const SimEvent& event : state.events) {
        switch (event.type) {
            case EventType::WeaponFired: noteWeaponFired(static_cast<WeaponId>(event.detail)); break;
            case EventType::EnemyKilled:
            case EventType::EliteKilled:
            case EventType::BossKilled: addCorpse(static_cast<EnemyType>(event.detail), event.pos, state.run.tick); break;
            default: break;
        }
    }
    updateSmoke(state);
}

void draw(const GameState& state, float alpha) {
    if (!g_ready) init();
    fitTargetToWindow();
    const Vec2 ringCenter = aimCamera(state, alpha);
    updateGround(g_view, state.run.seed);

    const Vec2 playerWorld = lerp(state.player.prevPos, state.player.pos, alpha);
    updateSoldierAnimation(playerWorld, GetFrameTime());

    const DayLook look = dayLookAt(dayClock(state));
    const uint32_t tick = state.run.tick;
    const bool flicker = effects::flickerAllowed() && g_quality > 0;

    const int zombies = collectVisibleZombies(state, alpha, look.shadowLength);
    VisibleFigure& playerFigure = g_visible[zombies];
    playerFigure.feet = project(g_view, playerWorld);
    playerFigure.enemyIndex = static_cast<uint16_t>(kPlayerSlot);
    const int figures = zombies + 1;
    sortByDepth(figures);

    BeginTextureMode(g_target);
    ClearBackground(maps::fallbackGround(maps::current()));

    drawGround(g_view, look);

    bindPalette(look);
    if (g_quality > 0 && look.lampLevel > 0.01f) {
        // Light multiplies what is under it, so lit ground keeps its own colors.
        rlSetBlendFactors(0x0306 /* destination color */, 1 /* one */, 0x8006 /* add */);
        rlSetBlendMode(RL_BLEND_CUSTOM);
        beginSprites();
        drawLightPools(g_view, look, tick, flicker);
        // He carries a little light of his own, so the ground near him stays readable.
        poolQuad(itemSprite(Item::LightPool), playerFigure.feet.x, playerFigure.feet.y, 1.2f, kRowPlayerLight, look.lampLevel);
        endSprites();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    // Flat things on the ground first, so characters always stand on top of them.
    beginFlat();
    // Stains go under the telegraphs, so they can never hide one.
    effects::drawStains(g_view, look.spriteTint);
    drawGroundMarks(g_view, state, alpha);
    // The ring sits on the unshaken camera position, the same whole art pixel as his
    // feet, so it never wobbles around him as he walks.
    drawSoldierRing(g_view, ringCenter);
    endFlat();

    beginSprites();
    drawLightSprites(g_view, look, tick, flicker);
    drawCorpses(g_view, tick);
    drawGroundSprites(g_view, state, alpha);
    drawSpriteSheet();

    // All shadows go down before any figure, so none falls across a body.
    const bool longShadows = look.shadowLength > 0.02f;
    const int shadowBudget = g_fastMode ? kLongShadowBudgetFast : kLongShadowBudget;
    drawSoldierShadow(state, playerFigure.feet, look);
    for (int i = 0; i < zombies; ++i) {
        const VisibleFigure& figure = g_visible[i];
        const Enemy& enemy = state.enemies[figure.enemyIndex];
        // In a very big crowd, only figures near the soldier keep the long shadow.
        const float away = std::abs(figure.feet.x - playerFigure.feet.x) + std::abs(figure.feet.y - playerFigure.feet.y);
        const bool wanted = zombies <= shadowBudget || away < kLongShadowNear || enemy.type == EnemyType::Boss;
        if (longShadows && wanted) drawZombieLongShadow(state, enemy, figure.feet, look);
        drawZombieShadow(enemy, figure.feet);
    }

    drawDashTrail(g_view, state, playerWorld);
    for (int i = 0; i < figures; ++i) {
        const VisibleFigure& figure = g_visible[g_drawOrder[i]];
        if (figure.enemyIndex == kPlayerSlot) drawSoldier(state, figure.feet, 255);
        else drawZombie(state, state.enemies[figure.enemyIndex], figure.feet);
    }
    // He shows faintly through anything standing in front of him, so he can't be lost.
    drawSoldier(state, playerFigure.feet, kSoldierSeeThrough);
    endSprites();

    // Above the crowd, the things that must never be lost in it.
    beginFlat();
    for (int i = 0; i < zombies; ++i) {
        const VisibleFigure& figure = g_visible[i];
        const Enemy& enemy = state.enemies[figure.enemyIndex];
        if (enemy.elite) drawEliteBar(state, enemy, figure.feet);
    }
    g_telegraphsShown = 0;
    for (int i = 0; i < g_telegraphCount; ++i) {
        const Enemy& enemy = state.enemies[g_telegraphs[i]];
        if (drawTelegraph(g_view, state, enemy, lerp(enemy.prevPos, enemy.pos, alpha))) g_telegraphsShown++;
    }
    drawBladeTrails(g_view, state, alpha);
    drawSoldierRingOverCrowd(g_view, ringCenter);
    endFlat();

    // Weak devices have no neon layer, so plain discs light up their blasts instead.
    if (!effects::glowEnabled()) {
        rlSetBlendMode(RL_BLEND_ADDITIVE);
        beginFlat();
        drawGlows(g_view, state);
        endFlat();
        rlSetBlendMode(RL_BLEND_ALPHA);
    }

    beginFlat();
    drawBlasts(g_view, state);
    drawShots(g_view, state, alpha);
    drawBeams(g_view, state);
    effects::drawParticles(g_view, look.spriteTint);
    endFlat();

    beginSprites();
    drawSmoke(g_view, tick);
    drawAirSprites(g_view, state, alpha);
    drawMuzzleFlash(state, playerFigure.feet);
    endSprites();

    // The neon layer, any full-screen flash, then the numbers.
    queueShotGlows(g_view, state, alpha);
    effects::drawGlow(g_view);
    effects::drawScreenFlash(g_view);
    effects::drawNumbers(g_view, state);

    // Enemy bullets are drawn last, so nothing can hide them.
    beginSprites();
    drawEnemyBullets(g_view, state, alpha);
    endSprites();

    EndTextureMode();
    present(look);
}

}  // namespace ls::worldview
