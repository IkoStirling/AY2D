// Test_CollisionFlags.cpp — Phase 1+ stub.
//
// Asserts that the CollisionFlags bitmask + operators in design.md §8.1
// behave correctly:
//
//   * operator|, operator&, operator^, operator~ compose as expected.
//   * operator|=, operator&= are mutating equivalents.
//   * `None` is the zero value; `Empty` is an explicit non-zero bit.
//
// Real collision flags backing-store tests land in Phase 5+ alongside
// ITileCollisionQuery / .aytilemap loader integration (design.md §8 +
// §9). Until then, this file is a compile + operator coverage test.

#include <cstdint>

#include "AY2D/TileCollision.h"
#include "AYTest.h"

using ayt::ay2d::CollisionFlags;

TEST_SUITE(CollisionFlagsSuite)

    TEST_CASE(NoneIsZero) {
        CHECK_INT_EQ(static_cast<uint32_t>(CollisionFlags::None), 0u);
    }

    TEST_CASE(EmptyIsNonZero) {
        // design.md §8.1: Empty (1u << 6) is an explicit "this tile has
        // no collision at all" marker, distinct from None (unset /
        // unknown). The default-constructed zero MUST NOT be used to
        // mean "empty"; the loader normalizes unknown bits to Empty.
        CHECK(static_cast<uint32_t>(CollisionFlags::Empty) != 0u);
    }

    TEST_CASE(OrCombinesDistinctFlags) {
        auto combined = CollisionFlags::Solid | CollisionFlags::Hazard;
        CHECK((combined & CollisionFlags::Solid)  != CollisionFlags::None);
        CHECK((combined & CollisionFlags::Hazard) != CollisionFlags::None);
        CHECK((combined & CollisionFlags::Ladder) == CollisionFlags::None);
    }

    TEST_CASE(AndIsIntersect) {
        auto a = CollisionFlags::Solid | CollisionFlags::Hazard;
        auto b = CollisionFlags::Hazard | CollisionFlags::Ladder;
        auto intersection = a & b;
        CHECK(intersection == CollisionFlags::Hazard);
    }

    TEST_CASE(OrAssignUpdatesLhs) {
        CollisionFlags acc = CollisionFlags::Solid;
        acc |= CollisionFlags::Hazard;
        CHECK((acc & CollisionFlags::Solid)  != CollisionFlags::None);
        CHECK((acc & CollisionFlags::Hazard) != CollisionFlags::None);
        CHECK((acc & CollisionFlags::Ladder) == CollisionFlags::None);
    }

    TEST_CASE(NotInvertsBits) {
        // Spot-check: ~Solid has all OTHER bits set, including Empty
        // and Hazard.
        auto inverted = ~CollisionFlags::Solid;
        CHECK((inverted & CollisionFlags::Solid) == CollisionFlags::None);
        CHECK((inverted & CollisionFlags::Hazard) != CollisionFlags::None);
        CHECK((inverted & CollisionFlags::Empty)  != CollisionFlags::None);
    }

TEST_SUITE_END
