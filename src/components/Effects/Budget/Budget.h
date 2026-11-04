#pragma once

// A token bucket: the limit that keeps thousands of hits a second cheap. Tokens refill
// at a steady rate up to a small burst, and an event with no token left is skipped in
// one comparison, before it has cost anything.
namespace ls::effects {

class Budget {
public:
    constexpr Budget(float perSecond, float burst) : perSecond_(perSecond), burst_(burst), tokens_(burst) {}

    void refill(float dt, float amount) {
        tokens_ += perSecond_ * amount * dt;
        if (tokens_ > burst_) tokens_ = burst_;
    }

    bool take() {
        if (tokens_ < 1.0f) return false;
        tokens_ -= 1.0f;
        return true;
    }

private:
    float perSecond_;
    float burst_;
    float tokens_;
};

}  // namespace ls::effects
