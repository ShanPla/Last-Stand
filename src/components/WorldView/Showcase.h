#pragma once

#include "components/WorldView/Projection.h"

namespace ls::worldview {

// Debug only: shows pages of the sprite atlas when the showcase asks for them. Does
// nothing the rest of the time. Must run inside the palette shader's sprite pass.
void drawSpriteSheet();

// The camera as last drawn, so a test scene can stand things just outside the picture.
const View& currentView();

}  // namespace ls::worldview
