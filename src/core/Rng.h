#pragma once

#include <cstdint>

namespace ls {

// PCG32 random number generator. It is small and fast, and the same seed always
// gives the same numbers on every platform. That property makes runs replayable.
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL;
    uint64_t stream = 0xda3e39cb94b95bdbULL;

    void seed(uint64_t value) {
        state = 0;
        nextU32();
        state += value;
        nextU32();
    }

    uint32_t nextU32() {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + (stream | 1u);
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // Uniform float in [0, 1).
    float next01() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }

    float range(float low, float high) { return low + (high - low) * next01(); }

    // Uniform integer in [0, count). Returns 0 when count is 0.
    int below(int count) {
        if (count <= 0) return 0;
        return static_cast<int>((static_cast<uint64_t>(nextU32()) * static_cast<uint64_t>(count)) >> 32);
    }

    bool chance(float probability) { return next01() < probability; }
};

}  // namespace ls
