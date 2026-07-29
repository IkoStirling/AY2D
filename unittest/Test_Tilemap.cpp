// Test_Tilemap.cpp — Phase 2 functional tests for Tilemap.
//
// Mirrors AYPhysics sibling pattern: each Test_*.cpp is a stand-alone
// TU. Header-only types (TileCoord, ChunkRequestHandle, AtlasDesc)
// verify the placeholder contract; .cpp-backed types (Tilemap::setTile
// / getTile / loadChunkFromSource via InMemoryTilemapChunkSource)
// get round-trip coverage.

#include "AYTilemap.h"

#include <vector>

#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYInMemoryTilemapChunkSource.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(TilemapSuite)

    TEST_CASE(DefaultTilemapIsEmptyAndUnloaded) {
        Tilemap t;
        CHECK_INT_EQ(t.cols, 0u);
        CHECK_INT_EQ(t.rows, 0u);
        CHECK_INT_EQ(t.tileCount(), 0u);
        CHECK(t.loadState == TileLoadState::Unloaded);
    }

    TEST_CASE(SetAndGetTileRoundTripNarrow) {
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);
        CHECK(t.cols == 4u);
        CHECK(t.rows == 3u);
        CHECK_INT_EQ(t.tileCount(), 12u);

        t.setTile(TileCoord{0, 0}, 7);
        t.setTile(TileCoord{3, 2}, 11);
        t.setTile(TileCoord{1, 1}, 42);

        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 7u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{3, 2})), 11u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{1, 1})), 42u);
        // Untouched cell returns defaultTileId (0).
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{2, 2})), 0u);
    }

    TEST_CASE(OutOfRangeWritesAreSilentlyDropped) {
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);

        // Negative coords — silently dropped, defaultTileId still wins
        // when reading back.
        t.setTile(TileCoord{-1, 0},  1);
        t.setTile(TileCoord{0,  -1}, 2);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 0u);

        // Coords past the edge.
        t.setTile(TileCoord{4, 0}, 5);   // col==cols
        t.setTile(TileCoord{0, 3}, 6);   // row==rows
        t.setTile(TileCoord{99, 99}, 7);
        // Reads at valid in-bounds cells still get the right answer;
        // reads at out-of-range cells return defaultTileId.
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{99, 99})), 0u);
    }

    TEST_CASE(NarrowRejectsWideTileId) {
        // 0x10000 == 65536 is one past Narrow16's 16-bit capacity.
        // Setting it on a Narrow tilemap is silently a no-op (so the
        // data store stays valid; conversion via .aytilemap loader
        // does the contract rejection at load time, not draw time).
        Tilemap t;
        t.resizeGrid(2, 2, TileIdPackMode::Narrow16);
        t.setTile(TileCoord{0, 0}, 0x10000u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 0u);

        // Same payload on a Wide32 tilemap is accepted.
        t.resizeGrid(2, 2, TileIdPackMode::Wide32);
        t.setTile(TileCoord{0, 0}, 0x10000u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 0x10000u);
    }

    TEST_CASE(LoadChunkFromInMemorySourceSwapsStorage) {
        InMemoryTilemapChunkSource src{ /*capacity=*/0 };

        ChunkData chunk;
        chunk.coord = ChunkCoord{0, 0};
        chunk.mode  = TileIdPackMode::Narrow16;
        chunk.tileIds16.assign(16 * 16, 0u);
        for (uint32_t i = 0; i < chunk.tileIds16.size(); ++i) {
            chunk.tileIds16[i] = static_cast<uint16_t>(i & 0xFFFFu);
        }
        CHECK(src.put(chunk.coord, chunk));

        Tilemap t;
        t.resizeGrid(16, 16, TileIdPackMode::Narrow16);
        t.defaultTileId = 0xFFFFu;

        CHECK(loadChunkFromSource(t, &src, ChunkCoord{0, 0}));
        CHECK(t.loadState == TileLoadState::Loaded);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{15, 15})), 0xFFu);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{8, 8})), 0x88u);
    }

    TEST_CASE(LoadChunkWidthMismatchMarksFailed) {
        // design.md §6.2: width mismatch is a contract violation, no
        // partial-up. Source provides Wide32, Tilemap is Narrow.
        InMemoryTilemapChunkSource src{0};

        ChunkData chunk;
        chunk.coord = ChunkCoord{0, 0};
        chunk.mode  = TileIdPackMode::Wide32;
        chunk.tileIds32.assign(16 * 16, 0u);
        src.put(chunk.coord, chunk);

        Tilemap t;
        t.resizeGrid(16, 16, TileIdPackMode::Narrow16);

        CHECK_FALSE(loadChunkFromSource(t, &src, ChunkCoord{0, 0}));
        CHECK(t.loadState == TileLoadState::Failed);
    }

    TEST_CASE(LoadChunkFromNullSourceMarksFailed) {
        // Source-null short-circuit: cannot drive loadState Loading and
        // then leave it in Failed; the early-out should be `Failed`
        // (design.md F-18).
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);

        CHECK_FALSE(loadChunkFromSource(t, nullptr, ChunkCoord{0, 0}));
        CHECK(t.loadState == TileLoadState::Failed);
    }

    TEST_CASE(IsInRangeContract) {
        Tilemap t;
        CHECK_FALSE(t.isInRange(TileCoord{0, 0}));   // cols=0,rows=0
        t.resizeGrid(2, 2, TileIdPackMode::Narrow16);
        CHECK(t.isInRange(TileCoord{0, 0}));
        CHECK(t.isInRange(TileCoord{1, 1}));
        CHECK_FALSE(t.isInRange(TileCoord{2, 2}));
        CHECK_FALSE(t.isInRange(TileCoord{-1, 0}));
    }

    TEST_CASE(DefaultTileIdAppliedOnRead) {
        Tilemap t;
        t.resizeGrid(2, 2, TileIdPackMode::Narrow16);
        t.defaultTileId = 42;

        // Untouched cell reads as defaultTileId.
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 42u);
        // Override writes its own id.
        t.setTile(TileCoord{0, 0}, 1);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 1u);
        // Adjacent untouched still reads defaultTileId.
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{1, 0})), 42u);
    }

    TEST_CASE(ResizingClearsStorage) {
        Tilemap t;
        t.resizeGrid(2, 2, TileIdPackMode::Narrow16);
        t.setTile(TileCoord{0, 0}, 7);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 7u);
        // resizeGrid zeroes storage (loadState back to Unloaded for
        // editor / streaming to re-issue).
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        CHECK(t.tileIds16.empty());
        CHECK(t.loadState == TileLoadState::Unloaded);
    }

TEST_SUITE_END
