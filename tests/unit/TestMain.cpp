#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "core/Rng.h"

TEST_CASE("same seed gives the same random numbers") {
    ls::Rng first;
    ls::Rng second;
    first.seed(1234);
    second.seed(1234);
    for (int i = 0; i < 1000; ++i) {
        CHECK(first.nextU32() == second.nextU32());
    }
}

TEST_CASE("random ranges stay inside their bounds") {
    ls::Rng rng;
    rng.seed(99);
    for (int i = 0; i < 10000; ++i) {
        const float value = rng.next01();
        CHECK(value >= 0.0f);
        CHECK(value < 1.0f);
        const int pick = rng.below(7);
        CHECK(pick >= 0);
        CHECK(pick < 7);
    }
}
