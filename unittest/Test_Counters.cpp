// Test_Counters.cpp — Phase 3 Ay2DCounters + chunk-source wiring tests.
//
// design.md §10.1.1: AY2D ships six ay2d_<metric>_<unit> counters
// as instance fields on the owning object. This file exercises:
//   * Default-constructed `Ay2DCounters` is all zero.
//   * `snapshot()` round-trips correctly.
//   * `resetAll()` zeros every field.
//   * `resetPerFrame()` clears only the per-frame fields.
//   * `InMemoryTilemapChunkSource::put()` populates chunk_io_bytes
//     and chunk_resident_count.
//   * `counters()` accessor on the chunk source returns the same
//     instance.

#include <cstdint>
#include <thread>

#include "AY2DCounters.h"
#include "AYChunkData.h"
#include "AYInMemoryTilemapChunkSource.h"
#include "AYTest.h"
#include "AYTileCoord.h"

using namespace ayt::ay2d;

TEST_SUITE(CountersSuite)

    TEST_CASE(DefaultsAreZero) {
        Ay2DCounters c;
        const auto s = c.snapshot();
        CHECK_INT_EQ(s.chunk_io_us, 0u);
        CHECK_INT_EQ(s.chunk_io_bytes, 0u);
        CHECK_INT_EQ(s.chunk_resident_count, 0u);
        CHECK_INT_EQ(s.atlas_bytes, 0u);
        CHECK_INT_EQ(s.draw2d_items, 0u);
        CHECK_INT_EQ(s.draw2d_pass_us, 0u);
    }

    TEST_CASE(ResetAllZerosEveryField) {
        Ay2DCounters c;
        c.chunk_io_us.store(100);
        c.chunk_io_bytes.store(200);
        c.atlas_bytes.store(300);
        c.draw2d_items.store(7);
        c.draw2d_pass_us.store(50);
        c.resetAll();
        const auto s = c.snapshot();
        CHECK_INT_EQ(s.chunk_io_us, 0u);
        CHECK_INT_EQ(s.chunk_io_bytes, 0u);
        CHECK_INT_EQ(s.atlas_bytes, 0u);
        CHECK_INT_EQ(s.draw2d_items, 0u);
        CHECK_INT_EQ(s.draw2d_pass_us, 0u);
    }

    TEST_CASE(ResetPerFrameClearsOnlyPerFrameFields) {
        Ay2DCounters c;
        c.chunk_io_us.store(100);
        c.chunk_io_bytes.store(200);
        c.atlas_bytes.store(300);
        c.draw2d_items.store(7);
        c.draw2d_pass_us.store(50);
        c.resetPerFrame();
        const auto s = c.snapshot();
        // Cumulative fields stay.
        CHECK_INT_EQ(s.chunk_io_us, 100u);
        CHECK_INT_EQ(s.chunk_io_bytes, 200u);
        CHECK_INT_EQ(s.atlas_bytes, 300u);
        // Per-frame fields are zeroed.
        CHECK_INT_EQ(s.draw2d_items, 0u);
        CHECK_INT_EQ(s.draw2d_pass_us, 0u);
    }

    TEST_CASE(ChunkSourceCountersPopulateOnPut) {
        // design.md §10.1.1 row 1+2: chunk_io_bytes + chunk_resident_count.
        InMemoryTilemapChunkSource src;
        ChunkData data;
        data.tileIds16.assign(16 * 16, 0u);  // 256 * 2 bytes = 512 bytes
        src.put(ChunkCoord{0, 0}, std::move(data));
        const auto s = src.counters().snapshot();
        CHECK_INT_EQ(s.chunk_io_bytes, 16u * 16u * sizeof(uint16_t));
        CHECK_INT_EQ(s.chunk_resident_count, 1u);
    }

    TEST_CASE(ChunkSourceCountersDecrementOnEvict) {
        // capacity = 2 holds 2 chunks; the 3rd put evicts the
        // LRU and chunk_resident_count stays at 2.
        InMemoryTilemapChunkSource src(2u);
        ChunkData a; a.tileIds16.assign(16, 0u);
        ChunkData b; b.tileIds16.assign(16, 0u);
        ChunkData c; c.tileIds16.assign(16, 0u);
        src.put(ChunkCoord{0, 0}, std::move(a));
        src.put(ChunkCoord{1, 0}, std::move(b));
        CHECK_INT_EQ(src.counters().snapshot().chunk_resident_count, 2u);
        src.put(ChunkCoord{2, 0}, std::move(c));
        // Eviction keeps the count at the cap.
        CHECK_INT_EQ(src.counters().snapshot().chunk_resident_count, 2u);
        // And the bytes counter accumulates the cumulative
        // bytes delivered (including the evicted chunk).
        // Each chunk is 16 tileIds16 * 2 bytes = 32 bytes; 3 puts
        // add up to 96 bytes (the 3rd chunk's bytes are still
        // counted even though the LRU evicted (0,0)).
        CHECK_INT_EQ(src.counters().snapshot().chunk_io_bytes,
                     3u * 16u * sizeof(uint16_t));
    }

    TEST_CASE(ChunkSourceCountersAccessorReturnsSameInstance) {
        InMemoryTilemapChunkSource src;
        // Same identity check: counters() returns the same Ay2DCounters.
        CHECK(&src.counters() == &src.counters());
    }

TEST_SUITE_END
