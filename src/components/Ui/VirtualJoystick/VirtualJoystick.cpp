#include "components/Ui/VirtualJoystick/VirtualJoystick.h"

#include "components/Ui/Widgets/Widgets.h"
#include "core/Math.h"
#include "data/Theme.h"

#include <algorithm>

namespace ls::ui {

namespace {
constexpr float kStickMargin = 24.0f;       // From the safe area's corner, in design units.
constexpr float kStickIdleStrength = 0.55f;  // How visible the stick is with no thumb on it.
}  // namespace

Vec2 joystickHome() {
    const Layout& l = layout();
    const float radius = input::joystickRadius();
    const float margin = kStickMargin * l.u;
    return Vec2{l.left + margin + radius, l.bottom - margin - radius};
}

void drawVirtualJoystick(const input::Snapshot& snapshot) {
    const float radius = input::joystickRadius();
    const float p = layout().px;
    const Vec2 origin = joystickHome();
    registerExtent("joystick", Rectangle{origin.x - radius, origin.y - radius, radius * 2.0f, radius * 2.0f});
    // Fainter when not in use, because it sits over the world all the time.
    const float strength = snapshot.joystickActive ? 1.0f : kStickIdleStrength;

    // The knob follows the thumb, but never leaves the base. With no thumb it rests in the middle.
    Vec2 offset = snapshot.joystickActive ? snapshot.joystickKnob - origin : Vec2{};
    const float len = length(offset);
    if (len > radius) offset = offset * (radius / len);

    // Translucent, so the world stays visible under the thumb. Built from square
    // pixels like everything else: a disc, a ring, four notches and a knob.
    pixelDisc(origin.x, origin.y, radius, withAlpha(theme::kUiWeaponFill, 0.5f * strength));
    pixelRing(origin.x, origin.y, radius, p * 2.0f, withAlpha(theme::kJoystick, 0.6f * strength));
    for (int i = 0; i < 4; ++i) {
        const float dx = i == 0 ? 1.0f : (i == 2 ? -1.0f : 0.0f);
        const float dy = i == 1 ? 1.0f : (i == 3 ? -1.0f : 0.0f);
        pixelRect(Rectangle{origin.x + dx * radius * 0.78f - p, origin.y + dy * radius * 0.78f - p, p * 2.0f, p * 2.0f}, withAlpha(theme::kJoystick, 0.5f * strength));
    }
    const float knobRadius = radius * 0.42f;
    pixelDisc(origin.x + offset.x, origin.y + offset.y, knobRadius, withAlpha(theme::kJoystick, 0.6f * strength));
    pixelRing(origin.x + offset.x, origin.y + offset.y, knobRadius, p * 2.0f, withAlpha(theme::kUiWeaponLight, 0.85f * strength));
}

}  // namespace ls::ui
