// Test_World2DRemoveTilemapPurge.cpp — P3I.2 / §13.21 + §18.4.
//
// design.md §13.21: `World2D::removeTilemap` now invokes
// `chunkSource->purgeChunks()` BEFORE erasing the entry (L-3I-5
// strict ordering). §18.4: in-AY2D chunk-source model =
// one source per tilemap; purge = purge-all.
//
// The eight cases below lock:
//   * eviction count (`evictions_lru`) on a successful purge
//   * epoch +1 on a successful remove (no extra bump from purge)
//   * `tilemaps_in_world` saturating decrement (no double-decrement
//     from the purge path)
//   * 2-arg `addTilemap` (legacy nullptr-source path) unaffected
//   * `swapTilemap` does NOT purge (§13.15 lock)
//   * `purgeChunks()` idempotent
//   * pending-load cancellation does NOT inflate `evictions_lru`
//   * direct `purgeChunks()` (no World2D in the call) does NOT
//     bump `resourceEpoch` (§3.4 lock)
//
// All assertions use real `counters().snapshot()` deltas — same
// discipline as P3C Test_CountersWired + P3G Test_ChunkSourceBudget.

#include "AYInMemoryTilemapChunkSource.h"
#include "AYWorld2D.h"

#include "AY2DCounters.h"
#include "AYChunkData.h"
#include "AYTest.h"

using namespace ayt::ay2d;

namespace {

// Build a fresh chunk payload (mirrors Test_CpuSoftCap helper).
ChunkData makeChunk(ChunkCoord coord) {
    ChunkData c;
    c.coord = coord;
    c.mode  = TileIdPackMode::Narrow16;
    c.tileIds16.assign(16u * 16u, 0u);
    return c;
}

// Pre-populate `src` with N distinct chunks. Returns the number
// of resident chunks after the put.
uint32_t seed(InMemoryTilemapChunkSource& src, uint32_t n) noexcept {
    for (uint32_t i = 0; i < n; ++i) {
        (void)src.put(ChunkCoord{int32_t(i), 0},
                      makeChunk(ChunkCoord{int32_t(i), 0}));
    }
    return src.residentCount();
}

} // namespace

TEST_SUITE(World2DRemoveTilemapPurgeSuite)

    TEST_CASE(RemoveTilemap_WithSource_PurgesAllResidentChunks) {
        // §13.21 L-3I-5 case 1: capacity 10, 8 chunks resident,
        // remove the tilemap -> 0 resident + evictions_lru == 8.
        InMemoryTilemapChunkSource src(/*capacity=*/10u);
        CHECK_INT_EQ(seed(src, 8u), 8u);
        CHECK_INT_EQ(src.counters().snapshot().evictions_lru, 0u);

        World2D w;
        const TilemapHandle h = w.addTilemap(/*layer=*/0u,
                                             /*sortingKey=*/0u,
                                             &src);
        CHECK_TRUE(h.id != 0u);
        CHECK_INT_EQ(src.residentCount(), 8u);

        CHECK_TRUE(w.removeTilemap(h));
        CHECK_INT_EQ(src.residentCount(), 0u);
        CHECK_INT_EQ(src.counters().snapshot().evictions_lru, 8u);
    }

    TEST_CASE(RemoveTilemap_WithSource_BumpsEpochExactlyOnce) {
        // §3.4 lock: chunk eviction is internal to removeTilemap
        // and must NOT contribute a second epoch bump.
        InMemoryTilemapChunkSource src(10u);
        seed(src, 8u);

        World2D w;
        const uint64_t epochBefore = w.resourceEpochValue();
        const TilemapHandle h = w.addTilemap(0u, 0u, &src);
        // addTilemap bumps epoch (verified separately).
        CHECK_INT_EQ(w.resourceEpochValue(), epochBefore + 1u);

        CHECK_TRUE(w.removeTilemap(h));
        CHECK_INT_EQ(w.resourceEpochValue(), epochBefore + 2u);

        // Second remove with the stale handle is a no-op (false)
        // and does NOT bump epoch further.
        CHECK_FALSE(w.removeTilemap(h));
        CHECK_INT_EQ(w.resourceEpochValue(), epochBefore + 2u);
    }

    TEST_CASE(RemoveTilemap_DecrementsTilemapsInWorldExactlyOnce) {
        // §13.15: tilemaps_in_world only -1 per remove (saturating).
        // The purge path inside removeTilemap must NOT trigger an
        // extra decrement.
        InMemoryTilemapChunkSource src(10u);
        seed(src, 5u);

        World2D w;
        const TilemapHandle h = w.addTilemap(0u, 0u, &src);
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 1u);

        CHECK_TRUE(w.removeTilemap(h));
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 0u);

        // Repeat on empty registry: saturating guard keeps it 0
        // (R-3C.1) and the unmatched remove returns false.
        CHECK_FALSE(w.removeTilemap(h));
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 0u);
    }

    TEST_CASE(RemoveTilemap_NullSource_LegacyOverloadUnaffected) {
        // §13.21: 2-arg addTilemap still works (delegates with
        // nullptr). removeTilemap with a null source returns true
        // and bumps epoch +1, no crash.
        World2D w;
        const TilemapHandle h = w.addTilemap(0u, 0u);  // nullptr source
        const uint64_t epochBefore = w.resourceEpochValue();
        CHECK_TRUE(w.removeTilemap(h));
        CHECK_INT_EQ(w.resourceEpochValue(), epochBefore + 1u);
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 0u);
    }

    TEST_CASE(SwapTilemap_DoesNotPurgeChunks) {
        // §13.15 lock: swapTilemap must NOT touch chunk source.
        // The bound source's resident count and evictions_lru are
        // unchanged after a swap; only epoch +1.
        InMemoryTilemapChunkSource src(10u);
        seed(src, 8u);

        World2D w;
        const TilemapHandle h = w.addTilemap(0u, 0u, &src);

        CHECK_TRUE(w.swapTilemap(h, /*newLayer=*/5u,
                                 /*newSortingKey=*/7u));
        CHECK_INT_EQ(src.residentCount(), 8u);
        CHECK_INT_EQ(src.counters().snapshot().evictions_lru, 0u);
        CHECK_TRUE(w.find(h) != nullptr);  // handle still valid
    }

    TEST_CASE(PurgeChunks_Idempotent) {
        // §13.21: calling purgeChunks twice in a row does not
        // double-count evictions and does not crash on the empty
        // cache.
        InMemoryTilemapChunkSource src(10u);
        seed(src, 8u);

        src.purgeChunks();
        CHECK_INT_EQ(src.residentCount(), 0u);
        CHECK_INT_EQ(src.counters().snapshot().evictions_lru, 8u);

        // Second call: nothing to evict, no inflation.
        src.purgeChunks();
        CHECK_INT_EQ(src.residentCount(), 0u);
        CHECK_INT_EQ(src.counters().snapshot().evictions_lru, 8u);
    }

    TEST_CASE(PurgeChunks_CancelsPending_NoEvictionCounterInflation) {
        // §13.21 L-3I-4: cancelling pending (never-resident)
        // requests does NOT bump evictions_lru. We seed 5
        // resident chunks and issue 3 `requestChunk` calls
        // (which add to _pending without populating the cache),
        // then purge; evictions_lru must equal the resident count
        // (5), NOT resident + pending (8).
        InMemoryTilemapChunkSource src(10u);
        seed(src, 5u);
        // Three pending requests, all with coords distinct from
        // the 5 resident ones (so they would not naturally land).
        (void)src.requestChunk(ChunkCoord{100, 100});
        (void)src.requestChunk(ChunkCoord{101, 100});
        (void)src.requestChunk(ChunkCoord{102, 100});

        src.purgeChunks();
        CHECK_INT_EQ(src.residentCount(), 0u);
        CHECK_INT_EQ(src.counters().snapshot().evictions_lru, 5u);
    }

    TEST_CASE(DirectPurge_DoesNotBumpResourceEpoch) {
        // §3.4 lock: a direct purge call (no World2D in the
        // stack) does NOT touch resourceEpoch. This is the
        // hook for the future cross-module consumer to call
        // purge without confusing World2D's epoch bookkeeping.
        InMemoryTilemapChunkSource src(10u);
        seed(src, 8u);

        World2D w;
        const uint64_t epochBefore = w.resourceEpochValue();
        src.purgeChunks();
        CHECK_INT_EQ(src.residentCount(), 0u);
        CHECK_INT_EQ(w.resourceEpochValue(), epochBefore);
    }

TEST_SUITE_END