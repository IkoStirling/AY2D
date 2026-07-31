// Test_TileLoadStateEnum.cpp - P3J.6 / A-7 TileLoadState enum coverage.
//
// design.md §13.30. The TileLoadState enum
// (include/AYTileLoadState.h, Phase 2 F-18) has 4 values
// (Unloaded=0, Loading=1, Loaded=2, Failed=3) backed by
// uint8_t. ECS inspectors + the Editor Inspector consume
// this enum (design.md §11 F-18). P3J.6 locks the
// underlying-type discipline, the canonical codes, and
// the 0xFF reserved-sentinel convention (mirrors A-12).

#include <type_traits>

#include "AYTileLoadState.h"
#include "AYTilemap.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(TileLoadStateEnumSuite)

    TEST_CASE(TileLoadState_UnderlyingTypeIsUint8) {
        static_assert(sizeof(TileLoadState) == 1u,
                      "TileLoadState must be 1 byte (uint8_t).");
        static_assert(std::is_same<
                          std::underlying_type_t<TileLoadState>,
                          uint8_t>::value,
                      "TileLoadState underlying type must be uint8_t.");
        CHECK_TRUE(true);
    }

    TEST_CASE(TileLoadState_CanonicalCodes_ZeroOneTwoThree) {
        CHECK_INT_EQ(static_cast<uint8_t>(TileLoadState::Unloaded), uint8_t{0});
        CHECK_INT_EQ(static_cast<uint8_t>(TileLoadState::Loading),  uint8_t{1});
        CHECK_INT_EQ(static_cast<uint8_t>(TileLoadState::Loaded),   uint8_t{2});
        CHECK_INT_EQ(static_cast<uint8_t>(TileLoadState::Failed),   uint8_t{3});
    }

    TEST_CASE(TileLoadState_FFIsReservedSentinel) {
        constexpr uint8_t kReserved = 0xFFu;
        CHECK(kReserved > static_cast<uint8_t>(TileLoadState::Failed));
        CHECK(kReserved != static_cast<uint8_t>(TileLoadState::Unloaded));
        CHECK(kReserved != static_cast<uint8_t>(TileLoadState::Loading));
        CHECK(kReserved != static_cast<uint8_t>(TileLoadState::Loaded));
        CHECK(kReserved != static_cast<uint8_t>(TileLoadState::Failed));
    }

    TEST_CASE(TileLoadState_Tilemap_DefaultIsUnloaded) {
        Tilemap t{};
        // §11 F-18 default state on a fresh tilemap.
        CHECK(t.loadState == TileLoadState::Unloaded);
        CHECK_INT_EQ(t.tileWidth,  uint32_t{0});
        CHECK_INT_EQ(t.tileHeight, uint32_t{0});
        CHECK_INT_EQ(t.defaultTileId, uint32_t{0});
    }

TEST_SUITE_END