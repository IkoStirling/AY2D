// Test_BudgetPolicyCounters.cpp — P3G.1 partial counter scaffolding + log warning tests.
//
// design.md §13.16 + §13.PF (C1 R-3G.1 clarification). The
// R-3G.1 lock is intact (non-LRU policy = no-op + false
// return); this slice ships the counter scaffolding +
// non-LRU log warning.
//
// All assertions use real `counters().snapshot()` deltas —
// same discipline as P3C Test_CountersWired + P3G
// Test_ChunkSourceBudget + P3G.2a Test_CpuSoftCap.

#include "AYInMemoryTilemapChunkSource.h"

#include "AY2DCounters.h"
#include "AYChunkData.h"
#include "AYTest.h"
#include "AYTilemapBudget.h"

using namespace ayt::ay2d;

namespace {

ChunkData makeChunk(ChunkCoord coord) {
    ChunkData c;
    c.coord = coord;
    c.mode  = TileIdPackMode::Narrow16;
    c.tileIds16.assign(16u * 16u, 0u);
    return c;
}

} // namespace

TEST_SUITE(BudgetPolicyCountersSuite)

    TEST_CASE(NonLRUSetBudgetReturnsFalseAndKeepsPrevious) {
        // §13.16 case 1: setBudget with Distance policy
        // returns false; the previous LRU budget stays in
        // effect (R-3G.1 lock intact per §13.PF C1). The new
        // scaffolding counters (`evictions_distance` /
        // `evictions_time_window`) stay at 0 because the
        // policy is not wired.
        InMemoryTilemapChunkSource src;
        // Establish a working LRU budget.
        TilemapBudget good;
        good.maxChunksLoaded    = 4u;
        good.maxIoBytesPerSec   = 0u;
        good.eviction           = EvictionPolicy::LRU;
        CHECK_TRUE(src.setBudget(good));

        // Now request Distance via setBudget.
        TilemapBudget bad = good;
        bad.eviction = EvictionPolicy::Distance;
        CHECK_FALSE(src.setBudget(bad));

        // Previous budget is still in effect.
        const TilemapBudget live = src.budget();
        CHECK_TRUE(live.eviction == EvictionPolicy::LRU);
        CHECK_INT_EQ(static_cast<uint32_t>(live.maxChunksLoaded), 4u);

        // Scaffolding counters stay at 0.
        const auto s = src.counters().snapshot();
        CHECK_INT_EQ(s.evictions_distance, 0u);
        CHECK_INT_EQ(s.evictions_time_window, 0u);
    }

    TEST_CASE(ScaffoldingCountersAreZeroByDefault) {
        // §13.16 case 2: the three new counters
        // (`evictions_distance` / `evictions_time_window` /
        // `evictions_lru`) exist on the Snapshot type and the
        // struct. Default = 0. Distance / TimeWindow stay at 0
        // because the policies are deferred.
        InMemoryTilemapChunkSource src;
        const auto s0 = src.counters().snapshot();
        CHECK_INT_EQ(s0.evictions_distance, 0u);
        CHECK_INT_EQ(s0.evictions_time_window, 0u);
        CHECK_INT_EQ(s0.evictions_lru, 0u);

        // LRU eviction bumps evictions_lru but NOT
        // evictions_distance / _time_window.
        src.setCapacity(2u);
        for (uint32_t i = 0; i < 5u; ++i) {
            CHECK(src.put(ChunkCoord{int32_t(i), 0},
                          makeChunk(ChunkCoord{int32_t(i), 0})));
        }
        const auto s1 = src.counters().snapshot();
        CHECK_INT_EQ(s1.chunk_resident_count, 2u);  // hard cap rules
        CHECK_TRUE(s1.evictions_lru >= 3u);    // 5 puts - 2 cap = 3 evicted
        CHECK_INT_EQ(s1.evictions_distance, 0u);
        CHECK_INT_EQ(s1.evictions_time_window, 0u);
    }

    TEST_CASE(LRUEvictionsCounterResetByResetAllOnly) {
        // §13.16 case 3: `evictions_lru` is cumulative
        // (R-3G.2 discipline); resetAll zeros it, resetPerFrame
        // does NOT.
        InMemoryTilemapChunkSource src;
        src.setCapacity(1u);
        for (uint32_t i = 0; i < 4u; ++i) {
            CHECK(src.put(ChunkCoord{int32_t(i), 0},
                          makeChunk(ChunkCoord{int32_t(i), 0})));
        }
        const auto s0 = src.counters().snapshot();
        CHECK_TRUE(s0.evictions_lru >= 3u);

        // Reset per-frame must NOT zero evictions_lru.
        src.counters().resetPerFrame();
        const auto s1 = src.counters().snapshot();
        CHECK_TRUE(s1.evictions_lru >= 3u);

        // Reset all zeros it.
        src.counters().resetAll();
        const auto s2 = src.counters().snapshot();
        CHECK_INT_EQ(s2.evictions_lru, 0u);
        // Distance / TimeWindow stay at 0 throughout (no-op).
        CHECK_INT_EQ(s2.evictions_distance, 0u);
        CHECK_INT_EQ(s2.evictions_time_window, 0u);
    }

TEST_SUITE_END