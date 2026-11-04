#include "components/WorldView/DayCycle.h"

#include "core/Math.h"
#include "data/Theme.h"

namespace ls::worldview {

namespace {

constexpr int kKeyCount = static_cast<int>(sizeof(theme::kDayKeys) / sizeof(theme::kDayKeys[0]));

void setTint(float out[3], Color from, Color to, float t, float range) {
    out[0] = lerpf(static_cast<float>(from.r), static_cast<float>(to.r), t) / 255.0f * range;
    out[1] = lerpf(static_cast<float>(from.g), static_cast<float>(to.g), t) / 255.0f * range;
    out[2] = lerpf(static_cast<float>(from.b), static_cast<float>(to.b), t) / 255.0f * range;
}

}  // namespace

DayLook dayLookAt(float seconds) {
    int next = 1;
    while (next < kKeyCount - 1 && seconds > theme::kDayKeys[next].seconds) next++;
    const theme::DayKey& a = theme::kDayKeys[next - 1];
    const theme::DayKey& b = theme::kDayKeys[next];
    const float t = clampf((seconds - a.seconds) / (b.seconds - a.seconds), 0.0f, 1.0f);

    DayLook look;
    setTint(look.groundTint, a.groundTint, b.groundTint, t, theme::kDayTintRange);
    setTint(look.groundLift, a.groundLift, b.groundLift, t, 1.0f);
    setTint(look.spriteTint, a.spriteTint, b.spriteTint, t, 1.0f);
    look.groundSaturation = lerpf(a.groundSaturation, b.groundSaturation, t);
    look.lampLevel = lerpf(a.lampLevel, b.lampLevel, t);
    look.shadowLength = lerpf(a.shadowLength, b.shadowLength, t);
    look.vignette = lerpf(a.vignette, b.vignette, t);
    return look;
}

}  // namespace ls::worldview
