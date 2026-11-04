#include "components/Effects/DamageNumbers/DamageNumbers.h"

#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>

namespace ls::effects {

namespace {

// A chunky 5 by 7 digit set, plus K for big totals, drawn at the world's pixel size.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kCellWidth = kGlyphWidth + 2;  // Room for a one pixel outline each side.
constexpr int kCellHeight = kGlyphHeight + 2;
constexpr int kGlyphCount = 11;
constexpr int kGlyphK = 10;
constexpr int kAdvance = kGlyphWidth + 1;    // Outlines overlap, which keeps numbers tight.

constexpr const char* kGlyphs[kGlyphCount][kGlyphHeight] = {
    {".###.", "##.##", "##.##", "##.##", "##.##", "##.##", ".###."},
    {"..##.", ".###.", "..##.", "..##.", "..##.", "..##.", ".####"},
    {".###.", "##.##", "...##", "..##.", ".##..", "##...", "#####"},
    {"####.", "...##", "...##", ".###.", "...##", "...##", "####."},
    {"...##", "..###", ".#.##", "#..##", "#####", "...##", "...##"},
    {"#####", "##...", "####.", "...##", "...##", "##.##", ".###."},
    {".###.", "##...", "##...", "####.", "##.##", "##.##", ".###."},
    {"#####", "...##", "..##.", "..##.", ".##..", ".##..", ".##.."},
    {".###.", "##.##", "##.##", ".###.", "##.##", "##.##", ".###."},
    {".###.", "##.##", "##.##", ".####", "...##", "...##", ".###."},
    {"##.##", "##.##", "####.", "###..", "####.", "##.##", "##.##"},
};

constexpr float kRiseArtPixels = 16.0f;
constexpr float kTelegraphDim = 0.45f;  // Over a telegraph, a number is see-through.

Texture2D g_glyphs{};

Color tierColor(NumberTier tier) {
    if (tier == kTierHuge) return theme::kFxNumberHuge;
    return tier == kTierBig ? theme::kFxNumberBig : theme::kFxNumberSmall;
}

void digitQuad(int glyph, int left, int top, int scale, Color color) {
    const float texWidth = static_cast<float>(kCellWidth * kGlyphCount);
    const float u0 = static_cast<float>(glyph * kCellWidth) / texWidth;
    const float u1 = static_cast<float>((glyph + 1) * kCellWidth) / texWidth;
    const float l = static_cast<float>(left);
    const float t = static_cast<float>(top);
    const float r = l + static_cast<float>(kCellWidth * scale);
    const float b = t + static_cast<float>(kCellHeight * scale);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlTexCoord2f(u0, 0.0f);
    rlVertex2f(l, t);
    rlTexCoord2f(u0, 1.0f);
    rlVertex2f(l, b);
    rlTexCoord2f(u1, 1.0f);
    rlVertex2f(r, b);
    rlTexCoord2f(u1, 0.0f);
    rlVertex2f(r, t);
}

}  // namespace

void loadNumberGlyphs() {
    if (g_glyphs.id != 0) return;
    // White digits and a black outline: the vertex color tints only the white part.
    static Color pixels[kCellWidth * kGlyphCount * kCellHeight];
    const auto set = [](int x, int y, Color color) { pixels[y * kCellWidth * kGlyphCount + x] = color; };
    for (int glyph = 0; glyph < kGlyphCount; ++glyph) {
        const int originX = glyph * kCellWidth + 1;
        for (int y = 0; y < kGlyphHeight; ++y) {
            for (int x = 0; x < kGlyphWidth; ++x) {
                if (kGlyphs[glyph][y][x] != '#') continue;
                set(originX + x - 1, y + 1, theme::kFxNumberEdge);
                set(originX + x + 1, y + 1, theme::kFxNumberEdge);
                set(originX + x, y, theme::kFxNumberEdge);
                set(originX + x, y + 2, theme::kFxNumberEdge);
            }
        }
        for (int y = 0; y < kGlyphHeight; ++y) {
            for (int x = 0; x < kGlyphWidth; ++x) {
                if (kGlyphs[glyph][y][x] == '#') set(originX + x, y + 1, theme::kFxNumberMask);
            }
        }
    }
    Image image{pixels, kCellWidth * kGlyphCount, kCellHeight, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    g_glyphs = LoadTextureFromImage(image);
    SetTextureFilter(g_glyphs, TEXTURE_FILTER_POINT);
}

void drawNumberPool(const View& view, const DamageNumberPool& pool, bool rise, bool (*overTelegraph)(Vec2 world)) {
    if (pool.live() == 0) return;
    rlSetTexture(g_glyphs.id);
    rlBegin(RL_QUADS);
    for (const DamageNumber& number : pool) {
        if (!number.used) continue;
        const ScreenPoint ground = project(view, number.world);
        if (!onTarget(view, ground, 40.0f, 80.0f, 10.0f)) continue;

        // Digits, last to first, into a small fixed list. Big totals shorten to "12K".
        int digits[6];
        int digitCount = 0;
        int value = static_cast<int>(number.value + 0.5f);
        if (value >= 10000) {
            digits[digitCount++] = kGlyphK;
            value /= 1000;
        }
        do {
            digits[digitCount++] = value % 10;
            value /= 10;
        } while (value > 0 && digitCount < 6);

        // A big hit jumps to double size for a moment. Double size is the only other
        // size, so the digits keep whole, even pixels.
        const bool popped = number.pop > 0.0f && rise;
        const int scale = (number.tier == kTierHuge || popped) ? 2 : 1;
        const float lift = rise ? kRiseArtPixels * std::min(1.0f, number.total / 0.45f) : 0.0f;
        const int width = digitCount * kAdvance * scale;
        int left = static_cast<int>(ground.x + 0.5f) - width / 2;
        const int top = static_cast<int>(ground.y - (number.height + lift) * ground.scale) - kCellHeight * scale;
        // Keeps it fully on screen when the zombie is at the edge of the view.
        left = std::clamp(left, 1, std::max(1, view.width - width - 1));

        // Solid for most of its life, then a short fade. Fading late keeps it readable.
        const float t = number.age / number.life;
        float alpha = t < 0.65f ? 1.0f : 1.0f - (t - 0.65f) / 0.35f;
        if (overTelegraph(number.world)) alpha *= kTelegraphDim;
        Color color = tierColor(number.tier);
        color.a = static_cast<unsigned char>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
        for (int i = digitCount - 1; i >= 0; --i) {
            digitQuad(digits[i], left, top, scale, color);
            left += kAdvance * scale;
        }
    }
    rlEnd();
    rlSetTexture(0);
}

}  // namespace ls::effects
