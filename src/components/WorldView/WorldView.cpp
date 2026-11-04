#include "components/WorldView/WorldView.h"

#include "data/Balance.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

constexpr float kWorldUnitsOnLongSide = 1280.0f;
constexpr float kGridSpacing = 80.0f;
constexpr int kMaxSides = 12;

// Screen edges in world units, for skipping objects that can't be seen.
struct ViewBounds {
    float left, right, top, bottom;
    bool contains(Vec2 pos, float margin) const {
        return pos.x > left - margin && pos.x < right + margin && pos.y > top - margin && pos.y < bottom + margin;
    }
};

ViewBounds viewBounds(const Camera2D& camera) {
    const float halfWidth = static_cast<float>(GetScreenWidth()) * 0.5f / camera.zoom;
    const float halfHeight = static_cast<float>(GetScreenHeight()) * 0.5f / camera.zoom;
    return {camera.target.x - halfWidth, camera.target.x + halfWidth, camera.target.y - halfHeight, camera.target.y + halfHeight};
}

void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color color) {
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex2f(a.x, a.y);
    rlVertex2f(b.x, b.y);
    rlVertex2f(c.x, c.y);
    rlVertex2f(d.x, d.y);
}

void thickLine(Vec2 from, Vec2 to, float width, Color color) {
    const Vec2 dir = normalizedOr(to - from, Vec2{1.0f, 0.0f});
    const Vec2 side = Vec2{-dir.y, dir.x} * (width * 0.5f);
    quad(from + side, from - side, to - side, to + side, color);
}

// Unit shapes, computed once, so that drawing thousands of enemies uses no
// trigonometry per enemy. Each shape is rotated with a direction vector instead.
struct UnitShape {
    Vec2 points[kMaxSides];
    int sides = 0;
};

UnitShape makeRegularShape(int sides) {
    UnitShape shape;
    shape.sides = sides;
    for (int i = 0; i < sides; ++i) {
        shape.points[i] = fromAngle(kTau * static_cast<float>(i) / static_cast<float>(sides));
    }
    return shape;
}

// A filled shape with a bright outline. All of it is drawn as quads, which keeps
// everything in one raylib batch, so the GPU sees few draw calls.
void drawShape(const UnitShape& shape, Vec2 center, float radius, Vec2 heading, Color fill, Color outline, float thickness) {
    Vec2 corners[kMaxSides];
    for (int i = 0; i < shape.sides; ++i) {
        const Vec2 p = shape.points[i];
        corners[i] = center + Vec2{p.x * heading.x - p.y * heading.y, p.x * heading.y + p.y * heading.x} * radius;
    }
    for (int i = 0; i < shape.sides; ++i) {
        const Vec2 a = corners[i];
        const Vec2 b = corners[(i + 1) % shape.sides];
        quad(center, a, b, b, fill);
    }
    for (int i = 0; i < shape.sides; ++i) {
        thickLine(corners[i], corners[(i + 1) % shape.sides], thickness, outline);
    }
}

void drawGrid(const ViewBounds& view, float lineWidth) {
    const float startX = std::floor(view.left / kGridSpacing) * kGridSpacing;
    const float startY = std::floor(view.top / kGridSpacing) * kGridSpacing;
    for (float x = startX; x <= view.right + kGridSpacing; x += kGridSpacing) {
        const bool major = static_cast<int>(std::lround(x / kGridSpacing)) % 5 == 0;
        thickLine(Vec2{x, view.top}, Vec2{x, view.bottom}, lineWidth, major ? theme::kGridLineMajor : theme::kGridLine);
    }
    for (float y = startY; y <= view.bottom + kGridSpacing; y += kGridSpacing) {
        const bool major = static_cast<int>(std::lround(y / kGridSpacing)) % 5 == 0;
        thickLine(Vec2{view.left, y}, Vec2{view.right, y}, lineWidth, major ? theme::kGridLineMajor : theme::kGridLine);
    }
}

}  // namespace

Camera2D makeCamera(const GameState& state, float alpha) {
    const Vec2 center = lerp(state.player.prevPos, state.player.pos, alpha);
    const float longSide = static_cast<float>(std::max(GetScreenWidth(), GetScreenHeight()));
    Camera2D camera{};
    camera.target = Vector2{center.x, center.y};
    camera.offset = Vector2{static_cast<float>(GetScreenWidth()) * 0.5f, static_cast<float>(GetScreenHeight()) * 0.5f};
    camera.zoom = longSide / kWorldUnitsOnLongSide;
    return camera;
}

void draw(const GameState& state, float alpha, const Camera2D& camera) {
    static const UnitShape kTriangle = makeRegularShape(3);
    static const UnitShape kDiamond = makeRegularShape(4);
    static const UnitShape kHexagon = makeRegularShape(6);
    static const UnitShape kRound = makeRegularShape(12);

    const ViewBounds view = viewBounds(camera);
    const float pixel = 1.0f / camera.zoom;

    BeginMode2D(camera);
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);

    drawGrid(view, pixel * 1.5f);

    // Gems are round, and enemies are sharp, so the two differ by shape as well as color.
    for (const Gem& gem : state.gems) {
        const Vec2 pos = lerp(gem.prevPos, gem.pos, alpha);
        if (!view.contains(pos, 12.0f)) continue;
        const bool large = gem.xp >= 5;
        drawShape(kHexagon, pos, large ? 9.0f : 6.0f, Vec2{1.0f, 0.0f}, theme::kGemFill,
                  large ? theme::kGemLarge : theme::kGem, pixel * 2.0f);
    }

    for (const Enemy& enemy : state.enemies) {
        const Vec2 pos = lerp(enemy.prevPos, enemy.pos, alpha);
        if (!view.contains(pos, enemy.radius + 4.0f)) continue;
        const Vec2 heading = normalizedOr(state.player.pos - pos, Vec2{1.0f, 0.0f});
        const Color fill = enemy.flash > 0.0f ? theme::kEnemyHitFlash : theme::kEnemyFill;
        drawShape(kTriangle, pos, enemy.radius * 1.25f, heading, fill, theme::kEnemyDrifter, pixel * 2.5f);
    }

    for (const Projectile& shot : state.projectiles) {
        const Vec2 pos = lerp(shot.prevPos, shot.pos, alpha);
        if (!view.contains(pos, 16.0f)) continue;
        const Vec2 dir = normalizedOr(shot.vel, Vec2{1.0f, 0.0f});
        thickLine(pos - dir * (shot.radius * 1.8f), pos + dir * (shot.radius * 1.8f), shot.radius * 0.9f, theme::kPlayerShot);
    }

    const Vec2 playerPos = lerp(state.player.prevPos, state.player.pos, alpha);
    drawShape(kRound, playerPos, balance::kPlayerRadius, Vec2{1.0f, 0.0f}, theme::kPlayerFill, theme::kPlayer, pixel * 3.0f);
    drawShape(kDiamond, playerPos + state.player.facing * (balance::kPlayerRadius * 0.55f), 4.0f, Vec2{1.0f, 0.0f},
              theme::kPlayer, theme::kPlayer, pixel);

    // Enemy bullets are drawn last, so nothing can hide them.
    for (const EnemyBullet& bullet : state.enemyBullets) {
        const Vec2 pos = lerp(bullet.prevPos, bullet.pos, alpha);
        if (!view.contains(pos, bullet.radius + 4.0f)) continue;
        drawShape(kDiamond, pos, bullet.radius * 1.3f, Vec2{1.0f, 0.0f}, theme::kEnemyBullet, theme::kEnemyBullet, pixel);
        drawShape(kDiamond, pos, bullet.radius * 0.6f, Vec2{1.0f, 0.0f}, theme::kEnemyBulletCore, theme::kEnemyBulletCore, pixel);
    }

    rlEnd();
    rlSetTexture(0);
    EndMode2D();
}

}  // namespace ls::worldview
