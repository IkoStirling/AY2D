// Test_ChunkSourceRowCoalesce.cpp - P3J.9 / A-6 chunk-row coalesce tests.
//
// design.md §13.33. The InMemoryTilemapChunkSource adds a
// requestChunkRow(y, xStart, xEndExclusive) bulk API that
// dedupes within the row: a coord already in `_pending` returns
// the existing handle instead of issuing a new request + rate-gate
// charge.

#include "AYInMemoryTilemapChunkSource.h"

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

TEST_SUITE(ChunkSourceRowCoalesceSuite)

    TEST_CASE(RowRequest_AllNew_AllPending_HandlesDistinct) {
        InMemoryTilemapChunkSource src{0};
        auto handles = src.requestChunkRow(0, 0, 4);
        CHECK_INT_EQ(static_cast<int>(handles.size()), 4);
        for (const auto& h : handles) {
            CHECK(h.isValid());
            CHECK(h.index() != 0u);
        }
        // All four are distinct handles.
        CHECK(handles[0] != handles[1]);
        CHECK(handles[1] != handles[2]);
        CHECK(handles[2] != handles[3]);
    }

    TEST_CASE(RowRequest_FullDuplicate_ReturnsExistingHandles) {
        InMemoryTilemapChunkSource src{0};
        // Pre-issue 4 handles one at a time.
        ChunkRequestHandle h0 = src.requestChunk(ChunkCoord{0, 0});
        ChunkRequestHandle h1 = src.requestChunk(ChunkCoord{1, 0});
        ChunkRequestHandle h2 = src.requestChunk(ChunkCoord{2, 0});
        ChunkRequestHandle h3 = src.requestChunk(ChunkCoord{3, 0});
        const auto bytesBefore = src.counters().chunk_io_bytes.load(
            std::memory_order_relaxed);
        const auto rejectBefore = src.counters().chunk_io_reject.load(
            std::memory_order_relaxed);

        // Re-request the same row: all dedup.
        auto handles = src.requestChunkRow(0, 0, 4);
        CHECK_INT_EQ(static_cast<int>(handles.size()), 4);
        CHECK(handles[0] == h0);
        CHECK(handles[1] == h1);
        CHECK(handles[2] == h2);
        CHECK(handles[3] == h3);

        // No new rate-gate charge; no new reject count.
        CHECK_INT_EQ(static_cast<int>(src.counters().chunk_io_bytes.load(
                         std::memory_order_relaxed)),
                     static_cast<int>(bytesBefore));
        CHECK_INT_EQ(static_cast<int>(src.counters().chunk_io_reject.load(
                         std::memory_order_relaxed)),
                     static_cast<int>(rejectBefore));
    }

    TEST_CASE(RowRequest_PartialDuplicate_MixedBehavior) {
        InMemoryTilemapChunkSource src{0};
        // Pre-issue ONLY (1, 0).
        ChunkRequestHandle h1 = src.requestChunk(ChunkCoord{1, 0});

        // Row of 4: (0,0), (1,0), (2,0), (3,0).
        auto handles = src.requestChunkRow(0, 0, 4);
        CHECK_INT_EQ(static_cast<int>(handles.size()), 4);
        // (1,0) dedupes; the other three are new.
        CHECK(handles[1] == h1);
        CHECK(handles[0] != h1);
        CHECK(handles[2] != h1);
        CHECK(handles[3] != h1);
        // All four distinct.
        CHECK(handles[0] != handles[1]);
        CHECK(handles[0] != handles[2]);
        CHECK(handles[0] != handles[3]);
        CHECK(handles[1] != handles[2]);
        CHECK(handles[1] != handles[3]);
        CHECK(handles[2] != handles[3]);
    }

    TEST_CASE(RowRequest_EmptyRange_NoOp) {
        InMemoryTilemapChunkSource src{0};
        const auto bytesBefore = src.counters().chunk_io_bytes.load(
            std::memory_order_relaxed);
        auto handles = src.requestChunkRow(0, 5, 5);
        CHECK_INT_EQ(static_cast<int>(handles.size()), 0);
        CHECK_INT_EQ(static_cast<int>(src.counters().chunk_io_bytes.load(
                         std::memory_order_relaxed)),
                     static_cast<int>(bytesBefore));
    }

    TEST_CASE(RowRequest_NegativeRange_NoOp) {
        InMemoryTilemapChunkSource src{0};
        // xEndExclusive < xStart: half-open, no entries.
        auto handles = src.requestChunkRow(0, 5, 3);
        CHECK_INT_EQ(static_cast<int>(handles.size()), 0);

        // Negative coords still work if range is non-empty.
        auto neg = src.requestChunkRow(-1, -3, 0);
        CHECK_INT_EQ(static_cast<int>(neg.size()), 3);
        for (const auto& h : neg) CHECK(h.isValid());
    }

    TEST_CASE(RowRequest_RateGateHonored_PerChunkCharge) {
        // Each accepted request charges the rate gate. The
        // `nominalChunkBytes` value is private to the source; we
        // derive it by inspection: P3G hard-codes 32 KB per
        // Narrow16 chunk. Set the gate to 2 * nominal so two
        // charges fit and the third is rejected.
        InMemoryTilemapChunkSource src{0};
        constexpr uint64_t kNominalNarrow16 = 32ull * 1024ull;
        src.setMaxIoBytesPerSec(2ull * kNominalNarrow16);

        auto handles = src.requestChunkRow(0, 0, 3);
        CHECK_INT_EQ(static_cast<int>(handles.size()), 3);
        // First two: valid (rate-gate accepted).
        CHECK(handles[0].isValid());
        CHECK(handles[1].isValid());
        // Third: rejected at the gate, invalid handle.
        CHECK_FALSE(handles[2].isValid());
        // chunk_io_reject == 1.
        CHECK_INT_EQ(static_cast<int>(src.counters().chunk_io_reject.load(
                         std::memory_order_relaxed)), 1);
    }

TEST_SUITE_END