#include "components/Ui/Font/Font.h"

#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

// Written by CMake from PixelifySans.ttf. See CMakeLists.txt.
namespace ls::font {
extern const unsigned char kEmbeddedTtf[];
extern const std::size_t kEmbeddedTtfSize;
}

namespace ls::font {

namespace {

// The font is baked at a short list of whole pixel sizes, and text is always drawn
// at one of them, never scaled. Scaling a pixel font blurs its edges or makes its
// pixels uneven, so a requested size snaps to the nearest baked size instead.
// The biggest sizes only hold plain ASCII. They are only needed on big windows (the
// logo and headings scale with the window), and each one is a texture of up to 4 MB, so
// they are baked the first time text of that size is drawn. A phone never pays for them.
constexpr int kBakedSizes[] = {16, 24, 32, 40, 48, 56, 64, 80, 96, 128, 160};
constexpr int kSizeCount = static_cast<int>(sizeof(kBakedSizes) / sizeof(kBakedSizes[0]));
constexpr int kFullSetMaxSize = 64;
constexpr int kMaxGlyphs = 128;

Font g_fonts[kSizeCount];
bool g_loaded[kSizeCount];
bool g_ready = false;

// ASCII plus the few extra marks the UI text uses.
int buildCodepoints(int* out, bool full) {
    int count = 0;
    for (int c = 32; c <= 126; ++c) out[count++] = c;
    if (full) {
        const int extra[] = {0xB7, 0xD7, 0x2013, 0x2014, 0x2022, 0x2026};
        for (int c : extra) out[count++] = c;
    }
    return count;
}

// In this font the digit 5 looks almost the same as a capital S, which is a real
// problem in a game full of numbers. So that one glyph is redrawn from this small
// grid, with a flat top and a square corner, before the font is packed into a texture.
constexpr const char* kFiveRows[] = {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."};
constexpr int kFiveWidth = 5;
constexpr int kFiveHeight = 7;
constexpr int kGlyphPadding = 4;

void redrawFive(GlyphInfo* glyphs, int count) {
    for (int g = 0; g < count; ++g) {
        if (glyphs[g].value != '5') continue;
        const Image& image = glyphs[g].image;
        auto* pixels = static_cast<unsigned char*>(image.data);
        int minX = image.width, minY = image.height, maxX = -1, maxY = -1;
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                if (pixels[y * image.width + x] < 128) continue;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        if (maxX < 0) return;
        // Same size and place as the original digit, so text spacing doesn't change.
        const int width = maxX - minX + 1;
        const int height = maxY - minY + 1;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const char cell = kFiveRows[y * kFiveHeight / height][x * kFiveWidth / width];
                pixels[(minY + y) * image.width + minX + x] = cell == '#' ? 255 : 0;
            }
        }
        return;
    }
}

// Same steps as raylib's LoadFontFromMemory, with one glyph changed before packing.
Font loadFont(int bakedSize, const int* codepoints, int count) {
    Font font{};
    font.baseSize = bakedSize;
    font.glyphCount = count;
    font.glyphPadding = kGlyphPadding;
    font.glyphs = LoadFontData(kEmbeddedTtf, static_cast<int>(kEmbeddedTtfSize), bakedSize, codepoints, count, FONT_DEFAULT, &font.glyphCount);
    if (!font.glyphs) return GetFontDefault();
    redrawFive(font.glyphs, font.glyphCount);
    Image atlas = GenImageFontAtlas(font.glyphs, &font.recs, font.glyphCount, bakedSize, kGlyphPadding, 0);
    font.texture = LoadTextureFromImage(atlas);
    // Drawing only needs the texture, the rectangles and the glyph offsets. The per-glyph
    // pictures are never read again, so they are freed now instead of kept in memory.
    // UnloadFont skips an image with no data, so this is safe.
    for (int i = 0; i < font.glyphCount; ++i) {
        UnloadImage(font.glyphs[i].image);
        font.glyphs[i].image = Image{};
    }
    UnloadImage(atlas);
    return font;
}

// Bakes one size, and gives its texture hard pixel edges.
void bake(int index) {
    int codepoints[kMaxGlyphs];
    const int count = buildCodepoints(codepoints, kBakedSizes[index] <= kFullSetMaxSize);
    g_fonts[index] = loadFont(kBakedSizes[index], codepoints, count);
    // Nearest-neighbor filtering: letters stay hard-edged at any window size.
    SetTextureFilter(g_fonts[index].texture, TEXTURE_FILTER_POINT);
    g_loaded[index] = true;
}

const Font& fontAt(int index) {
    if (!g_loaded[index]) bake(index);
    return g_fonts[index];
}

// The smallest baked size that is at least 88 percent of the request. Leaning up
// instead of down keeps small text from ever dropping under a readable size.
int nearestIndex(float size) {
    for (int i = 0; i < kSizeCount; ++i) {
        if (static_cast<float>(kBakedSizes[i]) >= size * 0.88f) return i;
    }
    return kSizeCount - 1;
}

float alignedX(const char* text, float x, float size, Align align, float tracking) {
    if (align == Align::Left) return x;
    const float width = measure(text, size, tracking);
    return align == Align::Center ? x - width * 0.5f : x - width;
}

// Whole pixels keep glyph edges sharp.
Vector2 snap(float x, float y) { return Vector2{std::floor(x + 0.5f), std::floor(y + 0.5f)}; }

}  // namespace

void init() {
    if (g_ready) return;
    for (int i = 0; i < kSizeCount; ++i) {
        if (kBakedSizes[i] <= kFullSetMaxSize) bake(i);
    }
    g_ready = true;
}

void shutdown() {
    if (!g_ready) return;
    for (int i = 0; i < kSizeCount; ++i) {
        if (!g_loaded[i]) continue;
        UnloadFont(g_fonts[i]);
        g_loaded[i] = false;
    }
    g_ready = false;
}

int memoryBytes() {
    long long total = 0;
    for (int i = 0; i < kSizeCount; ++i) {
        // The atlas is two bytes a pixel: gray and alpha.
        if (g_loaded[i]) total += static_cast<long long>(g_fonts[i].texture.width) * g_fonts[i].texture.height * 2;
    }
    return static_cast<int>(total);
}

float snapSize(float size) { return static_cast<float>(kBakedSizes[nearestIndex(size)]); }

float measure(const char* text, float size, float tracking) {
    if (!g_ready || !text[0]) return 0.0f;
    const int index = nearestIndex(size);
    return MeasureTextEx(fontAt(index), text, static_cast<float>(kBakedSizes[index]), std::floor(tracking)).x;
}

float fitSize(const char* text, float size, float maxWidth, float tracking) {
    int index = nearestIndex(size);
    while (index > 0 && measure(text, static_cast<float>(kBakedSizes[index]), tracking) > maxWidth) --index;
    return static_cast<float>(kBakedSizes[index]);
}

void draw(const char* text, float x, float y, float size, Color color, Align align, float tracking) {
    if (!g_ready || !text[0] || color.a == 0) return;
    const int index = nearestIndex(size);
    const float baked = static_cast<float>(kBakedSizes[index]);
    DrawTextEx(fontAt(index), text, snap(alignedX(text, x, baked, align, tracking), y), baked, std::floor(tracking), color);
}

void drawShadowed(const char* text, float x, float y, float size, Color color, Align align, float tracking) {
    // A hard one-step shadow, like a pixel game's text, with no blur.
    const float offset = std::max(1.0f, std::floor(snapSize(size) / 14.0f));
    Color shadow = theme::kUiShadow;
    shadow.a = static_cast<unsigned char>(static_cast<float>(shadow.a) * static_cast<float>(color.a) / 255.0f);
    draw(text, x + offset, y + offset, size, shadow, align, tracking);
    draw(text, x, y, size, color, align, tracking);
}

void drawMiddle(const char* text, float x, float cy, float size, Color color, Align align, float tracking) {
    // The capitals sit a little below the middle of the line box, so the box is
    // lifted by this share of the size to center the capitals on cy.
    constexpr float kCapCenter = 0.50f;
    draw(text, x, cy - snapSize(size) * kCapCenter, size, color, align, tracking);
}

int wrapped(const char* text, float x, float y, float maxWidth, float size, float lineHeight, Color color, Align align,
            int maxLines, bool drawIt) {
    constexpr int kLineBytes = 160;
    char line[kLineBytes];
    int lines = 0;
    const char* cursor = text;
    while (*cursor && lines < maxLines) {
        // Grow the line one word at a time until the next word would not fit.
        int length = 0;
        const char* scan = cursor;
        while (*scan) {
            const char* wordEnd = scan;
            while (*wordEnd && *wordEnd != ' ') ++wordEnd;
            const int candidate = static_cast<int>(wordEnd - cursor);
            if (candidate >= kLineBytes) break;
            std::memcpy(line, cursor, static_cast<size_t>(candidate));
            line[candidate] = '\0';
            if (length > 0 && measure(line, size) > maxWidth) break;
            length = candidate;
            scan = wordEnd;
            while (*scan == ' ') ++scan;
        }
        if (length == 0) break;
        std::memcpy(line, cursor, static_cast<size_t>(length));
        line[length] = '\0';
        if (drawIt) draw(line, x, y + static_cast<float>(lines) * lineHeight, size, color, align);
        ++lines;
        cursor += length;
        while (*cursor == ' ') ++cursor;
    }
    return lines;
}

}  // namespace ls::font
