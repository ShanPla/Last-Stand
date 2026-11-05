#include "doctest.h"

#include "StateFields.h"
#include "core/Sim.h"

#include <memory>
#include <string>
#include <vector>

using namespace ls;
using namespace ls::testing;

namespace {

std::string joined(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) text += "\n  " + line;
    return text;
}

}  // namespace

TEST_CASE("the field lists cover every byte of the state structs") {
    // The lists in StateFields.h are written by hand. If a field is added to a struct and
    // not to its list, the struct grows and the bytes no longer add up. Fix it by adding
    // the field to the list, and to sim::stateHash unless it is only used for drawing.
    for (const StructInfo* info : allStructInfos()) {
        size_t covered = 0;
        for (const Field& field : info->fields) {
            const std::string where = std::string(info->name) + "." + field.name + " lies outside the struct";
            CHECK_MESSAGE(field.offset + field.size <= info->size, where);
            covered += field.size;
        }
        const std::string what = std::string(info->name) + " is " + std::to_string(info->size) + " bytes, but its listed fields add up to " +
                                 std::to_string(covered) + " plus " + std::to_string(info->padding) +
                                 " bytes of padding. A field was added or removed: update StateFields.h and sim::stateHash.";
        CHECK_MESSAGE(covered + info->padding == info->size, what);
    }
}

TEST_CASE("a field added into an alignment gap of a state struct is noticed too") {
    // Adding a bool next to other small fields can fit in the gap the compiler leaves for
    // alignment, so the struct does not grow and the byte count above cannot see it. But a
    // real field is written when the struct is built, and a gap is not. So build each struct
    // twice in memory filled with different bytes, and count the unlisted bytes that were
    // left alone. They must be exactly the gaps that StateFields.h lists.
    struct Probe {
        char a = 0;
        int b = 0;  // Three bytes of gap sit between a and b.
    };
    const std::vector<bool> probe = untouchedBytes<Probe>();
    // Not every compiler leaves gaps alone. Where one does not, there is nothing to measure,
    // and the check is skipped rather than guessed. The probe shows which case this is.
    if (!(probe[1] && probe[2] && probe[3])) {
        WARN_MESSAGE(false, "this compiler writes to alignment gaps, so the gap check was skipped");
        return;
    }
    for (const StructInfo* info : allStructInfos()) {
        std::vector<bool> listed(info->size, false);
        for (const Field& field : info->fields) {
            for (size_t b = 0; b < field.size; ++b) listed[field.offset + b] = true;
        }
        const std::vector<bool> left = info->untouched();
        size_t unlistedLeftAlone = 0;
        for (size_t b = 0; b < info->size; ++b) {
            if (!listed[b] && left[b]) ++unlistedLeftAlone;
        }
        const std::string what = std::string(info->name) + " has " + std::to_string(unlistedLeftAlone) + " unlisted bytes that are never written, but " +
                                 std::to_string(info->padding) + " are expected: a field may have been added into a gap, so update StateFields.h and sim::stateHash.";
        CHECK_MESSAGE(unlistedLeftAlone == info->padding, what);
    }
}

TEST_CASE("every field of the state changes the hash, and only drawing fields do not") {
    auto state = makeBusyState();
    const uint64_t original = sim::stateHash(*state);
    std::vector<std::string> problems;
    int flips = 0;

    // Flips one bit of every byte of every field in turn. The hash has to notice each one,
    // unless the field is for drawing only. A bit flip needs no knowledge of the field's
    // type, and it keeps every bool, enum and float a legal value.
    visitStructs(*state, false, [&](const StructInfo& info, unsigned char* base, const char* name, int index) {
        const std::string label = labelOf(name, index);
        std::vector<bool> listed(info.size, false);
        for (const Field& field : info.fields) {
            for (size_t b = 0; b < field.size; ++b) listed[field.offset + b] = true;
            if (field.nested) continue;
            for (size_t b = 0; b < field.flipBytes; ++b) {
                base[field.offset + b] ^= 1;
                const bool changed = sim::stateHash(*state) != original;
                base[field.offset + b] ^= 1;
                ++flips;
                if (changed != field.hashed) {
                    problems.push_back(label + "." + field.name + " byte " + std::to_string(b) +
                                       (changed ? " changes the hash but is listed as drawing only" : " does not change the hash"));
                    break;  // One line per field is enough.
                }
            }
        }
        // Bytes that are in no field: padding. If one of them changes the hash, a field is
        // missing from the list.
        for (size_t b = 0; b < info.size; ++b) {
            if (listed[b]) continue;
            base[b] ^= 1;
            const bool changed = sim::stateHash(*state) != original;
            base[b] ^= 1;
            if (changed) problems.push_back(label + " byte " + std::to_string(b) + " is in no field list but changes the hash");
        }
    });

    CHECK_MESSAGE(flips > 500, "flips " << flips);  // The walk really visited the state.
    CHECK(sim::stateHash(*state) == original);  // Every flip was undone.
    CHECK_MESSAGE(problems.empty(), "hash and field lists disagree:" << joined(problems));
}

TEST_CASE("the hash sees how many items each pool holds") {
    auto state = makeBusyState();
    const uint64_t original = sim::stateHash(*state);
    const auto check = [&](auto& pool, const std::string& name) {
        const std::string more = name + ": one more item";
        const std::string fewer = name + ": one item fewer";
        REQUIRE(pool.count > 0);
        REQUIRE(pool.count < pool.capacity());
        pool.count += 1;  // The next item is a default one, so only the count differs.
        CHECK_MESSAGE(sim::stateHash(*state) != original, more);
        pool.count -= 2;
        CHECK_MESSAGE(sim::stateHash(*state) != original, fewer);
        pool.count += 1;
        CHECK(sim::stateHash(*state) == original);
    };
    check(state->enemies, "enemies");
    check(state->projectiles, "projectiles");
    check(state->enemyBullets, "enemyBullets");
    check(state->zones, "zones");
    check(state->gems, "gems");
    check(state->pickups, "pickups");
}

TEST_CASE("the hash sees which item sits in which pool slot") {
    // Two states that hold the same enemies in another order step differently, because
    // loops run in pool order. So the order has to be part of the hash.
    auto state = makeBusyState();
    REQUIRE(state->enemies.count >= 2);
    const uint64_t original = sim::stateHash(*state);
    std::swap(state->enemies[0], state->enemies[state->enemies.count - 1]);
    CHECK(sim::stateHash(*state) != original);
}
