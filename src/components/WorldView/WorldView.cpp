#include "components/WorldView/WorldView.h"

#include "components/WorldView/Characters.h"
#include "components/WorldView/Draw.h"
#include "components/WorldView/Objects.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

constexpr float kWorldUnitsOnLongSide = 1280.0f;
constexpr float kFarthestVisibleWorldDistance = balance::kSpawnRadius - 40.0f;
constexpr float kGridSpacing = 80.0f;
constexpr int kGridMajorEvery = 5;
constexpr float kGridMajorGlowPixels = 5.0f;

// Keeps the soldier's middle at the screen center, rather than his feet.
constexpr float kCameraLift = balance::kPlayerRadius * 1.5f;

// Level of detail. With few zombies all get the full figure. Above that, only the
// nearest ones do, as nobody can see the extra quads on a far figure in a crowd.
constexpr int kAllDetailedUpTo = 400;
constexpr int kNearestDetailed = 300;
constexpr int kNearestDetailedFast = 120;
constexpr int kDistanceBuckets = 128;

// Depth sort. Sorting by a coarse bucket is enough: figures in the same bucket
// are at almost the same height on screen, so their order can't be noticed.
constexpr int kSortBuckets = 512;

// The player takes one extra slot in the sort, marked with this stand-in index.
constexpr int kPlayerSlot = kMaxEnemies;

struct VisibleFigure {
    Vec2 feet;           // Ground position, after the tilt.
    float distanceSq;    // From the player, in view units.
    uint16_t enemyIndex;
    bool detailed;
};

// Fixed memory, so that drawing never touches the heap.
VisibleFigure g_visible[kMaxEnemies + 1];
uint16_t g_drawOrder[kMaxEnemies + 1];
int g_bucketStarts[kSortBuckets + 1];
int g_distanceCounts[kDistanceBuckets];

bool g_fastMode = false;

ViewBounds viewBounds(const Camera2D& camera) {
    const float halfWidth = static_cast<float>(GetScreenWidth()) * 0.5f / camera.zoom;
    const float halfHeight = static_cast<float>(GetScreenHeight()) * 0.5f / camera.zoom;
    return {camera.target.x - halfWidth, camera.target.x + halfWidth, camera.target.y - halfHeight, camera.target.y + halfHeight};
}

void drawGrid(const ViewBounds& view, float pixel) {
    // Lines of constant y get closer together under the tilt, so the world y of the
    // first line comes from undoing the tilt on the screen's top edge.
    const float spacingY = kGridSpacing * kTilt;
    const int firstX = static_cast<int>(std::floor(view.left / kGridSpacing));
    const int lastX = static_cast<int>(std::ceil(view.right / kGridSpacing));
    const int firstY = static_cast<int>(std::floor(view.top / spacingY));
    const int lastY = static_cast<int>(std::ceil(view.bottom / spacingY));
    const float width = pixel * 1.5f;
    const float glow = pixel * kGridMajorGlowPixels;

    const auto isMajor = [](int line) { return line % kGridMajorEvery == 0; };

    for (int i = firstX; i <= lastX; ++i) {
        const float x = static_cast<float>(i) * kGridSpacing;
        if (isMajor(i)) thickLine(Vec2{x, view.top}, Vec2{x, view.bottom}, glow, theme::kGridGlow);
        thickLine(Vec2{x, view.top}, Vec2{x, view.bottom}, width, isMajor(i) ? theme::kGridLineMajor : theme::kGridLine);
    }
    for (int i = firstY; i <= lastY; ++i) {
        const float y = static_cast<float>(i) * spacingY;
        if (isMajor(i)) thickLine(Vec2{view.left, y}, Vec2{view.right, y}, glow, theme::kGridGlow);
        thickLine(Vec2{view.left, y}, Vec2{view.right, y}, width, isMajor(i) ? theme::kGridLineMajor : theme::kGridLine);
    }
}

// Finds every zombie on screen. Returns how many, not counting the player.
int collectVisibleZombies(const GameState& state, float alpha, const ViewBounds& view, Vec2 playerFeet) {
    int visible = 0;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        const Vec2 feet = project(lerp(enemy.prevPos, enemy.pos, alpha));
        if (!view.contains(feet, figureHalfWidth(enemy), figureHeight(enemy), enemy.radius)) continue;
        VisibleFigure& figure = g_visible[visible++];
        figure.feet = feet;
        figure.distanceSq = distanceSq(feet, playerFeet);
        figure.enemyIndex = static_cast<uint16_t>(i);
        figure.detailed = true;
    }
    return visible;
}

// Picks who gets the full figure. A histogram of distances finds the cut-off in one
// pass, so there is no need to fully sort thousands of zombies by distance.
void chooseDetail(const GameState& state, int visible, const ViewBounds& view) {
    const int budget = g_fastMode ? kNearestDetailedFast : kNearestDetailed;
    if (visible <= kAllDetailedUpTo && !g_fastMode) return;
    if (visible <= budget) return;

    const float width = view.right - view.left;
    const float height = view.bottom - view.top;
    // Squared distance makes each bucket cover the same screen area.
    const float largest = (width * width + height * height) * 0.25f + 1.0f;
    const float toBucket = static_cast<float>(kDistanceBuckets) / largest;

    std::fill(g_distanceCounts, g_distanceCounts + kDistanceBuckets, 0);
    for (int i = 0; i < visible; ++i) {
        const int bucket = std::min(kDistanceBuckets - 1, static_cast<int>(g_visible[i].distanceSq * toBucket));
        g_distanceCounts[bucket]++;
    }
    int total = 0;
    int cutoff = 0;
    while (cutoff < kDistanceBuckets && total + g_distanceCounts[cutoff] <= budget) total += g_distanceCounts[cutoff++];
    const float cutoffSq = static_cast<float>(cutoff) / toBucket;

    for (int i = 0; i < visible; ++i) {
        VisibleFigure& figure = g_visible[i];
        figure.detailed = figure.distanceSq < cutoffSq || mustStayDetailed(state.enemies[figure.enemyIndex]);
    }
}

// Counting sort by screen height: things further up the screen come first, so
// nearer figures draw over them. It runs in linear time and uses only fixed arrays.
void sortByDepth(int count, const ViewBounds& view) {
    const float top = view.top - 200.0f;
    const float range = view.bottom + 400.0f - top;
    const float toBucket = static_cast<float>(kSortBuckets) / range;
    const auto bucketOf = [&](const VisibleFigure& figure) {
        return std::clamp(static_cast<int>((figure.feet.y - top) * toBucket), 0, kSortBuckets - 1);
    };

    std::fill(g_bucketStarts, g_bucketStarts + kSortBuckets + 1, 0);
    for (int i = 0; i < count; ++i) g_bucketStarts[bucketOf(g_visible[i]) + 1]++;
    for (int i = 0; i < kSortBuckets; ++i) g_bucketStarts[i + 1] += g_bucketStarts[i];
    for (int i = 0; i < count; ++i) g_drawOrder[g_bucketStarts[bucketOf(g_visible[i])]++] = static_cast<uint16_t>(i);
}

}  // namespace

void setFastMode(bool enabled) { g_fastMode = enabled; }

void consumeEvents(const GameState& state) {
    for (const SimEvent& event : state.events) {
        if (event.type == EventType::WeaponFired) noteWeaponFired(static_cast<WeaponId>(event.detail));
    }
}

Camera2D makeCamera(const GameState& state, float alpha) {
    const Vec2 center = project(lerp(state.player.prevPos, state.player.pos, alpha));
    const float longSide = static_cast<float>(std::max(GetScreenWidth(), GetScreenHeight()));
    Camera2D camera{};
    camera.target = Vector2{center.x, center.y - kCameraLift};
    camera.offset = Vector2{static_cast<float>(GetScreenWidth()) * 0.5f, static_cast<float>(GetScreenHeight()) * 0.5f};
    camera.zoom = longSide / kWorldUnitsOnLongSide;
    // Zombies spawn on a circle around the player. In a near-square window the corners
    // would reach past that circle, so zoom in just enough to keep spawns off screen.
    const float halfWidth = static_cast<float>(GetScreenWidth()) * 0.5f / camera.zoom;
    const float halfHeight = static_cast<float>(GetScreenHeight()) * 0.5f / camera.zoom / kTilt;
    const float cornerDistance = std::sqrt(halfWidth * halfWidth + halfHeight * halfHeight);
    if (cornerDistance > kFarthestVisibleWorldDistance) camera.zoom *= cornerDistance / kFarthestVisibleWorldDistance;
    return camera;
}

void draw(const GameState& state, float alpha, const Camera2D& camera) {
    const ViewBounds view = viewBounds(camera);
    const float pixel = 1.0f / camera.zoom;
    const Vec2 playerWorld = lerp(state.player.prevPos, state.player.pos, alpha);
    const Vec2 playerFeet = project(playerWorld);

    updateSoldierAnimation(state, playerWorld, GetFrameTime());

    const int zombies = collectVisibleZombies(state, alpha, view, playerFeet);
    chooseDetail(state, zombies, view);
    VisibleFigure& playerFigure = g_visible[zombies];
    playerFigure.feet = playerFeet;
    playerFigure.distanceSq = 0.0f;
    playerFigure.enemyIndex = static_cast<uint16_t>(kPlayerSlot);
    playerFigure.detailed = true;
    const int figures = zombies + 1;
    sortByDepth(figures, view);

    BeginMode2D(camera);
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);

    // Flat things on the ground first, so characters always stand on top of them.
    drawGrid(view, pixel);
    drawGroundZones(state, view);
    drawMines(state, alpha, view);
    drawGems(state, alpha, view);
    drawPickups(state, view);

    // All shadows go down before any figure, so none falls across a body.
    for (int i = 0; i < figures; ++i) {
        const VisibleFigure& figure = g_visible[i];
        if (figure.enemyIndex == kPlayerSlot) drawSoldierShadow(figure.feet);
        else drawZombieShadow(state.enemies[figure.enemyIndex], figure.feet, figure.detailed);
    }

    for (int i = 0; i < figures; ++i) {
        const VisibleFigure& figure = g_visible[g_drawOrder[i]];
        if (figure.enemyIndex == kPlayerSlot) drawSoldier(state, figure.feet);
        else drawZombie(state, state.enemies[figure.enemyIndex], figure.feet, figure.detailed);
    }

    for (int i = 0; i < zombies; ++i) {
        const VisibleFigure& figure = g_visible[i];
        const Enemy& enemy = state.enemies[figure.enemyIndex];
        if (enemy.elite || enemy.state != 0) drawZombieOverlay(state, enemy, figure.feet);
    }

    drawBlades(state, alpha);
    drawPlayerShots(state, alpha, view);
    drawBeams(state, pixel);
    drawMuzzleFlash(state, playerFeet);

    // Enemy bullets are drawn last, so nothing can hide them.
    drawEnemyBullets(state, alpha, view);

    rlEnd();
    rlSetTexture(0);
    EndMode2D();
}

}  // namespace ls::worldview
