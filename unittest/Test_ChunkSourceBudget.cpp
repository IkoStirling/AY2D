// Test_ChunkSourceBudget.cpp — Phase 3G chunk-source budget tests.
//
// design.md §18.5 (8-case matrix) + §18.6 (locks):
//   * R-3G.1: non-LRU policy = no-op + `false` return.
//   * R-3G.2: `chunk_io_reject` is cumulative (resetAll only).
//   * R-3G.3: sliding-window rate gate, 1-second window.
//   * R-3G.3a: `maxIoBytesPerSec == 0` disables the gate.
//   * R-3G.4: `setBudget` with non-LRU is a no-op; residency
//     side (`maxChunksResident`) is acknowledged but not wired.
//   * R-3G.7: deterministic tests use `advanceWindowForTest`
//     helper; no `sleep()` calls.
//
// All assertions use real `counters().snapshot()` deltas —
// same discipline as Phase 3C Test_CountersWired.

#include "AY2D/InMemoryTilemapChunkSource.h"

#include <chrono>

#include "AY2D/2DCounters.h"
#include "AY2D/ChunkData.h"
#include "AY2D/ChunkRequestHandle.h"
#include "AYTest.h"
#include "AY2D/TilemapChunkSource.h"

using namespace ayt::ay2d;

namespace {

// Build a fresh chunk payload for the rate-gate tests. The
// `InMemoryTilemapChunkSource` accepts arbitrary ChunkData;
// the rate gate uses the source's *nominal* size, not the
// payload's row*cell count.
ChunkData makeChunk(ChunkCoord coord, uint16_t fill = 0u) {
    ChunkData c;
    c.coord = coord;
    c.mode  = TileIdPackMode::Narrow16;
    // 16*16 narrow chunk (P3G's nominal).
    c.tileIds16.assign(16u * 16u, fill);
    return c;
}

} // namespace

TEST_SUITE(ChunkSourceBudgetSuite)

    TEST_CASE(SetCapacity0DisablesCap) {
        // §18.5 case 1 — R-3G.7 backward compat. Construct
        // with cap=4, then push it to 0: cache must allow
        // unlimited entries.
        InMemoryTilemapChunkSource src(/*capacity=*/4u);
        src.setCapacity(0u);
        // 6 distinct chunks must all fit (unlimited mode).
        for (uint32_t i = 0; i < 6u; ++i) {
            CHECK(src.put(ChunkCoord{int32_t(i), 0},
                          makeChunk(ChunkCoord{int32_t(i), 0})));
        }
        CHECK_INT_EQ(src.residentCount(), 6u);
    }

    TEST_CASE(SetCapacity5EvictsOldestOnSixthPut) {
        // §18.5 case 2 — LRU wire (R-3G.4 / §18.4). Cap=5,
        // put 6 distinct coords → 5-resident with the oldest
        // evicted.
        InMemoryTilemapChunkSource src;
        src.setCapacity(5u);
        // 6 puts in order (0,0)..(5,0); oldest is (0,0).
        for (uint32_t i = 0; i < 6u; ++i) {
            CHECK(src.put(ChunkCoord{int32_t(i), 0},
                          makeChunk(ChunkCoord{int32_t(i), 0})));
        }
        CHECK_INT_EQ(src.residentCount(), 5u);
        // (0,0) evicted.
        CHECK_FALSE(src.isResident(ChunkCoord{0, 0}));
        // (1,0) still present (was second-in, not yet LRU).
        CHECK_TRUE (src.isResident(ChunkCoord{1, 0}));
        // (5,0) (newest) present.
        CHECK_TRUE (src.isResident(ChunkCoord{5, 0}));
    }

    TEST_CASE(SetMaxIoBytesPerSecZeroDisablesGate) {
        // §18.5 case 3 — R-3G.3a. Once the gate is disabled,
        // subsequent requests must not increment the
        // rejection counter even when far exceeding any
        // typical budget.
        InMemoryTilemapChunkSource src;
        src.setMaxIoBytesPerSec(64u * 1024u);  // 64 KB
        // Burn through the gate so the counter ticks once.
        src.requestChunk(ChunkCoord{0, 0});
        src.requestChunk(ChunkCoord{1, 0});
        // Third request must reject.
        const auto h0 = src.requestChunk(ChunkCoord{2, 0});
        CHECK_FALSE(h0.isValid());
        const auto sGate = src.counters().snapshot();
        CHECK_TRUE(sGate.chunk_io_reject >= 1u);
        // Now disable the gate and request a flood.
        src.setMaxIoBytesPerSec(0u);
        const uint64_t rejectBefore = sGate.chunk_io_reject;
        for (int32_t i = 0; i < 16; ++i) {
            const auto h = src.requestChunk(ChunkCoord{i, 1});
            // Disabling the gate means the gate code path
            // is skipped, so the request never reaches the
            // rejection branch.
            CHECK_TRUE(h.isValid());
        }
        const auto sAfter = src.counters().snapshot();
        CHECK_INT_EQ(sAfter.chunk_io_reject,
                     static_cast<uint64_t>(rejectBefore));
    }

    TEST_CASE(RateGateRejectsBeyondBudget) {
        // §18.5 case 4 — R-3G.3. Budget = 64 KB; nominal
        // Narrow16 = 32 KB. So the FIRST request fits (32 KB
        // consumed), the SECOND request's `consumed +
        // nominal = 64 KB` still equals the budget and is
        // therefore borderline — the explicit comparison
        // `_consumedInWindow + nominal > _maxIoBytesPerSec`
        // is strict greater-than, so the second request is
        // accepted (boundary). The THIRD request's check
        // `64 KB + 32 KB > 64 KB` = `true`, so it rejects.
        InMemoryTilemapChunkSource src;
        src.setMaxIoBytesPerSec(64u * 1024u);
        const ChunkRequestHandle h0 = src.requestChunk(ChunkCoord{0, 0});
        const ChunkRequestHandle h1 = src.requestChunk(ChunkCoord{1, 0});
        const ChunkRequestHandle h2 = src.requestChunk(ChunkCoord{2, 0});
        // h0 accepted, h1 accepted (boundary), h2 rejected.
        CHECK_TRUE (h0.isValid());
        CHECK_TRUE (h1.isValid());
        CHECK_FALSE(h2.isValid());
        const auto s = src.counters().snapshot();
        CHECK_INT_EQ(s.chunk_io_reject, 1u);
    }

    TEST_CASE(RateGateWindowRolloverAllowsNewRequests) {
        // §18.5 case 5 — R-3G.3 + R-3G.7. After the rate
        // gate rejects, the caller's window rolls. We use
        // the deterministic helper to drive the roll.
        InMemoryTilemapChunkSource src;
        src.setMaxIoBytesPerSec(64u * 1024u);  // 64 KB
        // Burn through.
        src.requestChunk(ChunkCoord{0, 0});   // accept (32 KB)
        src.requestChunk(ChunkCoord{1, 0});   // accept (32 KB; boundary)
        const ChunkRequestHandle hReject =
            src.requestChunk(ChunkCoord{2, 0}); // reject
        CHECK_FALSE(hReject.isValid());
        const auto sBefore = src.counters().snapshot();
        CHECK_INT_EQ(sBefore.chunk_io_reject, 1u);

        // Advance the window deterministically (R-3G.7 helper).
        // Read the wall clock for the "now" baseline so the
        // helper writes a `_windowStartUs` value that triggers
        // rollover on the next `requestChunk` call.
        const uint64_t nowUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        src.advanceWindowForTest(nowUs);

        // After the roll, the bucket is fresh; this request
        // passes.
        const ChunkRequestHandle hAfter =
            src.requestChunk(ChunkCoord{3, 0});
        CHECK_TRUE(hAfter.isValid());
        const auto sAfter = src.counters().snapshot();
        // counter still 1 (no additional rejection).
        CHECK_INT_EQ(sAfter.chunk_io_reject, 1u);
    }

    TEST_CASE(BudgetNonLRUPolicyReturnsFalseAndKeepsPrevious) {
        // §18.5 case 6 — R-3G.1 + R-3G.4. setBudget with a
        // non-LRU policy returns false; the previous budget
        // remains in effect.
        InMemoryTilemapChunkSource src;
        // Establish a working budget.
        TilemapBudget good;
        good.maxChunksLoaded  = 4u;
        good.maxIoBytesPerSec = 64u * 1024u;
        good.eviction         = EvictionPolicy::LRU;
        CHECK_TRUE(src.setBudget(good));

        // Attempt a non-LRU budget.
        TilemapBudget bad = good;
        bad.maxChunksLoaded  = 100u;  // would-be change
        bad.eviction         = EvictionPolicy::Distance;
        CHECK_FALSE(src.setBudget(bad));

        // The previous budget is still in effect.
        const TilemapBudget live = src.budget();
        CHECK_INT_EQ(static_cast<uint32_t>(live.maxChunksLoaded), 4u);
        CHECK_INT_EQ(static_cast<uint64_t>(live.maxIoBytesPerSec),
                     64u * 1024u);
        CHECK_TRUE(live.eviction == EvictionPolicy::LRU);
    }

    TEST_CASE(BudgetLRUAppliesBothFields) {
        // §18.5 case 7 — happy-path LRU. setBudget({.cap=10,
        // .maxIoBytesPerSec=32KB, LRU}) returns true; the
        // budget reads back the same shape.
        InMemoryTilemapChunkSource src;
        TilemapBudget b;
        b.maxChunksLoaded  = 10u;
        b.maxIoBytesPerSec = 32u * 1024u;
        b.maxChunksResident = 0u;     // R-3G.4 residency silent
        b.eviction         = EvictionPolicy::LRU;
        CHECK_TRUE(src.setBudget(b));
        const TilemapBudget live = src.budget();
        CHECK_INT_EQ(static_cast<uint32_t>(live.maxChunksLoaded), 10u);
        CHECK_INT_EQ(static_cast<uint64_t>(live.maxIoBytesPerSec),
                     32u * 1024u);
        CHECK_INT_EQ(static_cast<uint32_t>(live.maxChunksResident), 0u);
        CHECK_TRUE(live.eviction == EvictionPolicy::LRU);
    }

    TEST_CASE(ChunkIoRejectCounterResetByResetAllOnlyNotResetPerFrame) {
        // §18.5 case 8 — R-3G.2. The reject counter is
        // cumulative; resetAll zeros it, resetPerFrame does NOT.
        InMemoryTilemapChunkSource src;
        src.setMaxIoBytesPerSec(64u * 1024u);
        // Trigger two rejections.
        src.requestChunk(ChunkCoord{0, 0});
        src.requestChunk(ChunkCoord{1, 0});
        src.requestChunk(ChunkCoord{2, 0});  // reject
        src.requestChunk(ChunkCoord{3, 0});  // reject
        src.requestChunk(ChunkCoord{4, 0});  // reject
        const auto s0 = src.counters().snapshot();
        CHECK_TRUE(s0.chunk_io_reject >= 3u);

        // Reset per-frame must NOT zero the rejection counter.
        src.counters().resetPerFrame();
        const auto s1 = src.counters().snapshot();
        CHECK_TRUE(s1.chunk_io_reject >= 3u);

        // Reset all zeros it.
        src.counters().resetAll();
        const auto s2 = src.counters().snapshot();
        CHECK_INT_EQ(s2.chunk_io_reject, static_cast<uint64_t>(0u));
    }

TEST_SUITE_END
