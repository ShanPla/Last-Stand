#include "components/WorldView/Shaders.h"

#include "data/Theme.h"

#include "rlgl.h"

namespace ls::worldview {

namespace {

// WebGL2 needs GLSL ES 3.00, and the native build uses desktop GLSL 3.30. The language
// is otherwise the same here, so only the first line changes.
#ifdef __EMSCRIPTEN__
#define LS_GLSL_HEADER "#version 300 es\nprecision highp float;\nprecision highp int;\n"
#else
#define LS_GLSL_HEADER "#version 330\n"
#endif

constexpr const char* kSpriteVertex = LS_GLSL_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 vUv;
out vec4 vInfo;
void main() {
    vUv = vertexTexCoord;
    vInfo = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// The atlas holds palette positions. The vertex color says which palette row to use,
// how strongly the time of day tints this sprite, and how see-through it is. Its third
// value is the hit flash: how far the figure mixes toward the flash color. The outline
// stays dark, so a flashing figure in a crowd keeps its shape. The very top value of
// that channel marks a light pool, whose strength has to go into its color, as
// multiplying blends can't use the alpha.
constexpr const char* kPaletteFragment = LS_GLSL_HEADER R"(
in vec2 vUv;
in vec4 vInfo;
out vec4 fragColor;
uniform sampler2D texture0;
uniform sampler2D uPalette;
uniform vec3 uTint;
uniform vec3 uFlash;
void main() {
    int column = int(texture(texture0, vUv).r * 255.0 + 0.5);
    int row = int(vInfo.r * 255.0 + 0.5);
    vec4 color = texelFetch(uPalette, ivec2(column, row), 0);
    color.rgb = mix(color.rgb, color.rgb * uTint, vInfo.g);
    float pool = step(0.998, vInfo.b);
    float flash = (1.0 - pool) * vInfo.b * step(1.5, float(column));
    color.rgb = mix(color.rgb, uFlash, flash);
    color.rgb *= 1.0 - pool * (1.0 - vInfo.a);
    fragColor = vec4(color.rgb, color.a * vInfo.a);
}
)";

// The ground's positions are world units from the camera's focus. A real perspective
// matrix means the GPU corrects the texture across each chunk, with no subdividing.
constexpr const char* kGroundVertex = LS_GLSL_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 uProjection;
out vec2 vUv;
void main() {
    vUv = vertexTexCoord;
    gl_Position = uProjection * vec4(vertexPosition.xy, 0.0, 1.0);
}
)";

// The strong part of the day cycle. Only the ground goes through this, so characters
// keep their own colors and stay readable at the darkest moment.
constexpr const char* kGroundFragment = LS_GLSL_HEADER R"(
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D texture0;
uniform vec3 uTint;
uniform float uSaturation;
uniform vec3 uLift;
uniform vec3 uStandTint;
uniform float uStandSaturation;
void main() {
    vec4 texel = texture(texture0, vUv);
    // Anything standing, a car or a prop, has its alpha under 255. It is graded between the
    // ground and the characters, so at night it still stands out from the dark ground.
    float standing = 1.0 - step(254.5 / 255.0, texel.a);
    vec3 tint = mix(uTint, uStandTint, standing);
    float saturation = mix(uSaturation, uStandSaturation, standing);
    float gray = dot(texel.rgb, vec3(0.30, 0.59, 0.11));
    vec3 color = mix(vec3(gray), texel.rgb, saturation) * tint + uLift;
    fragColor = vec4(color, 1.0);
}
)";

// A car is part of the painted ground, so it can't be drawn in front of a zombie. Instead its
// pixels go into the depth buffer at the depth of the car's near edge, and a zombie further
// back then fails the depth test on them. Its position works like the ground's, with the
// depth carried in z. Multiplying by w cancels the divide the GPU does afterward, so the
// depth stays the same across the whole car, even with the perspective.
constexpr const char* kCarDepthVertex = LS_GLSL_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 uProjection;
out vec2 vUv;
void main() {
    vUv = vertexTexCoord;
    gl_Position = uProjection * vec4(vertexPosition.xy, 0.0, 1.0);
    gl_Position.z = (1.0 - 2.0 * vertexPosition.z) * gl_Position.w;
}
)";

// The painters leave the alpha of a car's pixels at zero. Everything else is thrown away.
constexpr const char* kCarDepthFragment = LS_GLSL_HEADER R"(
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D texture0;
void main() {
    if (texture(texture0, vUv).a > 0.5 / 255.0) discard;
    fragColor = vec4(0.0);
}
)";

// A standing object, like a lamp or a wall, is part of the painted ground too. Each of its
// pixels keeps in its alpha how many rows below it the object's foot is. This works out where
// that foot lands on screen, the same way project() does, and writes the depth a figure
// standing there would have. So a pole hides a zombie behind it, and one in front draws over
// it, pixel by pixel, with no list of objects needed on the GPU.
constexpr const char* kPropDepthVertex = LS_GLSL_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 uProjection;
out vec2 vUv;
out float vDy;
void main() {
    vUv = vertexTexCoord;
    vDy = vertexPosition.y;
    gl_Position = uProjection * vec4(vertexPosition.xy, 0.0, 1.0);
}
)";

// uView: anchor row, target height, world units per texel row, texel rows per chunk.
// uCamera: rows per world unit at the anchor, perspective, nearest depth, depth margin.
constexpr const char* kPropDepthFragment = LS_GLSL_HEADER R"(
in vec2 vUv;
in float vDy;
out vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 uCamera;
uniform vec4 uView;
void main() {
    int mark = int(texture(texture0, vUv).a * 255.0 + 0.5);
    if (mark == 0 || mark == 255) discard;
    // The row of the texel the mark was read from. A half-size texture holds every other
    // row, so the row is counted in its own texels and then scaled back up.
    float texelRows = float(textureSize(texture0, 0).y);
    float row = floor(vUv.y * texelRows) * (uView.w / texelRows);
    float chunkTop = vDy - vUv.y * uView.w * uView.z;
    float footDy = chunkTop + (row + float(mark - 1) + 0.5) * uView.z;
    float depth = max(1.0 - uCamera.y * footDy, uCamera.z);
    float footRow = uView.x + uCamera.x * footDy / depth;
    float d = clamp((footRow + uCamera.w) / (uView.y + 2.0 * uCamera.w), 0.001, 0.999);
    gl_FragDepth = 1.0 - d;
    fragColor = vec4(0.0);
}
)";

constexpr const char* kPresentVertex = LS_GLSL_HEADER R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 vUv;
void main() {
    vUv = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Scales the low-resolution picture up. Inside an art pixel it reads one flat color,
// and it only blends over the last screen pixel at an edge. At a whole number scale
// that is exact, and at any other scale every art pixel still looks the same size.
constexpr const char* kPresentFragment = LS_GLSL_HEADER R"(
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uTextureSize;
uniform float uPixelScale;
uniform float uVignette;
void main() {
    vec2 texel = vUv * uTextureSize;
    vec2 edge = floor(texel + 0.5);
    vec2 offset = clamp((texel - edge) * uPixelScale, -0.5, 0.5);
    vec3 color = texture(texture0, (edge + offset) / uTextureSize).rgb;
    // Worked out once per art pixel, so even the darkened corners stay in flat blocks.
    vec2 centered = (floor(texel) + 0.5) / uTextureSize * 2.0 - 1.0;
    float corner = dot(centered, centered) * 0.5;
    color *= 1.0 - uVignette * corner * corner;
    fragColor = vec4(color, 1.0);
}
)";

PaletteShader g_palette;
GroundShader g_ground;
CarDepthShader g_carDepth;
PropDepthShader g_propDepth;
PresentShader g_present;

}  // namespace

void loadShaders() {
    g_palette.shader = LoadShaderFromMemory(kSpriteVertex, kPaletteFragment);
    g_palette.tint = GetShaderLocation(g_palette.shader, "uTint");
    // The palette always lives in texture unit 1. raylib's batches only touch unit 0
    // after this, so it needs binding once a frame and never in the middle of a pass.
    const int unit = 1;
    SetShaderValue(g_palette.shader, GetShaderLocation(g_palette.shader, "uPalette"), &unit, SHADER_UNIFORM_INT);
    const float flash[3] = {static_cast<float>(theme::kFxHitFlash.r) / 255.0f, static_cast<float>(theme::kFxHitFlash.g) / 255.0f, static_cast<float>(theme::kFxHitFlash.b) / 255.0f};
    SetShaderValue(g_palette.shader, GetShaderLocation(g_palette.shader, "uFlash"), flash, SHADER_UNIFORM_VEC3);

    g_ground.shader = LoadShaderFromMemory(kGroundVertex, kGroundFragment);
    g_ground.projection = GetShaderLocation(g_ground.shader, "uProjection");
    g_ground.tint = GetShaderLocation(g_ground.shader, "uTint");
    g_ground.saturation = GetShaderLocation(g_ground.shader, "uSaturation");
    g_ground.lift = GetShaderLocation(g_ground.shader, "uLift");
    g_ground.standTint = GetShaderLocation(g_ground.shader, "uStandTint");
    g_ground.standSaturation = GetShaderLocation(g_ground.shader, "uStandSaturation");

    g_carDepth.shader = LoadShaderFromMemory(kCarDepthVertex, kCarDepthFragment);
    g_carDepth.projection = GetShaderLocation(g_carDepth.shader, "uProjection");

    g_propDepth.shader = LoadShaderFromMemory(kPropDepthVertex, kPropDepthFragment);
    g_propDepth.projection = GetShaderLocation(g_propDepth.shader, "uProjection");
    g_propDepth.camera = GetShaderLocation(g_propDepth.shader, "uCamera");
    g_propDepth.view = GetShaderLocation(g_propDepth.shader, "uView");

    g_present.shader = LoadShaderFromMemory(kPresentVertex, kPresentFragment);
    g_present.textureSize = GetShaderLocation(g_present.shader, "uTextureSize");
    g_present.pixelScale = GetShaderLocation(g_present.shader, "uPixelScale");
    g_present.vignette = GetShaderLocation(g_present.shader, "uVignette");
}

const PaletteShader& paletteShader() { return g_palette; }
const GroundShader& groundShader() { return g_ground; }
const CarDepthShader& carDepthShader() { return g_carDepth; }
const PropDepthShader& propDepthShader() { return g_propDepth; }
const PresentShader& presentShader() { return g_present; }

}  // namespace ls::worldview
