#pragma once

#include "components/WorldView/Rig.h"

#include <cstdint>

// Every zombie pose is built once at startup, then only looked up. Drawing a
// zombie is then a table lookup and a scale, with no trigonometry per zombie,
// which is what makes thousands of animated figures cheap.
namespace ls::worldview {

enum class FigureId : uint8_t {
    Walker, Runner, Brute, Spitter, Bloater, Crawler,
    Charger, ChargerBrace, ChargerDash, ChargerRecover,
    BossPhase1, BossPhase2, BossPhase3,
    Count,
};

inline constexpr int kFigureFrames = 16;  // Poses in one walk cycle.

struct FigureFrame {
    const FigureQuad* quads;
    int count;
};

// One pose. detailed picks the full figure, or the cheap one for far-off crowds.
// Figure units are multiples of the enemy's radius.
FigureFrame figureFrame(FigureId id, bool detailed, int frame);

}  // namespace ls::worldview
