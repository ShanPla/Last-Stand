#pragma once

#include "core/Math.h"

namespace ls::worldview {

// The way the soldier really went during a dash, for drawing his afterimage. A car can stop or
// deflect him, so the dash's direction and elapsed time don't tell where he has been. Only the
// renderer keeps this: nothing in the game depends on it, so replays are not affected. It has no
// graphics code in it, so the unit tests can check it.
class DashPath {
public:
    void clear() { count_ = 0; }
    bool empty() const { return count_ == 0; }

    // Adds where he is after a simulation step. A dash is 12 steps long, so it never fills up.
    void add(Vec2 pos) {
        if (count_ < kMaxPoints) points_[count_++] = pos;
    }

    // Finds the point this far back along the path, walking from where he is drawn. He is drawn
    // between the last two points, so the walk starts at the one before last. Returns false
    // when he hasn't come that far, which is every time if he dashed into a car and never moved.
    bool pointBack(Vec2 drawnAt, float back, Vec2* out) const {
        Vec2 from = drawnAt;
        float left = back;
        for (int i = count_ - 2; i >= 0; --i) {
            const float stretch = distance(from, points_[i]);
            if (stretch >= left && stretch > 0.0f) {
                *out = lerp(from, points_[i], left / stretch);
                return true;
            }
            left -= stretch;
            from = points_[i];
        }
        return false;
    }

private:
    static constexpr int kMaxPoints = 16;
    Vec2 points_[kMaxPoints];
    int count_ = 0;
};

}  // namespace ls::worldview
