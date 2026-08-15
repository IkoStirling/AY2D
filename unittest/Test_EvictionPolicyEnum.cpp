// Test_EvictionPolicyEnum.cpp - P3J.5 / A-12 EvictionPolicy reserved values.
//
// design.md §13.29. The EvictionPolicy enum
// (include/AY2D/TilemapBudget.h) ships with three values
// (LRU=0, Distance=1, TimeWindow=2) backed by uint8_t.
// Phase 4 streaming PR (R-3G.1) will add new policies; this
// test locks the underlying-type discipline, the canonical
// codes of existing values, and the convention that 0xFF
// is reserved as a sentinel for "unset / invalid policy".

#include <type_traits>

#include "AY2D/TilemapBudget.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(EvictionPolicyEnumSuite)

    TEST_CASE(EvictionPolicy_UnderlyingTypeIsUint8) {
        static_assert(sizeof(EvictionPolicy) == 1u,
                      "EvictionPolicy must be 1 byte (uint8_t).");
        static_assert(std::is_same<
                          std::underlying_type_t<EvictionPolicy>,
                          uint8_t>::value,
                      "EvictionPolicy underlying type must be uint8_t.");
        CHECK_TRUE(true);
    }

    TEST_CASE(EvictionPolicy_CanonicalCodes_ZeroOneTwo) {
        CHECK_INT_EQ(static_cast<uint8_t>(EvictionPolicy::LRU),        uint8_t{0});
        CHECK_INT_EQ(static_cast<uint8_t>(EvictionPolicy::Distance),   uint8_t{1});
        CHECK_INT_EQ(static_cast<uint8_t>(EvictionPolicy::TimeWindow), uint8_t{2});
    }

    TEST_CASE(EvictionPolicy_FFIsReservedSentinel) {
        // 0xFF is the reserved sentinel. It must not collide with
        // any current wired policy (codes 0, 1, 2) and must be
        // strictly greater than the highest wired code so future
        // policies (0x03..0xFE) can be added in between.
        constexpr uint8_t kReserved = 0xFFu;
        CHECK(kReserved > static_cast<uint8_t>(EvictionPolicy::TimeWindow));
        CHECK(kReserved != static_cast<uint8_t>(EvictionPolicy::LRU));
        CHECK(kReserved != static_cast<uint8_t>(EvictionPolicy::Distance));
        CHECK(kReserved != static_cast<uint8_t>(EvictionPolicy::TimeWindow));
    }

    TEST_CASE(EvictionPolicy_TilemapBudget_DefaultIsLRU) {
        TilemapBudget b{};
        // §18.1 defaults.
        CHECK(b.eviction == EvictionPolicy::LRU);
        CHECK_INT_EQ(b.maxChunksLoaded,    uint32_t{1024});
        CHECK_INT_EQ(b.maxIoBytesPerSec,   uint32_t{64u * 1024u * 1024u});
    }

    TEST_CASE(EvictionPolicy_SwitchPolicy_FallsThroughForReserved) {
        // In C++14+, casting an out-of-range integer to a scoped
        // enum IS valid as long as the result fits in the
        // underlying type; the value is "in range" of the type
        // even if no enumerator matches. This is the language
        // foundation the "reserved sentinel" convention relies
        // on. Verify: 0xFF is a representable EvictionPolicy.
        constexpr EvictionPolicy reserved =
            static_cast<EvictionPolicy>(0xFF);
        CHECK(reserved != EvictionPolicy::LRU);
        CHECK(reserved != EvictionPolicy::Distance);
        CHECK(reserved != EvictionPolicy::TimeWindow);
    }

TEST_SUITE_END