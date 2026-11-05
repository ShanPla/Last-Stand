#pragma once

#include "raylib.h"

// The game's only text API. The font is compiled into the program, so nothing is
// downloaded. Sizes are in drawing buffer pixels.
namespace ls::font {

enum class Align { Left, Center, Right };

// Builds the glyph atlases. Call once, after the window exists.
void init();
void shutdown();

// Bytes of glyph texture held right now. Big sizes are baked when first used, so this
// grows with the window size. Shown to the browser tests, which check it stays small.
int memoryBytes();

// y is the top of the text line. tracking adds space between letters, in pixels.
void draw(const char* text, float x, float y, float size, Color color, Align align = Align::Left, float tracking = 0.0f);

// The same with a soft dark shadow, for text drawn over the game world.
void drawShadowed(const char* text, float x, float y, float size, Color color, Align align = Align::Left, float tracking = 0.0f);

// Draws text with its capital letters centered on cy, which lines up with icons and bars.
void drawMiddle(const char* text, float x, float cy, float size, Color color, Align align = Align::Left, float tracking = 0.0f);

float measure(const char* text, float size, float tracking = 0.0f);

// The baked pixel size nearest to size. Text is only ever drawn at baked sizes.
float snapSize(float size);

// Shrinks the size until the text fits maxWidth, and returns the size used.
float fitSize(const char* text, float size, float maxWidth, float tracking = 0.0f);

// Word-wrapped text. Returns the number of lines, and draws only when draw is true,
// so the same call measures and draws. Lines are lineHeight apart, starting at y.
int wrapped(const char* text, float x, float y, float maxWidth, float size, float lineHeight, Color color,
            Align align, int maxLines, bool draw);

}  // namespace ls::font
