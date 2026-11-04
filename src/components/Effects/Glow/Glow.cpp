#include "components/Effects/Glow/Glow.h"

#include "rlgl.h"

#include <algorithm>

namespace ls::effects {

namespace {

// WebGL2 needs GLSL ES 3.00, and the native build uses desktop GLSL 3.30.
#ifdef __EMSCRIPTEN__
#define LS_GLOW_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define LS_GLOW_HEADER "#version 330\n"
#endif

constexpr const char* kGlowVertex = LS_GLOW_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 vSpot;
out vec4 vColor;
void main() {
    vSpot = vertexTexCoord;
    vColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// vSpot runs from -1 to 1 across the quad. The falloff is cut into four hard bands, and
// a checker at each band's edge is how pixel art draws a gradient. gl_FragCoord is one
// art pixel here, as this draws into the low-resolution picture.
constexpr const char* kGlowFragment = LS_GLOW_HEADER R"(
in vec2 vSpot;
in vec4 vColor;
out vec4 fragColor;
void main() {
    float falloff = max(0.0, 1.0 - length(vSpot));
    falloff *= falloff;
    float checker = mod(floor(gl_FragCoord.x) + floor(gl_FragCoord.y), 2.0) * 0.5;
    float band = floor(falloff * 4.0 + checker) * 0.25;
    fragColor = vec4(vColor.rgb, vColor.a * band);
}
)";

struct GlowSpot {
    float x, y, radius;
    Color color;
};

// Plenty for a real fight: a stress test's thousands of shots are thinned before here.
constexpr int kMaxGlows = 480;
GlowSpot g_spots[kMaxGlows];
int g_count = 0;
Shader g_shader{};

}  // namespace

void loadGlow() {
    if (g_shader.id == 0) g_shader = LoadShaderFromMemory(kGlowVertex, kGlowFragment);
}

void queueGlow(float x, float y, float radius, Color color, float strength) {
    if (g_count >= kMaxGlows || strength <= 0.01f || radius < 1.0f) return;
    GlowSpot& spot = g_spots[g_count++];
    spot.x = x;
    spot.y = y;
    spot.radius = radius;
    spot.color = color;
    spot.color.a = static_cast<unsigned char>(255.0f * std::min(1.0f, strength));
}

int flushGlow() {
    const int drawn = g_count;
    if (drawn == 0) return 0;
    rlSetBlendMode(RL_BLEND_ADDITIVE);
    BeginShaderMode(g_shader);
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (int i = 0; i < drawn; ++i) {
        const GlowSpot& spot = g_spots[i];
        // Whole art pixels, so a moving glow keeps the same pattern as it travels.
        const float cx = static_cast<float>(static_cast<int>(spot.x + 10000.5f) - 10000);
        const float cy = static_cast<float>(static_cast<int>(spot.y + 10000.5f) - 10000);
        rlColor4ub(spot.color.r, spot.color.g, spot.color.b, spot.color.a);
        rlTexCoord2f(-1.0f, -1.0f);
        rlVertex2f(cx - spot.radius, cy - spot.radius);
        rlTexCoord2f(-1.0f, 1.0f);
        rlVertex2f(cx - spot.radius, cy + spot.radius);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex2f(cx + spot.radius, cy + spot.radius);
        rlTexCoord2f(1.0f, -1.0f);
        rlVertex2f(cx + spot.radius, cy - spot.radius);
    }
    rlEnd();
    rlSetTexture(0);
    EndShaderMode();
    rlSetBlendMode(RL_BLEND_ALPHA);
    g_count = 0;
    return drawn;
}

}  // namespace ls::effects
