// Test_ChunkSourceEraseByKey.cpp - KI-3I-1 fix tests.
//
// design.md §13.24 / §18.7. The fix adds an `evictions_lru` bump
// to `InMemoryTilemapChunkSource::eraseByKey` so the per-key
// helper matches the bulk-path semantics. The bulk paths
// (`evictIfNeeded` / `evictDownTo`) use direct cache.erase()
// rather than going through `eraseByKey`, so the bump is not
// double-counted.

#include "AYInMemoryTilemapChunkSource.h"

#include "AYChunkData.h"
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

TEST_SUITE(ChunkSourceEraseByKeySuite)

    TEST_CASE(EraseByKey_BumpsEvictionsLruByOne) {
        // cap=10, put three chunks, then eraseByKey one.
        // Expected: resident drops to 2, evictions_lru == 1.
        InMemoryTilemapChunkSource src{10};

        CHECK(src.put(ChunkCoord{0, 0}, makeChunk(0, 0, 1)));
        CHECK(src.put(ChunkCoord{1, 0}, makeChunk(1, 0, 2)));
        CHECK(src.put(ChunkCoord{2, 0}, makeChunk(2, 0, 3)));

        CHECK_INT_EQ(src.residentCount(), 3u);
        CHECK_INT_EQ(src.counters().evictions_lru.load(
                         std::memory_order_relaxed), 0u);

        const auto key = InMemoryTilemapChunkSource::packKey(ChunkCoord{1, 0});
        src.eraseByKey(key);

        CHECK_INT_EQ(src.residentCount(), 2u);
        CHECK_INT_EQ(src.counters().evictions_lru.load(
                         std::memory_order_relaxed), 1u);
        CHECK_FALSE(src.isResident(ChunkCoord{1, 0}));
        CHECK(src.isResident(ChunkCoord{0, 0}));
        CHECK(src.isResident(ChunkCoord{2, 0}));
    }

    TEST_CASE(EraseByKey_NoMatch_DoesNotBump) {
        // Empty cache; eraseByKey on a non-existent key
        // must not bump the counter (no-op early return).
        InMemoryTilemapChunkSource src{10};

        const auto key = InMemoryTilemapChunkSource::packKey(ChunkCoord{99, 99});
        src.eraseByKey(key);

        CHECK_INT_EQ(src.residentCount(), 0u);
        CHECK_INT_EQ(src.counters().evictions_lru.load(
                         std::memory_order_relaxed), 0u);
    }

    TEST_CASE(EraseByKey_DoesNotInterfereWithBulkEvictionCounter) {
        // cap=2; put (0,0)/(1,0)/(2,0). Third put triggers
        // evictIfNeeded -> evictions_lru == 1, (0,0) evicted (LRU).
        // Cache now holds (1,0) + (2,0).
        // Then eraseByKey(packKey(1,0)) -> evictions_lru == 2,
        // only (2,0) remains.
        InMemoryTilemapChunkSource src{2};

        CHECK(src.put(ChunkCoord{0, 0}, makeChunk(0, 0, 1)));
        CHECK(src.put(ChunkCoord{1, 0}, makeChunk(1, 0, 2)));
        CHECK(src.put(ChunkCoord{2, 0}, makeChunk(2, 0, 3)));

        CHECK_INT_EQ(src.residentCount(), 2u);
        CHECK_INT_EQ(src.counters().evictions_lru.load(
                         std::memory_order_relaxed), 1u);
        CHECK_FALSE(src.isResident(ChunkCoord{0, 0}));
        CHECK(src.isResident(ChunkCoord{1, 0}));
        CHECK(src.isResident(ChunkCoord{2, 0}));

        const auto key = InMemoryTilemapChunkSource::packKey(ChunkCoord{1, 0});
        src.eraseByKey(key);

        CHECK_INT_EQ(src.residentCount(), 1u);
        CHECK_INT_EQ(src.counters().evictions_lru.load(
                         std::memory_order_relaxed), 2u);
        CHECK(src.isResident(ChunkCoord{2, 0}));
    }

TEST_SUITE_END