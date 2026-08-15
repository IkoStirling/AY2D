// Test_TileIdPackMode.cpp — Phase 1+ stub.
//
// Asserts the TileIdPackMode enum from design.md §6.2 is well-formed:
//
//   * `Narrow16` is the default (locked at design §6.1).
//   * `Wide32` exists as a separate value.
//   * The enum is sized exactly `uint8_t` (one byte of L1 / L2 layout
//     budget per locked decision).
//
// Real .aytilemap loader behavior tests land in Phase 2+ alongside
// .aytilemap L1 format design + converter (design.md §9.1 + §9.2).

#include <cstdint>

#include "AY2D/Tilemap.h"
#include "AYTest.h"

using ayt::ay2d::TileIdPackMode;
using ayt::ay2d::Tilemap;

TEST_SUITE(TileIdPackModeSuite)

    TEST_CASE(Narrow16IsDefaultEnumValue) {
        // design.md §6.1: enum starts at 0; Narrow16 is the default.
        CHECK_INT_EQ(static_cast<uint8_t>(TileIdPackMode::Narrow16), 0u);
    }

    TEST_CASE(Wide32IsDistinctFromNarrow16) {
        CHECK(static_cast<uint8_t>(TileIdPackMode::Wide32) !=
               static_cast<uint8_t>(TileIdPackMode::Narrow16));
    }

    TEST_CASE(TilemapPlaceholderDefaultsToZero) {
        // design.md §6.1: tileWidth / tileHeight are 0 until the
        // .aytilemap loader populates them. The default-constructed
        // zero state is the "freshly constructed, never loaded"
        // sentinel.
        Tilemap t;
        CHECK_INT_EQ(t.tileWidth, 0u);
        CHECK_INT_EQ(t.tileHeight, 0u);
        CHECK_INT_EQ(t.defaultTileId, 0u);
    }

    TEST_CASE(TilemapEnumSizeIsByte) {
        // Locked decision §6.2: TileIdPackMode must be uint8_t to fit
        // inside the chunk header without bloating the L1 layout.
        CHECK_INT_EQ(sizeof(TileIdPackMode), 1u);
    }

TEST_SUITE_END
