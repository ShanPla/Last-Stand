#include "components/Ui/Widgets/Widgets.h"

#include "components/Audio/Audio.h"
#include "components/Net/Net.h"
#include "core/Platform.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ls::ui {

namespace {

constexpr int kMaxRegistered = 64;  // The on-screen keyboard registers up to 44 keys.
constexpr float kDesignShortSide = 560.0f;

Layout g_layout;
input::Snapshot g_input;
float g_seconds = 0.0f;
float g_fade = 1.0f;
bool g_inputEnabled = true;
Registered g_registered[kMaxRegistered];
int g_registeredCount = 0;
constexpr int kMaxExtents = 24;
Registered g_extents[kMaxExtents];
int g_extentCount = 0;
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
    // A drag ends when the pointer is released, even on a frame when no slider is drawn.
    if (!input.pointerDown && !input.pointerPressed) g_dragging = nullptr;
    g_seconds += dtReal;
    g_registeredCount = 0;
    g_extentCount = 0;
    g_selectedName[0] = '\0';
    g_fade = 1.0f;
    g_inputEnabled = true;
    // A text field that is still being edited sets this again while it draws. Input reads it
    // before the next frame's drawing, so it falls away on its own when the field is gone.
    input::setTextInputActive(false);

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
    // A screen change also ends any slider drag. The slider code only runs while Settings
    // is on screen, so a drag left open here would still be there when Settings opens again.
    g_dragging = nullptr;
    g_input.pointerPressed = false;
    g_input.confirm = false;
    g_input.reroll = false;
    g_input.back = false;
    textCancel();
}

void setInputEnabled(bool enabled) { g_inputEnabled = enabled; }
bool inputEnabled() { return g_inputEnabled; }

bool showPrompts() { return g_input.lastDevice != input::Device::Touch; }

void formatClock(char* out, size_t size, float seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    if (total >= 3600) std::snprintf(out, size, "%d:%02d:%02d", total / 3600, total / 60 % 60, total % 60);
    else std::snprintf(out, size, "%d:%02d", total / 60, total % 60);
}

const char* promptConfirm() { return g_input.lastDevice == input::Device::Gamepad ? "A" : "Enter"; }
const char* promptBack() { return g_input.lastDevice == input::Device::Gamepad ? "B" : "Esc"; }
const char* promptReroll() { return g_input.lastDevice == input::Device::Gamepad ? "Y" : "R"; }
const char* promptDash() { return g_input.lastDevice == input::Device::Gamepad ? "RB" : "Space"; }
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
    // A bar too thin for a border gives it up, because a border on both sides would leave no
    // room at all, and a bar that silently draws nothing looks like one that never fills.
    const Rectangle in = r.height >= p * 3.0f ? inset(r, p) : r;
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

void pixelArrow(float x, float cy, float size, Color color, bool pointLeft) {
    if (g_fade <= 0.0f) return;
    const float p = std::max(px(), snapPx(size / 5.0f));
    const Color c = tint(color);
    for (int row = 0; row < 5; ++row) {
        const int width = std::min(row, 4 - row) + 1;
        // A left-pointing arrow is the same rows, pushed against the right side of the box.
        const float shift = pointLeft ? static_cast<float>(3 - width) * p : 0.0f;
        fillRect(std::round((x + shift) / px()) * px(), std::round((cy + (static_cast<float>(row) - 2.0f) * p) / px()) * px(), static_cast<float>(width) * p, p, c);
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

void registerExtent(const char* name, Rectangle rect) {
    if (g_extentCount >= kMaxExtents) return;
    Registered& entry = g_extents[g_extentCount++];
    std::snprintf(entry.name, sizeof(entry.name), "%s", name);
    entry.rect = rect;
}
int extentCount() { return g_extentCount; }
const Registered& extent(int index) { return g_extents[index]; }

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
    float fitted = font::fitSize(label, size, maxText);
    float labelCenter = rect.x + rect.width * 0.5f;

    const bool chipShown = keyHint && keyHint[0] && showPrompts() && enabled && rect.width > 150.0f * u;
    const float chipHeight = rect.height * 0.5f;
    const float chipRight = rect.x + rect.width - rect.height * 0.3f;
    if (chipShown) {
        // On a narrow button a centered label would run under the key prompt. So the label
        // then centers in the room left of the prompt, and shrinks if it still doesn't fit.
        const float chipWidth = std::max(chipHeight, font::measure(keyHint, chipHeight * 0.62f) + chipHeight * 0.7f);
        const float roomLeft = rect.x + rect.height * 0.35f;
        const float roomRight = chipRight - chipWidth - 6.0f * u;
        if (labelCenter + font::measure(label, fitted) * 0.5f > roomRight) {
            fitted = font::fitSize(label, fitted, roomRight - roomLeft);
            labelCenter = (roomLeft + roomRight) * 0.5f;
        }
        registerExtent("button chip", Rectangle{chipRight - chipWidth, rect.y, chipWidth, rect.height});
        const float labelWidth = font::measure(label, fitted);
        registerExtent("button text", Rectangle{labelCenter - labelWidth * 0.5f, rect.y, labelWidth, rect.height});
    }
    textMiddle(label, labelCenter, rect.y + rect.height * 0.5f, fitted, textColor, font::Align::Center);
    if (chipShown) keyChip(keyHint, chipRight, rect.y + rect.height * 0.5f, chipHeight, font::Align::Right);
    const bool pressed = enabled && activated(nav, index, rect);
    if (pressed) audio::playUi(audio::UiSound::Confirm);
    return pressed;
}

bool selector(Nav& nav, int index, Rectangle rect, const char* label, const char* const* options, int count, int* value,
              const Color* optionColors) {
    if (count <= 0) return false;
    const float u = g_layout.u;
    const float p = g_layout.px;
    *value = ((*value % count) + count) % count;

    // The arrow cells are squares at each end. The middle shows the label and the choice.
    const float cell = rect.height;
    const Rectangle left{rect.x, rect.y, cell, rect.height};
    const Rectangle right{rect.x + rect.width - cell, rect.y, cell, rect.height};
    const Rectangle middle{rect.x + cell, rect.y, rect.width - cell * 2.0f, rect.height};
    char name[24];
    std::snprintf(name, sizeof(name), "%s <", label);
    registerRect(name, left);
    registerRect(label, middle);
    std::snprintf(name, sizeof(name), "%s >", label);
    registerRect(name, right);

    hoverSelect(nav, index, rect);
    if (nav.selected == index) markSelected(label);
    const bool selected = nav.selected == index && showsSelection();
    controlBox(rect, selected ? theme::kButtonHover : theme::kButtonFill, selected, false);

    // Press on a part first, so a click on the left arrow can never also count as the middle.
    int step = 0;
    if (g_inputEnabled && g_input.pointerPressed) {
        if (inside(g_input.pointer, left)) step = -1;
        else if (inside(g_input.pointer, right) || inside(g_input.pointer, middle)) step = 1;
    }
    if (g_inputEnabled && nav.selected == index) {
        if (g_input.stepX != 0) step = g_input.stepX < 0 ? -1 : 1;
        else if (g_input.confirm) step = 1;
    }

    const float arrowSize = rect.height * 0.3f;
    const Color arrowColor = selected ? theme::kUiAccent : theme::kUiText;
    pixelArrow(left.x + (left.width - arrowSize * 0.6f) * 0.5f, left.y + left.height * 0.5f, arrowSize, arrowColor, true);
    pixelArrow(right.x + (right.width - arrowSize * 0.6f) * 0.5f, right.y + right.height * 0.5f, arrowSize, arrowColor, false);
    // Thin dividers, so the arrow cells read as parts of the row.
    pixelRect(Rectangle{middle.x - p * 0.5f, rect.y + p * 3.0f, p, rect.height - p * 6.0f}, theme::kDivider);
    pixelRect(Rectangle{middle.x + middle.width - p * 0.5f, rect.y + p * 3.0f, p, rect.height - p * 6.0f}, theme::kDivider);

    const char* shown = options[*value];
    const Color shownColor = optionColors ? optionColors[*value] : theme::kUiText;
    const float labelSize = std::min(rect.height * 0.24f, 14.0f * u);
    const float valueSize = font::fitSize(shown, std::min(rect.height * 0.42f, 28.0f * u), middle.width - 8.0f * u);
    text(label, middle.x + middle.width * 0.5f, rect.y + rect.height * 0.12f, labelSize, theme::kUiTextDim, font::Align::Center, p);
    textMiddle(shown, middle.x + middle.width * 0.5f, rect.y + rect.height * 0.66f, valueSize, shownColor, font::Align::Center);

    if (step == 0) return false;
    *value = ((*value + step) % count + count) % count;
    audio::playUi(audio::UiSound::Move);
    return true;
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


// ---- Text entry -----------------------------------------------------------------

namespace {

constexpr int kMaxKeys = 48;

enum class KeyType { Char, Shift, Space, Delete, Done };

struct KeyCell {
    KeyType type = KeyType::Char;
    char ch = 0;
    int span = 1;     // Cells wide.
    int row = 0;
    int cell = 0;     // First cell in its row.
    Rectangle rect{};
    char label[8] = {};
};

TextEntry* g_activeEntry = nullptr;
Rectangle g_activeRect{};
KeyCell g_keys[kMaxKeys];
int g_keyCount = 0;
int g_keyRows = 0;

int maxLength(TextKind kind) { return kind == TextKind::Code ? net::kCodeLength : net::kNameMax; }

bool charAllowed(TextKind kind, char c) { return kind == TextKind::Code ? net::codeCharAllowed(c) : net::nameCharAllowed(c); }

void appendChar(TextEntry& entry, char c) {
    if (entry.length >= maxLength(entry.kind) || !charAllowed(entry.kind, c)) return;
    if (entry.kind == TextKind::Code && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    // A name can't start with a space, and a code has none.
    if (c == ' ' && entry.length == 0) return;
    entry.text[entry.length++] = c;
    entry.text[entry.length] = '\0';
    // After a letter the next one is lower case, until Shift is pressed again.
    if (c != ' ') entry.shift = false;
}

void deleteChar(TextEntry& entry) {
    if (entry.length <= 0) return;
    entry.text[--entry.length] = '\0';
    if (entry.length == 0) entry.shift = true;
}

void beginEditing(TextEntry& entry, TextKind kind) {
    if (g_activeEntry && g_activeEntry != &entry) textCancel();
    g_activeEntry = &entry;
    entry.kind = kind;
    entry.editing = true;
    // The pixel keyboard is for fingers and gamepads. A keyboard or mouse user types.
    entry.keys = g_input.lastDevice == input::Device::Touch || g_input.lastDevice == input::Device::Gamepad;
    entry.shift = entry.length == 0 && kind == TextKind::Name;
    entry.key = 0;
}

void finishEditing(TextEntry& entry) {
    // A name keeps no trailing spaces.
    while (entry.length > 0 && entry.text[entry.length - 1] == ' ') entry.text[--entry.length] = '\0';
    entry.editing = false;
    entry.keys = false;
    entry.finished = true;
    if (g_activeEntry == &entry) g_activeEntry = nullptr;
}

// Builds the key list for the active entry, and returns the rectangle of the whole keyboard
// panel. Keys wrap row by row like text. Wider keys (Shift, Space, Delete, OK) take two cells.
Rectangle layoutKeys(const TextEntry& entry) {
    const Layout& l = g_layout;
    const float u = l.u;
    const float gap = std::max(l.px * 2.0f, 4.0f * u);
    const float pad = 10.0f * u;
    const float panelWidth = std::min(l.width(), 720.0f * u);

    // As many columns as keep every key finger sized, between 6 and 13.
    const float minKey = std::max(l.touch, 40.0f * u);
    int columns = static_cast<int>((panelWidth - pad * 2.0f + gap) / (minKey + gap));
    columns = std::min(13, std::max(6, columns));
    const float keyWidth = (panelWidth - pad * 2.0f - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);

    g_keyCount = 0;
    const auto add = [&](KeyType type, char ch, int span, const char* label) {
        if (g_keyCount >= kMaxKeys) return;
        KeyCell& key = g_keys[g_keyCount++];
        key.type = type;
        key.ch = ch;
        key.span = span;
        std::snprintf(key.label, sizeof(key.label), "%s", label);
    };
    if (entry.kind == TextKind::Code) {
        for (const char* c = net::kCodeAlphabet; *c; ++c) {
            const char label[2] = {*c, '\0'};
            add(KeyType::Char, *c, 1, label);
        }
    } else {
        for (char c = 'A'; c <= 'Z'; ++c) {
            const char shown = entry.shift ? c : static_cast<char>(c - 'A' + 'a');
            const char label[2] = {shown, '\0'};
            add(KeyType::Char, shown, 1, label);
        }
        for (char c = '0'; c <= '9'; ++c) {
            const char label[2] = {c, '\0'};
            add(KeyType::Char, c, 1, label);
        }
        add(KeyType::Shift, 0, 2, "ABC");
        add(KeyType::Space, ' ', 2, "SPACE");
    }
    add(KeyType::Delete, 0, 2, "DEL");
    add(KeyType::Done, 0, 2, "OK");

    // Flow the keys into rows.
    int row = 0;
    int used = 0;
    for (int i = 0; i < g_keyCount; ++i) {
        if (used + g_keys[i].span > columns) {
            row++;
            used = 0;
        }
        g_keys[i].row = row;
        g_keys[i].cell = used;
        used += g_keys[i].span;
    }
    g_keyRows = row + 1;

    const float previewHeight = std::max(l.touch, 40.0f * u);
    const bool hints = g_input.lastDevice == input::Device::Gamepad;
    const float hintHeight = hints ? 26.0f * u : 0.0f;
    // The keys give up height before they give up being readable, when the window is short.
    float keyHeight = std::max(l.touch, 44.0f * u);
    const float chrome = pad * 2.0f + previewHeight + gap + hintHeight;
    const float roomForKeys = l.height() - chrome - 8.0f * u;
    keyHeight = std::min(keyHeight, (roomForKeys - gap * static_cast<float>(g_keyRows - 1)) / static_cast<float>(g_keyRows));
    keyHeight = std::max(keyHeight, 28.0f * u);

    const float panelHeight = chrome + keyHeight * static_cast<float>(g_keyRows) + gap * static_cast<float>(g_keyRows - 1);
    const Rectangle panelRect{l.centerX() - panelWidth * 0.5f, l.bottom - panelHeight, panelWidth, panelHeight};
    const float keysTop = panelRect.y + pad + previewHeight + gap;
    for (int i = 0; i < g_keyCount; ++i) {
        KeyCell& key = g_keys[i];
        key.rect = Rectangle{panelRect.x + pad + static_cast<float>(key.cell) * (keyWidth + gap), keysTop + static_cast<float>(key.row) * (keyHeight + gap),
                             keyWidth * static_cast<float>(key.span) + gap * static_cast<float>(key.span - 1), keyHeight};
    }
    return panelRect;
}

// The key nearest above or below the selected one, by horizontal position.
int keyInRow(int from, int step) {
    if (g_keyRows <= 1) return from;
    const int targetRow = ((g_keys[from].row + step) % g_keyRows + g_keyRows) % g_keyRows;
    const float centerX = g_keys[from].rect.x + g_keys[from].rect.width * 0.5f;
    int best = from;
    float bestDistance = 1e9f;
    for (int i = 0; i < g_keyCount; ++i) {
        if (g_keys[i].row != targetRow) continue;
        const float distance = std::fabs(g_keys[i].rect.x + g_keys[i].rect.width * 0.5f - centerX);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

void pressKey(TextEntry& entry, const KeyCell& key) {
    switch (key.type) {
        case KeyType::Char:
        case KeyType::Space: appendChar(entry, key.ch); audio::playUi(audio::UiSound::Move); break;
        case KeyType::Shift: entry.shift = !entry.shift; audio::playUi(audio::UiSound::Move); break;
        case KeyType::Delete: deleteChar(entry); audio::playUi(audio::UiSound::Move); break;
        case KeyType::Done: finishEditing(entry); audio::playUi(audio::UiSound::Confirm); break;
    }
}

bool caretOn() { return std::fmod(g_seconds, 1.0f) < 0.6f; }

}  // namespace

void textSet(TextEntry& entry, TextKind kind, const char* value) {
    entry.kind = kind;
    entry.length = 0;
    entry.text[0] = '\0';
    for (const char* c = value ? value : ""; *c; ++c) appendChar(entry, *c);
    entry.shift = entry.length == 0;
}

bool textEditing() { return g_activeEntry != nullptr; }
bool textKeyboardOpen() { return g_activeEntry != nullptr && g_activeEntry->keys; }

void textCancel() {
    if (g_activeEntry) {
        g_activeEntry->editing = false;
        g_activeEntry->keys = false;
    }
    g_activeEntry = nullptr;
}

void textField(Nav& nav, int index, Rectangle rect, const char* label, TextEntry& entry, TextKind kind, const char* placeholder) {
    const float u = g_layout.u;
    entry.finished = false;
    entry.kind = kind;
    registerRect(label, rect);
    hoverSelect(nav, index, rect);
    if (nav.selected == index) markSelected(label);

    // Starting. The press that opens the field is used up, so it can't also press a key of the
    // on-screen keyboard that appears under the same finger.
    bool justStarted = false;
    if (!entry.editing && activated(nav, index, rect)) {
        beginEditing(entry, kind);
        g_input.pointerPressed = false;
        g_input.confirm = false;
        justStarted = true;
    }

    if (entry.editing && g_activeEntry == &entry) {
        g_activeRect = rect;
        input::setTextInputActive(true);
        for (int i = 0; i < g_input.typedCount; ++i) appendChar(entry, g_input.typed[i]);
        for (int i = 0; i < g_input.backspaces; ++i) deleteChar(entry);
        if (!justStarted && !entry.keys) {
            // With a physical keyboard the field itself takes Enter and Esc. The on-screen
            // keyboard takes them for its own keys instead.
            if (g_input.confirm) {
                g_input.confirm = false;
                finishEditing(entry);
            } else if (g_input.back) {
                g_input.back = false;
                finishEditing(entry);
            } else if (g_input.stepY != 0 || (g_input.pointerPressed && !inside(g_input.pointer, rect))) {
                finishEditing(entry);
            }
        }
    }

    const bool editing = entry.editing;
    const bool selected = (editing || nav.selected == index) && showsSelection();
    controlBox(rect, theme::kCoopCodePlate, selected, false);

    const float pad = rect.height * 0.28f;
    const float labelSize = std::min(rect.height * 0.24f, 14.0f * u);
    text(label, rect.x + pad, rect.y + rect.height * 0.10f, labelSize, theme::kUiTextDim, font::Align::Left, g_layout.px);
    const float valueSize = std::min(rect.height * 0.44f, 28.0f * u);
    const float valueY = rect.y + rect.height * 0.64f;
    const float room = rect.width - pad * 2.0f;
    if (entry.length > 0) {
        const float fitted = font::fitSize(entry.text, valueSize, room);
        textMiddle(entry.text, rect.x + pad, valueY, fitted, theme::kUiText);
        if (editing && caretOn()) {
            const float caretX = rect.x + pad + font::measure(entry.text, fitted) + g_layout.px;
            pixelRect(Rectangle{caretX, valueY - fitted * 0.4f, g_layout.px * 2.0f, fitted * 0.8f}, theme::kCoopCaret);
        }
    } else if (editing) {
        if (caretOn()) pixelRect(Rectangle{rect.x + pad, valueY - valueSize * 0.4f, g_layout.px * 2.0f, valueSize * 0.8f}, theme::kCoopCaret);
    } else {
        const float fitted = font::fitSize(placeholder, valueSize, room);
        textMiddle(placeholder, rect.x + pad, valueY, fitted, theme::kUiTextDim);
    }
}

void drawTextKeyboard() {
    if (!g_activeEntry || !g_activeEntry->keys) return;
    TextEntry& entry = *g_activeEntry;
    const Layout& l = g_layout;
    const float u = l.u;
    const Rectangle panelRect = layoutKeys(entry);
    entry.key = std::min(std::max(entry.key, 0), g_keyCount - 1);

    // Dim everything behind the keyboard, so it is clear that the screen is waiting for it.
    pixelRect(Rectangle{0.0f, 0.0f, l.w, l.h}, withAlpha(theme::kScreenScrim, 0.6f));
    panel(panelRect, theme::kUiPanel, theme::kUiBorder, 0.0f);

    // What is typed so far, big, with the cursor.
    const float pad = 10.0f * u;
    const float previewHeight = std::max(l.touch, 40.0f * u);
    const Rectangle preview{panelRect.x + pad, panelRect.y + pad, panelRect.width - pad * 2.0f, previewHeight};
    panel(preview, theme::kCoopCodePlate, theme::kUiBorder, 0.0f);
    const char* title = entry.kind == TextKind::Code ? "ROOM CODE" : "YOUR NAME";
    const float titleSize = 14.0f * u;
    text(title, preview.x + 10.0f * u, preview.y + preview.height * 0.5f - titleSize * 0.5f, titleSize, theme::kUiTextDim, font::Align::Left, l.px);
    const float previewSize = std::min(preview.height * 0.55f, 30.0f * u);
    const float titleWidth = font::measure(title, titleSize, l.px) + 24.0f * u;
    const float textX = preview.x + std::max(titleWidth, preview.width * 0.34f);
    const float fitted = entry.length > 0 ? font::fitSize(entry.text, previewSize, preview.x + preview.width - textX - 12.0f * u) : previewSize;
    textMiddle(entry.text, textX, preview.y + preview.height * 0.5f, fitted, theme::kUiText);
    if (caretOn()) {
        const float caretX = textX + (entry.length > 0 ? font::measure(entry.text, fitted) : 0.0f) + l.px;
        pixelRect(Rectangle{caretX, preview.y + preview.height * 0.5f - fitted * 0.4f, l.px * 2.0f, fitted * 0.8f}, theme::kCoopCaret);
    }

    // Which key was pressed, and where the selection moves.
    int pressed = -1;
    for (int i = 0; i < g_keyCount; ++i) {
        const KeyCell& key = g_keys[i];
        char name[24];
        std::snprintf(name, sizeof(name), "key %s", key.label);
        registerRect(name, key.rect);
        if (g_input.mouseMoved && inside(g_input.mouse, key.rect)) entry.key = i;
        if (g_input.pointerPressed && inside(g_input.pointer, key.rect)) pressed = i;
    }
    // Move around with the arrows or the D-pad, and wrap at the edges.
    if (g_input.stepX != 0) {
        entry.key = ((entry.key + g_input.stepX) % g_keyCount + g_keyCount) % g_keyCount;
        audio::playUi(audio::UiSound::Move);
    }
    if (g_input.stepY != 0) {
        entry.key = keyInRow(entry.key, g_input.stepY);
        audio::playUi(audio::UiSound::Move);
    }
    markSelected(g_keys[entry.key].label);

    const bool shows = showsSelection();
    for (int i = 0; i < g_keyCount; ++i) {
        const KeyCell& key = g_keys[i];
        const bool isSelected = shows && entry.key == i;
        Color fill = key.type == KeyType::Done ? theme::kCoopKeyDone : (isSelected ? theme::kCoopKeyDown : theme::kCoopKey);
        if (key.type == KeyType::Done && isSelected) fill = theme::kButtonPrimaryHover;
        controlBox(key.rect, fill, isSelected, key.type == KeyType::Done);
        // On the code keyboard digits are teal, because Z and 2 look alike in this font.
        const bool codeDigit = entry.kind == TextKind::Code && key.type == KeyType::Char && key.ch >= '0' && key.ch <= '9';
        const Color ink = key.type == KeyType::Done ? theme::kTextOnPrimary : (codeDigit ? theme::kCoopCodeDigit : theme::kUiText);
        const float size = font::fitSize(key.label, std::min(key.rect.height * 0.5f, 26.0f * u), key.rect.width * 0.86f);
        textMiddle(key.label, key.rect.x + key.rect.width * 0.5f, key.rect.y + key.rect.height * 0.5f, size, ink, font::Align::Center);
    }
    if (g_input.lastDevice == input::Device::Gamepad) {
        char hint[48];
        std::snprintf(hint, sizeof(hint), "%s type    Y delete    %s done", promptConfirm(), promptBack());
        text(hint, panelRect.x + panelRect.width * 0.5f, panelRect.y + panelRect.height - pad - 20.0f * u, 16.0f * u, theme::kUiTextDim, font::Align::Center);
    }

    // Input. A press on a key types it. A press outside the keyboard and the field ends editing.
    if (pressed >= 0) {
        entry.key = pressed;
        pressKey(entry, g_keys[pressed]);
        g_input.pointerPressed = false;
    } else if (g_input.pointerPressed && !inside(g_input.pointer, panelRect) && !inside(g_input.pointer, g_activeRect)) {
        finishEditing(entry);
        g_input.pointerPressed = false;
    } else if (g_input.confirm) {
        g_input.confirm = false;
        pressKey(entry, g_keys[entry.key]);
    } else if (g_input.reroll) {
        g_input.reroll = false;
        deleteChar(entry);
    } else if (g_input.back) {
        g_input.back = false;
        finishEditing(entry);
    }
}

}  // namespace ls::ui
