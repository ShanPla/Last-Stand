#include "components/Ui/VirtualJoystick/VirtualJoystick.h"

#include "components/Ui/Widgets/Widgets.h"
#include "core/Math.h"
#include "data/Theme.h"

#include <algorithm>

namespace ls::ui {

void drawVirtualJoystick(const input::Snapshot& snapshot) {
    if (!snapshot.joystickActive) return;
    const float radius = input::joystickRadius();
    const float p = layout().px;
    const Vec2 origin = snapshot.joystickOrigin;

    // The knob follows the thumb, but never leaves the base.
    Vec2 offset = snapshot.joystickKnob - origin;
    const float len = length(offset);
    if (len > radius) offset = offset * (radius / len);

    // Translucent, so the world stays visible under the thumb. Built from square
    // pixels like everything else: a disc, a ring, four notches and a knob.
    pixelDisc(origin.x, origin.y, radius, withAlpha(theme::kUiWeaponFill, 0.5f));
    pixelRing(origin.x, origin.y, radius, p * 2.0f, withAlpha(theme::kJoystick, 0.6f));
    for (int i = 0; i < 4; ++i) {
        const float dx = i == 0 ? 1.0f : (i == 2 ? -1.0f : 0.0f);
        const float dy = i == 1 ? 1.0f : (i == 3 ? -1.0f : 0.0f);
        pixelRect(Rectangle{origin.x + dx * radius * 0.78f - p, origin.y + dy * radius * 0.78f - p, p * 2.0f, p * 2.0f}, withAlpha(theme::kJoystick, 0.5f));
    }
    const float knobRadius = radius * 0.42f;
    pixelDisc(origin.x + offset.x, origin.y + offset.y, knobRadius, withAlpha(theme::kJoystick, 0.6f));
    pixelRing(origin.x + offset.x, origin.y + offset.y, knobRadius, p * 2.0f, withAlpha(theme::kUiWeaponLight, 0.85f));
}

}  // namespace ls::ui
