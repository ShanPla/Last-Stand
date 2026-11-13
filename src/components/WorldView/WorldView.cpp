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

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

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
// A zombie behind a car still shows at this strength, so a car never hides a threat.
constexpr uint8_t kCarSilhouette = 130;
// Behind anything else it shows more faintly. Those are thinner than a car, so the figure
// still shows around them, and a strong silhouette would make a pole look like it stands behind.
constexpr uint8_t kPropSilhouette = 96;
// Just nearer than the cover, so the silhouette passes the depth test on its pixels.
constexpr float kSilhouetteDepthGap = 0.0005f;
// Covers on screen are sorted into columns this many art pixels wide, so each figure only
// checks the few that share its columns, even with 2,000 zombies and hundreds of objects.
constexpr int kCoverColumnWidth = 32;
constexpr int kCoverColumns = 48;

constexpr int kPlayerSlot = kMaxEnemies;  // Players take the slots after the zombies in the sort, one each.

// A clock that jumps back to under this many ticks means a run has just begun.
constexpr uint32_t kFreshRunTicks = 30;

struct VisibleFigure {
    ScreenPoint feet;
    uint16_t enemyIndex;
};

// Fixed memory, so that drawing never touches the heap.
VisibleFigure g_visible[kMaxEnemies + kMaxPlayers];
uint16_t g_drawOrder[kMaxEnemies + kMaxPlayers];
int g_bucketStarts[kSortBuckets + 1];
// Zombies warning of an attack, in or out of the picture. Filled by the same walk that finds
// the visible figures, so it costs a comparison per zombie and no second loop.
uint16_t g_telegraphs[kMaxEnemies];
int g_telegraphCount = 0;
CarOnScreen g_cars[kMaxCarsOnScreen];
int g_carCount = 0;
// Every car and standing object on screen, and which of them reach each column.
CoverOnScreen g_covers[kMaxCoversOnScreen];
int g_coverCount = 0;
uint16_t g_columnCovers[kCoverColumns][kMaxCoversOnScreen];
int g_columnCount[kCoverColumns];
bool g_showCarBoxes = false;  // Debug only: outlines each car's solid box.
bool g_carCover = true;       // Debug only: off, cars hide nothing, for comparing pictures.

// Only read by the browser tests, through ls_query_world.
int g_telegraphsShown = 0;
int g_shieldsShown = 0;  // Shield bubbles drawn in the last frame, for the browser tests.
int g_silhouettesShown = 0;
const GameState* g_lastDrawnState = nullptr;
int g_lastPlayerFigures = 0;  // Soldiers drawn on the last frame, for the co-op browser tests.

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
    const Vec2 player = lerp(state.localPlayer().prevPos, state.localPlayer().pos, alpha);
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

int columnOf(float x) { return std::clamp(static_cast<int>(std::floor(x / static_cast<float>(kCoverColumnWidth))), 0, kCoverColumns - 1); }

// Gathers the cars and standing objects on screen into one list, sorted into columns.
void collectCovers() {
    g_coverCount = 0;
    for (int i = 0; i < g_carCount && g_coverCount < kMaxCoversOnScreen; ++i) {
        const CarOnScreen& car = g_cars[i];
        g_covers[g_coverCount++] = CoverOnScreen{car.left, car.top, car.right, car.bottom, car.feetRow, car.depth, true};
    }
    g_coverCount += coversOnScreen(g_view, g_covers + g_coverCount, kMaxCoversOnScreen - g_coverCount);
    std::fill(g_columnCount, g_columnCount + kCoverColumns, 0);
    for (int i = 0; i < g_coverCount; ++i) {
        const int last = columnOf(g_covers[i].right);
        for (int column = columnOf(g_covers[i].left); column <= last; ++column) g_columnCovers[column][g_columnCount[column]++] = static_cast<uint16_t>(i);
    }
}

// What a figure needs to show through the things in front of it.
struct Covering {
    bool found = false;
    float left, top, right, bottom;  // Around every cover it stands behind.
    float depth;                     // Just nearer than the nearest of those.
    bool car;                        // One of them is a car.
};

// Everything this figure stands behind and overlaps in the picture. A silhouette at the
// nearest one's depth passes the depth test on all of them. Where the figure isn't covered,
// the silhouette is the same picture drawn over itself, so it changes nothing there.
Covering coveringOf(const Enemy& enemy, const ScreenPoint& feet) {
    Covering covering;
    const float side = figureHalfWidth(enemy) * feet.scale;
    const float top = feet.y - figureHeight(enemy) * feet.scale;
    const int first = columnOf(feet.x - side);
    const int last = columnOf(feet.x + side);
    for (int column = first; column <= last; ++column) {
        for (int n = 0; n < g_columnCount[column]; ++n) {
            const CoverOnScreen& cover = g_covers[g_columnCovers[column][n]];
            if (feet.y >= cover.feetRow || feet.y <= cover.top) continue;
            if (feet.x + side <= cover.left || feet.x - side >= cover.right || top >= cover.bottom) continue;
            if (!covering.found) {
                covering = Covering{true, cover.left, cover.top, cover.right, cover.bottom, cover.depth, cover.car};
                continue;
            }
            covering.left = std::min(covering.left, cover.left);
            covering.top = std::min(covering.top, cover.top);
            covering.right = std::max(covering.right, cover.right);
            covering.bottom = std::max(covering.bottom, cover.bottom);
            covering.depth = std::max(covering.depth, cover.depth);
            covering.car = covering.car || cover.car;
        }
    }
    return covering;
}

void drawFigure(const GameState& state, const VisibleFigure& figure) {
    if (figure.enemyIndex >= kPlayerSlot) {
        setSpriteDepth(depthOfRow(g_view, figure.feet.y));
        drawSoldier(state, figure.enemyIndex - kPlayerSlot, figure.feet, 255);
        return;
    }
    const Enemy& enemy = state.enemies[figure.enemyIndex];
    // The boss crushes through cars, so it is always drawn over them.
    if (enemy.type == EnemyType::Boss) {
        setSpriteDepth(kDepthNearest);
        drawZombie(state, enemy, figure.feet);
        return;
    }
    setSpriteDepth(depthOfRow(g_view, figure.feet.y));
    drawZombie(state, enemy, figure.feet);
    if (!g_carCover) return;
    if (const Covering covering = coveringOf(enemy, figure.feet); covering.found) {
        // Right after its own figure, so zombies nearer the camera still draw over it.
        setSpriteDepth(covering.depth + kSilhouetteDepthGap);
        drawZombieSilhouette(state, enemy, figure.feet, covering.car ? kCarSilhouette : kPropSilhouette, covering.left, covering.top, covering.right, covering.bottom);
        g_silhouettesShown++;
    }
}

void drawCarBoxes() {
    for (int i = 0; i < g_carCount; ++i) {
        const CarOnScreen& car = g_cars[i];
        const auto corner = [&](float x, float y) {
            const ScreenPoint point = project(g_view, Vec2{x, y});
            return Vec2{point.x, point.y};
        };
        const Vec2 a = corner(car.boxMin.x, car.boxMin.y);
        const Vec2 b = corner(car.boxMax.x, car.boxMin.y);
        const Vec2 c = corner(car.boxMax.x, car.boxMax.y);
        const Vec2 d = corner(car.boxMin.x, car.boxMax.y);
        thickLine(a, b, 1.0f, theme::kDebugCarBox);
        thickLine(b, c, 1.0f, theme::kDebugCarBox);
        thickLine(c, d, 1.0f, theme::kDebugCarBox);
        thickLine(d, a, 1.0f, theme::kDebugCarBox);
    }
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

const View& currentView() { return g_view; }

bool screenPosition(Vec2 world, float liftArtPixels, float* x, float* y) {
    const ScreenPoint point = project(g_view, world);
    if (!onTarget(g_view, point, 30.0f, liftArtPixels + 30.0f, 30.0f)) return false;
    *x = point.x * g_view.pixelScale;
    *y = (point.y - liftArtPixels * point.scale) * g_view.pixelScale;
    return true;
}

void setPlayerColors(const int colors[kMaxPlayers]) { setSoldierColors(colors); }

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
void setShowCarBoxes(bool show) { g_showCarBoxes = show; }
void setCarCover(bool on) { g_carCover = on; }
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
            case EventType::WeaponFired: noteWeaponFired(event.player == kNoPlayer ? 0 : event.player, static_cast<WeaponId>(event.detail)); break;
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
    // Only does work on the first frame of a run on another difficulty.
    useBossLevel(state.run.difficulty);
    fitTargetToWindow();
    const Vec2 ringCenter = aimCamera(state, alpha);
    updateGround(g_view, state.run.seed, state.run.map, maps::CrushedCars{state.run.crushed, state.run.crushedCount});

    const Vec2 playerWorld = lerp(state.localPlayer().prevPos, state.localPlayer().pos, alpha);
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        const Player& player = state.players[slot];
        updateSoldierAnimation(slot, slot == state.localSlot ? playerWorld : lerp(player.prevPos, player.pos, alpha), GetFrameTime());
    }

    const DayLook look = dayLookAt(dayClock(state));
    const uint32_t tick = state.run.tick;
    const bool flicker = effects::flickerAllowed() && g_quality > 0;

    const int zombies = collectVisibleZombies(state, alpha, look.shadowLength);
    // Every player is a figure in the depth sort. This screen's own soldier is always drawn,
    // and the others when they are near enough to be in the picture.
    int figures = zombies;
    int localFigure = zombies;
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        const Player& player = state.players[slot];
        const Vec2 world = slot == state.localSlot ? playerWorld : lerp(player.prevPos, player.pos, alpha);
        const ScreenPoint feet = project(g_view, world);
        if (slot != state.localSlot && !onTarget(g_view, feet, 40.0f, 60.0f, 40.0f)) continue;
        if (slot == state.localSlot) localFigure = figures;
        g_visible[figures].feet = feet;
        g_visible[figures].enemyIndex = static_cast<uint16_t>(kPlayerSlot + slot);
        figures++;
    }
    const VisibleFigure& playerFigure = g_visible[localFigure];
    g_lastPlayerFigures = figures - zombies;
    sortByDepth(figures);

    BeginTextureMode(g_target);
    ClearBackground(maps::fallbackGround(state.run.map));

    drawGround(g_view, look);
    if (g_carCover) drawCoverDepth(g_view);
    g_carCount = carsOnScreen(g_view, g_cars);
    collectCovers();

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
    for (int i = zombies; i < figures; ++i) {
        const int slot = g_visible[i].enemyIndex - kPlayerSlot;
        if (slot == state.localSlot) continue;
        const Player& other = state.players[slot];
        drawTeammateRing(g_view, lerp(other.prevPos, other.pos, alpha), slot);
    }
    endFlat();

    // From here to the end of the figures, a car's pixels hide what stands behind them.
    // Only the cars write depth. Sprites just test against it, and keep their painted order.
    beginSprites();
    rlEnableDepthTest();
    rlDisableDepthMask();
    setSpriteDepth(kDepthNearest);
    drawLightSprites(g_view, look, tick, flicker);
    drawCorpses(g_view, tick);
    setSpriteDepth(kDepthNearest);
    drawGroundSprites(g_view, state, alpha);
    drawSpriteSheet();

    // All shadows go down before any figure, so none falls across a body.
    const bool longShadows = look.shadowLength > 0.02f;
    const int shadowBudget = g_fastMode ? kLongShadowBudgetFast : kLongShadowBudget;
    for (int i = zombies; i < figures; ++i) {
        setSpriteDepth(depthOfRow(g_view, g_visible[i].feet.y));
        drawSoldierShadow(state, g_visible[i].enemyIndex - kPlayerSlot, g_visible[i].feet, look);
    }
    for (int i = 0; i < zombies; ++i) {
        const VisibleFigure& figure = g_visible[i];
        const Enemy& enemy = state.enemies[figure.enemyIndex];
        setSpriteDepth(depthOfRow(g_view, figure.feet.y));
        // In a very big crowd, only figures near the soldier keep the long shadow.
        const float away = std::abs(figure.feet.x - playerFigure.feet.x) + std::abs(figure.feet.y - playerFigure.feet.y);
        const bool wanted = zombies <= shadowBudget || away < kLongShadowNear || enemy.type == EnemyType::Boss;
        if (longShadows && wanted) drawZombieLongShadow(state, enemy, figure.feet, look);
        drawZombieShadow(state, enemy, figure.feet);
    }

    setSpriteDepth(kDepthNearest);
    drawDashTrail(g_view, state, playerWorld);
    g_silhouettesShown = 0;
    for (int i = 0; i < figures; ++i) drawFigure(state, g_visible[g_drawOrder[i]]);
    // He shows faintly through anything standing in front of him, so he can't be lost.
    setSpriteDepth(kDepthNearest);
    drawSoldier(state, state.localSlot, playerFigure.feet, kSoldierSeeThrough);
    endSprites();
    rlEnableDepthMask();
    rlDisableDepthTest();

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
    g_shieldsShown = 0;
    for (int i = zombies; i < figures; ++i) {
        if (drawShieldBubble(state, g_visible[i].enemyIndex - kPlayerSlot, g_visible[i].feet)) g_shieldsShown++;
    }
    if (g_showCarBoxes) drawCarBoxes();
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
    for (int i = zombies; i < figures; ++i) drawMuzzleFlash(state, g_visible[i].enemyIndex - kPlayerSlot, g_visible[i].feet);
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
    g_lastDrawnState = &state;
}

}  // namespace ls::worldview

#ifdef __EMSCRIPTEN__
extern "C" {
// Test hook: what the renderer drew on its last frame, which the simulation can't report.
EMSCRIPTEN_KEEPALIVE const char* ls_query_world() {
    using namespace ls::worldview;
    static char json[1280];
    const SoldierDebug soldier = g_lastDrawnState ? soldierDebug(*g_lastDrawnState) : SoldierDebug{};
    // The car nearest the soldier, whose feet are at the anchor.
    CarOnScreen car{};
    float nearest = 1.0e30f;
    for (int i = 0; i < g_carCount; ++i) {
        const float dx = (g_cars[i].left + g_cars[i].right) * 0.5f - g_view.anchorX;
        const float dy = (g_cars[i].top + g_cars[i].bottom) * 0.5f - g_view.anchorY;
        if (dx * dx + dy * dy < nearest) {
            nearest = dx * dx + dy * dy;
            car = g_cars[i];
        }
    }
    float prop[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    sceneCoverOnScreen(g_view, prop);
    const ScreenPoint boxTopLeft = project(g_view, car.boxMin);
    const ScreenPoint boxBottomRight = project(g_view, car.boxMax);
    std::snprintf(json, sizeof(json),
                  "{\"telegraphing\":%d,\"telegraphsShown\":%d,\"heldGun\":%d,\"muzzleFrames\":%d,\"chunkPaints\":%d,\"cars\":%d,\"crushed\":%d,\"flatSwap\":%d,\"silhouettes\":%d,\"covers\":%d,\"coversDropped\":%d,\"pixelScale\":%.3f,\"car\":[%.1f,%.1f,%.1f,%.1f],\"carBox\":[%.1f,%.1f,%.1f,%.1f],\"worldBox\":[%.1f,%.1f,%.1f,%.1f],\"prop\":[%.1f,%.1f,%.1f,%.1f],\"spot\":[%.1f,%.1f],\"soldiers\":%d,\"shields\":%d}",
                  g_telegraphCount, g_telegraphsShown, soldier.heldGun, soldier.muzzleFrames, groundPaintCount(), g_carCount, g_lastDrawnState ? g_lastDrawnState->run.crushedCount : 0, groundLastSwapSize(), g_silhouettesShown, g_coverCount, coversDropped(),
                  static_cast<double>(g_view.pixelScale), static_cast<double>(car.left), static_cast<double>(car.top), static_cast<double>(car.right), static_cast<double>(car.bottom),
                  static_cast<double>(boxTopLeft.x), static_cast<double>(boxTopLeft.y), static_cast<double>(boxBottomRight.x), static_cast<double>(boxBottomRight.y),
                  static_cast<double>(car.boxMin.x), static_cast<double>(car.boxMin.y), static_cast<double>(car.boxMax.x), static_cast<double>(car.boxMax.y),
                  static_cast<double>(prop[0]), static_cast<double>(prop[1]), static_cast<double>(prop[2]), static_cast<double>(prop[3]),
                  static_cast<double>(prop[4]), static_cast<double>(prop[5]), g_lastPlayerFigures, g_shieldsShown);
    return json;
}
}
#endif
