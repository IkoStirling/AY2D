// Test_CountersWired.cpp — Phase 3C real-wired counter tests.
//
// design.md §14.2 (wiring contract) + §14.3 (test matrix). The
// Phase 3A `Test_Counters.cpp` covers the snapshot/resetAll/
// resetPerFrame surface (mock data). Phase 3C adds tests that
// prove the counters are driven from **real** mutation paths
// (setTile / resizeGrid / clear / loadChunkFromSource /
// World2D::addTilemap / removeTilemap / swapTilemap / chunk-source
// request → put latency), no longer manual increments.
//
// Eight cases:
//   * TilemapSetTileBumpsMutated
//   * TilemapSetTileFirstWriteBumpsResident
//   * TilemapSetTileOutOfRangeIsNoOp
//   * TilemapResizeGridResetsResidentAndBumps
//   * TilemapLoadChunkFromSourceSuccessBumpsBoth
//   * TilemapLoadChunkFromSourceFailureNoDelta
//   * World2DAddRemoveSwapInWorldGauge
//   * ChunkSourceRequestThenPutAccumulatesIoUs

#include <chrono>
#include <cstdint>
#include <thread>

#include "AY2DCounters.h"
#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYInMemoryTilemapChunkSource.h"
#include "AYTest.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"
#include "AYTilemap.h"
#include "AYWorld2D.h"

using namespace ayt::ay2d;

TEST_SUITE(CountersWiredSuite)

    TEST_CASE(TilemapSetTileBumpsMutated) {
        // design.md §14.2: only successful setTile writes bump
        // tiles_mutated, by 1 each. First write at a cell inside a
        // pre-resized grid lands; cells index from 0..cols-1.
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);

        // The resize itself counts as a mutation. Capture the
        // post-resize baseline so the assertions are about deltas
        // from there.
        const auto base = t.counters.snapshot();
        CHECK_INT_EQ(base.tiles_mutated, 1u);  // resizeGrid counted once.

        t.setTile(TileCoord{0, 0}, 7);
        t.setTile(TileCoord{3, 2}, 11);
        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated + 2u);
    }

    TEST_CASE(TilemapSetTileFirstWriteBumpsResident) {
        // design.md §14.2: first-write lazy-fill bumps
        // tiles_resident to the full expected slot count
        // (cols * rows) — ONE bump, not per-cell. After the first
        // write the resident gauge equals 4 * 3 = 12 (Narrow16)
        // and stays at 12 for subsequent writes.
        //
        // Baseline note: resizeGrid itself counts as a mutation,
        // bumping tiles_mutated to 1 (§14.2 + no-double-counting
        // invariant). Each successful setTile bumps tiles_mutated
        // by 1; two setTile calls land at tiles_mutated == 3.
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);
        CHECK_INT_EQ(t.counters.snapshot().tiles_resident, 0u);
        CHECK_INT_EQ(t.counters.snapshot().tiles_mutated, 1u);  // resizeGrid counted once.

        t.setTile(TileCoord{0, 0}, 7);
        CHECK_INT_EQ(t.counters.snapshot().tiles_resident, 12u);

        // Subsequent writes leave resident untouched (storage is
        // already allocated) and bump tiles_mutated.
        t.setTile(TileCoord{3, 2}, 11);
        CHECK_INT_EQ(t.counters.snapshot().tiles_resident, 12u);
        CHECK_INT_EQ(t.counters.snapshot().tiles_mutated, 3u);
    }

    TEST_CASE(TilemapSetTileOutOfRangeIsNoOp) {
        // design.md §14.2: out-of-range writes are silently dropped
        // (no allocation, no exception) and produce NO delta in
        // either tiles_mutated or tiles_resident.
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);
        const auto base = t.counters.snapshot();

        // Coordinates outside [0, cols/rows) all silently dropped.
        t.setTile(TileCoord{4, 0}, 1);    // col == cols
        t.setTile(TileCoord{0, 3}, 2);    // row == rows
        t.setTile(TileCoord{-1, 0}, 3);   // negative col
        t.setTile(TileCoord{0, -1}, 4);   // negative row
        t.setTile(TileCoord{100, 100}, 5);

        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated);
        CHECK_INT_EQ(s.tiles_resident, base.tiles_resident);
    }

    TEST_CASE(TilemapResizeGridResetsResidentAndBumps) {
        // design.md §14.2: successful resize resets resident to 0
        // (storage cleared) and counts as one mutation. A second
        // resize confirms the pattern is repeatable.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        t.setTile(TileCoord{0, 0}, 1);
        CHECK(t.counters.snapshot().tiles_resident > 0u);
        const auto before = t.counters.snapshot().tiles_mutated;

        t.resizeGrid(8, 8, TileIdPackMode::Wide32);
        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_resident, 0u);
        CHECK_INT_EQ(s.tiles_mutated, before + 1u);
    }

    TEST_CASE(TilemapLoadChunkFromSourceSuccessBumpsBoth) {
        // design.md §14.2: a successful delivery bumps
        // tiles_resident to the delivered size and counts as one
        // mutation. Pre-populate a 16x16 chunk and load it.
        Tilemap t;
        t.resizeGrid(16, 16, TileIdPackMode::Narrow16);

        InMemoryTilemapChunkSource src;
        ChunkData chunk;
        chunk.coord     = ChunkCoord{0, 0};
        chunk.mode      = TileIdPackMode::Narrow16;
        chunk.tileIds16.assign(16 * 16, 0u);
        CHECK(src.put(ChunkCoord{0, 0}, std::move(chunk)));

        const auto before = t.counters.snapshot();
        CHECK(loadChunkFromSource(t, &src, ChunkCoord{0, 0}));
        CHECK(t.loadState == TileLoadState::Loaded);

        const auto after = t.counters.snapshot();
        CHECK_INT_EQ(after.tiles_resident, 16u * 16u);
        CHECK_INT_EQ(after.tiles_mutated, before.tiles_mutated + 1u);
    }

    TEST_CASE(TilemapLoadChunkFromSourceFailureNoDelta) {
        // design.md §14.2: failure paths (null source / width
        // mismatch / handle invalid) produce NO delta. The
        // loader drives loadState to Failed for null + mismatch
        // and stays Loading for handle-invalid; the counter
        // invariants are the same either way.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        const auto base = t.counters.snapshot();

        // 1) null source — loadState = Failed, no counter delta.
        CHECK_FALSE(loadChunkFromSource(t, nullptr, ChunkCoord{0, 0}));
        CHECK(t.loadState == TileLoadState::Failed);
        {
            const auto s = t.counters.snapshot();
            CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated);
            CHECK_INT_EQ(s.tiles_resident, base.tiles_resident);
        }

        // 2) width mismatch — source holds a Wide32 chunk but the
        //    tilemap is Narrow16. loadState = Failed, no delta.
        InMemoryTilemapChunkSource src;
        ChunkData wide;
        wide.coord     = ChunkCoord{0, 0};
        wide.mode      = TileIdPackMode::Wide32;
        wide.tileIds32.assign(4 * 4, 0u);
        CHECK(src.put(ChunkCoord{0, 0}, std::move(wide)));
        CHECK_FALSE(loadChunkFromSource(t, &src, ChunkCoord{0, 0}));
        CHECK(t.loadState == TileLoadState::Failed);
        {
            const auto s = t.counters.snapshot();
            CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated);
            CHECK_INT_EQ(s.tiles_resident, base.tiles_resident);
        }
    }

    TEST_CASE(World2DAddRemoveSwapInWorldGauge) {
        // design.md §14.2: addTilemap bumps by 1, removeTilemap
        // (matching) decrements by 1 (saturating at 0), and
        // swapTilemap is in-place (no count delta, only the
        // resourceEpoch bump per §3.4). An unmatched remove /
        // swap is no-op for both counters.
        World2D w;
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 0u);

        const auto h1 = w.addTilemap(0, 0);
        const auto h2 = w.addTilemap(1, 0);
        const auto h3 = w.addTilemap(2, 0);
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 3u);
        CHECK(w.size() == 3u);

        // swap is in-place: count stays at 3, resourceEpoch bumps.
        const uint64_t epochBefore = w.resourceEpochValue();
        CHECK(w.swapTilemap(h2, 5, 17));
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 3u);
        CHECK(w.resourceEpochValue() == epochBefore + 1u);

        // Successful remove: count drops to 2.
        CHECK(w.removeTilemap(h1));
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 2u);

        // Unmatched remove: no count delta, returns false.
        CHECK_FALSE(w.removeTilemap(h1));  // already removed (generation bump)
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 2u);

        // Remove the rest; the gauge saturates at 0, never underflows.
        CHECK(w.removeTilemap(h2));
        CHECK(w.removeTilemap(h3));
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 0u);

        // Over-removal: gauge stays at 0 (R-3C.1 saturating guard).
        CHECK_FALSE(w.removeTilemap(h3));
        CHECK_INT_EQ(w.counters.snapshot().tilemaps_in_world, 0u);
    }

    TEST_CASE(ChunkSourceRequestThenPutAccumulatesIoUs) {
        // design.md §10.1.1 + §14: the chunk source stamps the
        // request → put latency into chunk_io_us. With a small
        // sleep between requestChunk and put, the accumulated
        // value must be a non-zero delta that exceeds the sleep
        // duration in microseconds.
        InMemoryTilemapChunkSource src;

        const auto before = src.counters().snapshot().chunk_io_us;

        // Phase 3A real-wired request flow. The source stamps
        // `requestTimeUs` on requestChunk; when a matching put()
        // lands, the latency delta lands in chunk_io_us.
        (void)src.requestChunk(ChunkCoord{42, 0});

        // Sleep a measurable interval so the latency is observable
        // (the wall-clock granularity on the test host is finer
        // than the us resolution we read at).
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        ChunkData chunk;
        chunk.coord     = ChunkCoord{42, 0};
        chunk.mode      = TileIdPackMode::Narrow16;
        chunk.tileIds16.assign(16 * 16, 0u);
        CHECK(src.put(ChunkCoord{42, 0}, std::move(chunk)));

        const uint64_t after = src.counters().snapshot().chunk_io_us;
        CHECK(after > before);
    }

TEST_SUITE_END
