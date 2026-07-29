// Test_InMemoryTilemapChunkSource.cpp — Phase 2 chunk-source backend
// tests.
//
// design.md §6.2 LRU + Phase 2 offline cache flow. Exercises:
//   - put() / tryGetChunk() round-trip
//   - capacity-driven LRU eviction
//   - isResident hot-path predicate
//   - requestChunk returns valid handle, even when chunk isn't
//     resident yet (handle bookkeeping so cancel() does the right
//     thing later).

#include "AYInMemoryTilemapChunkSource.h"

#include <vector>

#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYTileCoord.h"

#include "AYTest.h"

using namespace ayt::ay2d;

namespace {

ChunkData makeChunk(int32_t x, int32_t y, uint16_t id) {
    ChunkData c;
    c.coord = ChunkCoord{x, y};
    c.mode  = TileIdPackMode::Narrow16;
    c.tileIds16.assign(16 * 16, id);
    return c;
}

} // namespace

TEST_SUITE(InMemoryChunkSourceSuite)

    TEST_CASE(PutThenGetRoundTrips) {
        InMemoryTilemapChunkSource src{0};

        ChunkData stored = makeChunk(2, 3, 0xAAu);
        CHECK(src.put(stored.coord, stored));
        CHECK(src.isResident(ChunkCoord{2, 3}));
        CHECK_INT_EQ(src.residentCount(), 1u);

        ChunkData got;
        CHECK(src.tryGetChunk(ChunkCoord{2, 3}, got));
        CHECK(got.coord.x == 2);
        CHECK(got.coord.y == 3);
        CHECK(got.mode == TileIdPackMode::Narrow16);
        CHECK_INT_EQ(static_cast<uint32_t>(got.tileIds16.size()), 256u);
        CHECK_INT_EQ(static_cast<uint32_t>(got.tileIds16.front()), 0xAAu);
    }

    TEST_CASE(PutRefusesDoubleInsert) {
        InMemoryTilemapChunkSource src{0};
        CHECK(src.put(ChunkCoord{0, 0}, makeChunk(0, 0, 1)));
        CHECK_FALSE(src.put(ChunkCoord{0, 0}, makeChunk(0, 0, 2)));
        CHECK_INT_EQ(src.residentCount(), 1u);
    }

    TEST_CASE(EvictionAtCapacity) {
        // Capacity 2 — third put evicts the least-recently-used.
        InMemoryTilemapChunkSource src{2};

        src.put(ChunkCoord{0, 0}, makeChunk(0, 0, 1));
        src.put(ChunkCoord{1, 0}, makeChunk(1, 0, 2));
        CHECK_INT_EQ(src.residentCount(), 2u);
        CHECK(src.isResident(ChunkCoord{0, 0}));
        CHECK(src.isResident(ChunkCoord{1, 0}));

        // Touch (0,0) so it's the MRU; (1,0) becomes LRU.
        ChunkData probe;
        CHECK(src.tryGetChunk(ChunkCoord{0, 0}, probe));
        (void)probe;

        // Insert (2,0); expect (1,0) evicted.
        src.put(ChunkCoord{2, 0}, makeChunk(2, 0, 3));
        CHECK_INT_EQ(src.residentCount(), 2u);
        CHECK(src.isResident(ChunkCoord{0, 0}));
        CHECK(src.isResident(ChunkCoord{2, 0}));
        CHECK_FALSE(src.isResident(ChunkCoord{1, 0}));
    }

    TEST_CASE(RequestChunkReturnsValidHandleEvenWhenNotResident) {
        InMemoryTilemapChunkSource src{0};
        const ChunkRequestHandle h = src.requestChunk(ChunkCoord{7, 9});
        CHECK(h.isValid());

        // Until the matching put lands, tryGetChunk returns false.
        ChunkData got;
        CHECK_FALSE(src.tryGetChunk(ChunkCoord{7, 9}, got));
    }

    TEST_CASE(CancelIsNoOpForUnknownHandle) {
        InMemoryTilemapChunkSource src{0};
        // Invalid handle -> no-op (no exception, no state mutation).
        src.cancelChunk(ChunkRequestHandle{});
        src.cancelChunk(ChunkRequestHandle{0xDEADBEEFu});
        CHECK_INT_EQ(src.residentCount(), 0u);
    }

    TEST_CASE(GetForUnknownChunkReturnsFalse) {
        InMemoryTilemapChunkSource src{0};
        ChunkData got;
        CHECK_FALSE(src.tryGetChunk(ChunkCoord{99, 99}, got));
        CHECK_FALSE(src.isResident(ChunkCoord{99, 99}));
    }

TEST_SUITE_END
