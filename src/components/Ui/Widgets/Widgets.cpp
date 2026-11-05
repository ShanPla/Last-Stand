#include "components/Ui/Widgets/Widgets.h"

#include "components/Audio/Audio.h"
#include "core/Platform.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ls::ui {

namespace {

constexpr int kMaxRegistered = 32;
constexpr float kDesignShortSide = 560.0f;

Layout g_layout;
input::Snapshot g_input;
float g_seconds = 0.0f;
float g_fade = 1.0f;
bool g_inputEnabled = true;
Registered g_registered[kMaxRegistered];
int g_registeredCount = 0;
const void* g_dragging = nullptr;  // The slider value being dragged, or null.
char g_selectedName[24] = "";

float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

// True when the last device shows a highlighted item. Touch has no cursor, so it shows none.
bool showsSelection() { return g_input.lastDevice != input::Device::Touch; }

float px() { return g_layout.px; }

// Snaps a rectangle to the pixel grid, never smaller than one pixel each way.
Rectangle snapRect(Rectangle r) {
    const float p = px();
    const float x0 = std::round(r.x / p) * p;
    const float y0 = std::round(r.y / p) * p;
    const float x1 = std::max(x0 + p, std::round((r.x + r.width) / p) * p);
    const float y1 = std::max(y0 + p, std::round((r.y + r.height) / p) * p);
    return Rectangle{x0, y0, x1 - x0, y1 - y0};
}

void fillRect(float x, float y, float w, float h, Color c) {
    if (w <= 0.0f || h <= 0.0f) return;
    DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), c);
}

// How many pixels are cut off each corner: one step on small shapes, two on big ones.
int cornerSteps(Rectangle r) {
    const float shortSide = std::min(r.width, r.height);
    return shortSide >= 12.0f * px() ? 2 : (shortSide >= 5.0f * px() ? 1 : 0);
}

// A rectangle with stepped corners, drawn as a few flat rows. This is the one
// silhouette every panel, button and bar is made of.
void steppedFill(Rectangle r, int steps, Color c) {
    const float p = px();
    steps = std::min(steps, static_cast<int>(std::min(r.width, r.height) / (2.0f * p)));
    for (int k = 0; k < steps; ++k) {
        const float inset = static_cast<float>(steps - k) * p;
        fillRect(r.x + inset, r.y + static_cast<float>(k) * p, r.width - inset * 2.0f, p, c);
        fillRect(r.x + inset, r.y + r.height - static_cast<float>(k + 1) * p, r.width - inset * 2.0f, p, c);
    }
    fillRect(r.x, r.y + static_cast<float>(steps) * p, r.width, r.height - static_cast<float>(steps) * 2.0f * p, c);
}

Rectangle inset(Rectangle r, float amount) {
    return Rectangle{r.x + amount, r.y + amount, r.width - amount * 2.0f, r.height - amount * 2.0f};
}

}  // namespace

void beginFrame(const input::Snapshot& input, float dtReal) {
    g_input = input;
    g_seconds += dtReal;
    g_registeredCount = 0;
    g_selectedName[0] = '\0';
    g_fade = 1.0f;
    g_inputEnabled = true;

    Layout& l = g_layout;
    l.w = static_cast<float>(GetScreenWidth());
    l.h = static_cast<float>(GetScreenHeight());
    l.u = std::max(0.4f, std::min(l.w, l.h) / kDesignShortSide);
    l.portrait = l.h > l.w;
    l.touch = 44.0f * platform::pixelRatio();
    // One UI pixel is a whole number of screen pixels, about 1/240 of the short side.
    l.px = std::max(2.0f, std::round(std::min(l.w, l.h) / 240.0f));
    const platform::SafeInsets insets = platform::safeInsets();
    const float margin = 16.0f * l.u;
    l.left = insets.left + margin;
    l.top = insets.top + margin;
    l.right = l.w - insets.right - margin;
    l.bottom = l.h - insets.bottom - margin;
    // A broken inset value must never leave no room to draw.
    if (l.right - l.left < l.w * 0.5f) { l.left = margin; l.right = l.w - margin; }
    if (l.bottom - l.top < l.h * 0.5f) { l.top = margin; l.bottom = l.h - margin; }
}

const Layout& layout() { return g_layout; }
const input::Snapshot& input() { return g_input; }
float uiSeconds() { return g_seconds; }

void setFade(float fade) { g_fade = clamp01(fade); }
float fade() { return g_fade; }

Color tint(Color color) {
    color.a = static_cast<unsigned char>(static_cast<float>(color.a) * g_fade);
    return color;
}

Color withAlpha(Color color, float alpha) {
    color.a = static_cast<unsigned char>(static_cast<float>(color.a) * clamp01(alpha));
    return color;
}

void consumePresses() {
    g_input.pointerPressed = false;
    g_input.confirm = false;
    g_input.reroll = false;
    g_input.back = false;
}

void setInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool inputEnabled() { return g_inputEnabled; }

bool showPrompts() { return g_input.lastDevice != input::Device::Touch; }

const char* promptConfirm() { return g_input.lastDevice == input::Device::Gamepad ? "A" : "Enter"; }
const char* promptBack() { return g_input.lastDevice == input::Device::Gamepad ? "B" : "Esc"; }
const char* promptReroll() { return g_input.lastDevice == input::Device::Gamepad ? "Y" : "R"; }
const char* promptDash() { return g_input.lastDevice == input::Device::Gamepad ? "RB" : "Shift"; }
const char* promptPause() { return g_input.lastDevice == input::Device::Gamepad ? "Start" : "Esc"; }

void text(const char* string, float x, float y, float size, Color color, font::Align align, float tracking) {
    font::draw(string, x, y, size, tint(color), align, tracking);
}

void textShadowed(const char* string, float x, float y, float size, Color color, font::Align align, float tracking) {
    font::drawShadowed(string, x, y, size, tint(color), align, tracking);
}

void textMiddle(const char* string, float x, float cy, float size, Color color, font::Align align, float tracking) {
    font::drawMiddle(string, x, cy, size, tint(color), align, tracking);
}

float snapPx(float value) { return std::round(value / px()) * px(); }

void pixelRect(Rectangle rect, Color color) {
    if (color.a == 0 || g_fade <= 0.0f) return;
    const Rectangle r = snapRect(rect);
    fillRect(r.x, r.y, r.width, r.height, tint(color));
}

void roundedRect(Rectangle rect, float, Color color) {
    if (color.a == 0 || g_fade <= 0.0f) return;
    const Rectangle r = snapRect(rect);
    steppedFill(r, cornerSteps(r), tint(color));
}

// A one-pixel (or thicker) line around the stepped shape.
void roundedOutline(Rectangle rect, float, float thickness, Color color) {
    if (color.a == 0 || g_fade <= 0.0f) return;
    const Rectangle r = snapRect(rect);
    const float p = px();
    const float t = std::max(p, snapPx(thickness));
    const int steps = cornerSteps(r);
    const float cut = static_cast<float>(steps) * p;
    const Color c = tint(color);
    fillRect(r.x + cut, r.y, r.width - cut * 2.0f, t, c);
    fillRect(r.x + cut, r.y + r.height - t, r.width - cut * 2.0f, t, c);
    fillRect(r.x, r.y + cut, t, r.height - cut * 2.0f, c);
    fillRect(r.x + r.width - t, r.y + cut, t, r.height - cut * 2.0f, c);
    // A two-step corner needs one diagonal pixel to join the two sides.
    if (steps == 2) {
        fillRect(r.x + p, r.y + p, t, t, c);
        fillRect(r.x + r.width - p - t, r.y + p, t, t, c);
        fillRect(r.x + p, r.y + r.height - p - t, t, t, c);
        fillRect(r.x + r.width - p - t, r.y + r.height - p - t, t, t, c);
    }
}

// Flat fill, a dark outer line, and a lighter line just inside it.
void panel(Rectangle rect, Color fill, Color edge, float) {
    if (g_fade <= 0.0f) return;
    const Rectangle r = snapRect(rect);
    const float p = px();
    const int steps = cornerSteps(r);
    steppedFill(r, steps, tint(theme::kUiBorderDark));
    const Rectangle mid = inset(r, p);
    steppedFill(mid, std::max(0, steps - 1), tint(edge));
    const Rectangle in = inset(r, 2.0f * p);
    if (in.width > 0.0f && in.height > 0.0f) steppedFill(in, std::max(0, steps - 1), tint(fill));
}

// A bar is a dark frame with a flat fill chunk, cut to whole pixels.
void bar(Rectangle rect, float fraction, Color fill, Color back) {
    if (g_fade <= 0.0f) return;
    const Rectangle r = snapRect(rect);
    const float p = px();
    fillRect(r.x, r.y, r.width, r.height, tint(theme::kUiBorderDark));
    const Rectangle in = inset(r, p);
    if (in.width <= 0.0f || in.height <= 0.0f) return;
    fillRect(in.x, in.y, in.width, in.height, tint(back));
    const float f = clamp01(fraction);
    if (f <= 0.0f) return;
    const float width = std::max(p, snapPx(in.width * f));
    fillRect(in.x, in.y, std::min(width, in.width), in.height, tint(fill));
}

void pixelDisc(float cx, float cy, float radius, Color color) {
    if (color.a == 0 || g_fade <= 0.0f) return;
    const float p = px();
    const Color c = tint(color);
    for (float dy = -radius; dy < radius; dy += p) {
        const float mid = dy + p * 0.5f;
        const float half = std::sqrt(std::max(0.0f, radius * radius - mid * mid));
        const float w = std::max(p, snapPx(half * 2.0f));
        fillRect(std::round((cx - w * 0.5f) / p) * p, std::round((cy + dy) / p) * p, w, p, c);
    }
}

void pixelRing(float cx, float cy, float radius, float thickness, Color color) {
    if (color.a == 0 || g_fade <= 0.0f) return;
    const float p = px();
    const Color c = tint(color);
    const float inner = std::max(0.0f, radius - thickness);
    for (float dy = -radius; dy < radius; dy += p) {
        const float mid = dy + p * 0.5f;
        const float outerHalf = std::sqrt(std::max(0.0f, radius * radius - mid * mid));
        const float y = std::round((cy + dy) / p) * p;
        if (std::fabs(mid) >= inner) {
            const float w = std::max(p, snapPx(outerHalf * 2.0f));
            fillRect(std::round((cx - w * 0.5f) / p) * p, y, w, p, c);
        } else {
            const float innerHalf = std::sqrt(inner * inner - mid * mid);
            const float seg = std::max(p, snapPx(outerHalf - innerHalf));
            fillRect(std::round((cx - outerHalf) / p) * p, y, seg, p, c);
            fillRect(std::round((cx + outerHalf) / p) * p - seg, y, seg, p, c);
        }
    }
}

void pixelArrow(float x, float cy, float size, Color color) {
    if (g_fade <= 0.0f) return;
    const float p = std::max(px(), snapPx(size / 5.0f));
    const Color c = tint(color);
    for (int row = 0; row < 5; ++row) {
        const int width = std::min(row, 4 - row) + 1;
        fillRect(std::round(x / px()) * px(), std::round((cy + (static_cast<float>(row) - 2.0f) * p) / px()) * px(), static_cast<float>(width) * p, p, c);
    }
}

void keyChip(const char* label, float x, float cy, float height, font::Align align) {
    const float size = height * 0.62f;
    const float width = std::max(height, font::measure(label, size) + height * 0.7f);
    const float left = align == font::Align::Left ? x : (align == font::Align::Center ? x - width * 0.5f : x - width);
    const Rectangle rect{left, cy - height * 0.5f, width, height};
    panel(rect, theme::kKeyChipFill, theme::kUiBorder, 0.0f);
    textMiddle(label, left + width * 0.5f, cy, size, theme::kUiText, font::Align::Center);
}

bool inside(Vec2 point, Rectangle rect) {
    return point.x >= rect.x && point.x <= rect.x + rect.width && point.y >= rect.y && point.y <= rect.y + rect.height;
}

void navigate(Nav& nav, int count, bool horizontalToo) {
    if (count <= 0) return;
    const int step = g_input.stepY + (horizontalToo ? g_input.stepX : 0);
    if (step == 0) return;
    nav.selected = ((nav.selected + step) % count + count) % count;
    audio::playUi(audio::UiSound::Move);
}

void registerRect(const char* name, Rectangle rect) {
    if (g_registeredCount >= kMaxRegistered) return;
    Registered& entry = g_registered[g_registeredCount++];
    std::snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.rect = rect;
}

void markSelected(const char* name) { std::snprintf(g_selectedName, sizeof(g_selectedName), "%s", name); }
const char* selectedName() { return g_selectedName; }

int registeredCount() { return g_registeredCount; }
const Registered& registered(int index) { return g_registered[index]; }

namespace {

// Hover moves the selection, but only when the mouse actually moved, so a parked
// mouse never fights the keyboard.
void hoverSelect(Nav& nav, int index, Rectangle rect) {
    if (g_input.mouseMoved && inside(g_input.mouse, rect)) {
        if (nav.selected != index) audio::playUi(audio::UiSound::Move);
        nav.selected = index;
    }
}

bool activated(const Nav& nav, int index, Rectangle rect) {
    if (!g_inputEnabled) return false;
    if (g_input.pointerPressed && inside(g_input.pointer, rect)) return true;
    return nav.selected == index && g_input.confirm;
}

// The box behind a button, slider or toggle: a hard drop shadow, then a flat panel
// with a two-tone border. Selected controls get the gold border.
void controlBox(Rectangle rect, Color fill, bool selected, bool lit) {
    const float p = g_layout.px;
    roundedRect(Rectangle{rect.x + p, rect.y + p, rect.width, rect.height}, 0.0f, theme::kUiShadow);
    panel(rect, fill, selected ? theme::kUiAccent : (lit ? theme::kUiAccentDark : theme::kUiBorder), 0.0f);
}

}  // namespace

bool button(Nav& nav, int index, Rectangle rect, const char* label, ButtonLook look, const char* keyHint, bool enabled) {
    const float u = g_layout.u;
    registerRect(label, rect);
    if (enabled) hoverSelect(nav, index, rect);
    if (enabled && nav.selected == index) markSelected(label);
    const bool selected = enabled && nav.selected == index && showsSelection();
    const bool primary = look == ButtonLook::Primary;

    Color fill = primary ? theme::kUiAccent : theme::kButtonFill;
    Color textColor = primary ? theme::kTextOnPrimary : theme::kUiText;
    if (!enabled) {
        fill = theme::kButtonDisabled;
        textColor = theme::kTextDisabled;
    } else if (selected) {
        fill = primary ? theme::kButtonPrimaryHover : theme::kButtonHover;
    }
    controlBox(rect, fill, selected, primary);
    // A pointer marks the selected choice, so it can be read without relying on color alone.
    if (selected && !primary) pixelArrow(rect.x + g_layout.px * 5.0f, rect.y + rect.height * 0.5f, rect.height * 0.3f, theme::kUiAccent);
    if (selected && primary) pixelArrow(rect.x + g_layout.px * 5.0f, rect.y + rect.height * 0.5f, rect.height * 0.3f, theme::kTextOnPrimary);

    const float sizeShare = look == ButtonLook::Primary ? 0.46f : (look == ButtonLook::Small ? 0.40f : 0.44f);
    const float size = std::min(rect.height * sizeShare, 34.0f * u);
    const float maxText = rect.width - rect.height * 1.4f;
    const float fitted = font::fitSize(label, size, maxText);
    textMiddle(label, rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f, fitted, textColor, font::Align::Center);

    if (keyHint && keyHint[0] && showPrompts() && enabled && rect.width > 150.0f * u) {
        keyChip(keyHint, rect.x + rect.width - rect.height * 0.3f, rect.y + rect.height * 0.5f, rect.height * 0.5f, font::Align::Right);
    }
    const bool pressed = enabled && activated(nav, index, rect);
    if (pressed) audio::playUi(audio::UiSound::Confirm);
    return pressed;
}

bool slider(Nav& nav, int index, Rectangle rect, const char* label, float* value, float step) {
    const float u = g_layout.u;
    registerRect(label, rect);
    hoverSelect(nav, index, rect);
    if (nav.selected == index) markSelected(label);
    const bool selected = nav.selected == index && showsSelection();
    controlBox(rect, selected ? theme::kButtonHover : theme::kButtonFill, selected, false);

    const float pad = rect.height * 0.30f;
    const float labelSize = std::min(rect.height * 0.36f, 28.0f * u);
    // Wide rows put the label and track side by side, narrow ones stack them.
    const bool stacked = rect.width < 460.0f * u;
    Rectangle track;
    char percent[16];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(*value * 100.0f + 0.5f));
    if (stacked) {
        textMiddle(label, rect.x + pad, rect.y + rect.height * 0.30f, labelSize, theme::kUiText);
        textMiddle(percent, rect.x + rect.width - pad, rect.y + rect.height * 0.30f, labelSize, theme::kUiAccent, font::Align::Right);
        track = Rectangle{rect.x + pad, rect.y + rect.height * 0.66f - 3.0f * g_layout.px, rect.width - pad * 2.0f, 6.0f * g_layout.px};
    } else {
        textMiddle(label, rect.x + pad, rect.y + rect.height * 0.5f, labelSize, theme::kUiText);
        const float percentWidth = 70.0f * u;
        const float trackLeft = rect.x + rect.width * 0.42f;
        track = Rectangle{trackLeft, rect.y + rect.height * 0.5f - 3.0f * g_layout.px, rect.x + rect.width - pad - percentWidth - trackLeft, 6.0f * g_layout.px};
        textMiddle(percent, rect.x + rect.width - pad, rect.y + rect.height * 0.5f, labelSize, theme::kUiAccent, font::Align::Right);
    }

    bool changed = false;
    // The grab area is much taller than the drawn track, so a thumb can hit it.
    const Rectangle grab{track.x - 14.0f * u, rect.y, track.width + 28.0f * u, rect.height};
    if (g_inputEnabled && g_input.pointerPressed && inside(g_input.pointer, grab)) g_dragging = value;
    if (!g_input.pointerDown && !g_input.pointerPressed) g_dragging = nullptr;
    if (g_dragging == value && (g_input.pointerDown || g_input.pointerPressed)) {
        const float x = g_input.pointerDown ? g_input.pointerPos.x : g_input.pointer.x;
        const float next = std::round(clamp01((x - track.x) / track.width) * 100.0f) / 100.0f;
        if (next != *value) {
            *value = next;
            changed = true;
        }
        nav.selected = index;
    }
    if (nav.selected == index && g_inputEnabled) {
        if (g_input.stepX != 0) {
            *value = std::round(clamp01(*value + step * static_cast<float>(g_input.stepX)) * 100.0f) / 100.0f;
            changed = true;
            audio::playUi(audio::UiSound::Move);
        }
    }

    bar(track, *value, theme::kUiAccent, theme::kUiBarBack);
    const float knobX = track.x + track.width * clamp01(*value);
    // The knob is a small square with the same two-tone border as everything else.
    const float knob = g_layout.px * 7.0f;
    panel(Rectangle{knobX - knob * 0.5f, track.y + track.height * 0.5f - knob * 0.5f, knob, knob}, theme::kUiText, theme::kUiBorder, 0.0f);
    return changed;
}

bool toggle(Nav& nav, int index, Rectangle rect, const char* label, bool* value) {
    const float u = g_layout.u;
    registerRect(label, rect);
    hoverSelect(nav, index, rect);
    if (nav.selected == index) markSelected(label);
    const bool selected = nav.selected == index && showsSelection();
    controlBox(rect, selected ? theme::kButtonHover : theme::kButtonFill, selected, false);

    const float pad = rect.height * 0.30f;
    const float labelSize = std::min(rect.height * 0.36f, 28.0f * u);
    textMiddle(label, rect.x + pad, rect.y + rect.height * 0.5f, labelSize, theme::kUiText);

    // The switch: a square-ended slot with a square knob that sits left for off and right for on.
    const float p = g_layout.px;
    const float switchHeight = std::max(8.0f * p, snapPx(std::min(rect.height * 0.5f, 34.0f * u)));
    const Rectangle sw{rect.x + rect.width - pad - switchHeight * 2.0f, rect.y + (rect.height - switchHeight) * 0.5f, switchHeight * 2.0f, switchHeight};
    panel(sw, *value ? theme::kUiAccent : theme::kUiBarBack, *value ? theme::kUiAccentDark : theme::kUiBorder, 0.0f);
    const float knob = switchHeight - 4.0f * p;
    const float knobX = *value ? sw.x + sw.width - 2.0f * p - knob : sw.x + 2.0f * p;
    pixelRect(Rectangle{knobX, sw.y + 2.0f * p, knob, knob}, *value ? theme::kTextOnPrimary : theme::kUiTextDim);
    textMiddle(*value ? "ON" : "OFF", sw.x - pad * 0.6f, rect.y + rect.height * 0.5f, labelSize * 0.9f,
               *value ? theme::kUiAccent : theme::kUiTextDim, font::Align::Right);

    bool changed = false;
    if (activated(nav, index, rect)) {
        *value = !*value;
        changed = true;
    } else if (nav.selected == index && g_inputEnabled) {
        if (g_input.stepX < 0 && *value) { *value = false; changed = true; }
        if (g_input.stepX > 0 && !*value) { *value = true; changed = true; }
    }
    if (changed) audio::playUi(audio::UiSound::Confirm);
    return changed;
}

}  // namespace ls::ui
