#pragma once

#include "core/GameState.h"

#include "raylib.h"

// Icons are drawn with shapes, so the game ships no image files. Each one is built
// to be recognizable at about 24 pixels. size is the width and height of the box,
// and the icon is centered on cx, cy.
namespace ls::ui {

void drawWeaponIcon(WeaponId id, float cx, float cy, float size, Color color);
void drawPassiveIcon(PassiveId id, float cx, float cy, float size, Color color);
void drawPickupIcon(PickupType type, float cx, float cy, float size, Color color);
void drawGemIcon(float cx, float cy, float size, Color color);
void drawDashIcon(float cx, float cy, float size, Color color);

// A pixel zombie head for the title logo. It brings its own colors, so only alpha is given.
void drawZombieIcon(float cx, float cy, float size, float alpha);
// How far right of its center the head's last visible pixel is when drawn at this size.
float zombieIconRightEdge(float size);

}  // namespace ls::ui
