#pragma once

#include "raylib.h"

// The three small shaders the world uses, kept here with the code that needs them.
namespace ls::worldview {

struct PaletteShader {
    Shader shader;
    int tint;
};

struct GroundShader {
    Shader shader;
    int projection;
    int tint;
    int saturation;
    int lift;
    int standTint;
    int standSaturation;
};

// Writes a car's pixels into the depth buffer and draws nothing.
struct CarDepthShader {
    Shader shader;
    int projection;
};

// Writes the pixels of standing objects into the depth buffer, each at the depth of its own
// foot, and draws nothing.
struct PropDepthShader {
    Shader shader;
    int projection;
    int camera;
    int view;
};

struct PresentShader {
    Shader shader;
    int textureSize;
    int pixelScale;
    int vignette;
    int screenSize;
};

void loadShaders();
const PaletteShader& paletteShader();
const GroundShader& groundShader();
const CarDepthShader& carDepthShader();
const PropDepthShader& propDepthShader();
const PresentShader& presentShader();

}  // namespace ls::worldview
