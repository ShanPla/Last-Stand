#pragma once

#include "raylib.h"

// The one bright layer. Each glow is a small additive quad whose falloff is worked out
// in a shader, in a few hard bands with a checker pattern between them. That costs one
// draw call and no extra picture, and it reads as pixel art instead of a smooth blur.
namespace ls::effects {

// Compiles the shader. Needs a live graphics context.
void loadGlow();

// Queues one glow, in art pixels, for this frame. Quietly ignored once the list is full.
void queueGlow(float x, float y, float radius, Color color, float strength);

// Draws everything queued, then empties the list. Returns how many were drawn.
int flushGlow();

}  // namespace ls::effects
