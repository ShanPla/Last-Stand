#pragma once

// The time of day, worked out only from how long the run has lasted. Nothing about it
// needs saving in the simulation, and it can't change the outcome of a run.
namespace ls::worldview {

struct DayLook {
    float groundTint[3];
    float groundSaturation;
    float groundLift[3];
    float spriteTint[3];
    float lampLevel;
    float shadowLength;
    float vignette;
};

DayLook dayLookAt(float seconds);

}  // namespace ls::worldview
