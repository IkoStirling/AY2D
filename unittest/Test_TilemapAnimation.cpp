// Test_TilemapAnimation.cpp — Phase 3B Tilemap animation table + tick.
//
// design.md §7.2: per-tile animation table lives on Tilemap.
// Tick is integer-ms (no float drift). Frame change is batched
// (one walk per call).
//
// Coverage (10 cases):
//   * empty default / single tick advances / loops / remainder
//     accumulates / zero-duration no-op / same-time no-op /
//     reversed clock clamp / first-tick baseline / multiple
//     tiles independent / getAnimatedTileId returns live frame.

#include "AYTest.h"
#include "AYTileAnimation.h"
#include "AYTileCoord.h"
#include "AYTilemap.h"

using namespace ayt::ay2d;

namespace {

// Helper: resize animationTable to index `n`, set `frames` at index `id`.
void setAnimationEntry(Tilemap& t, uint32_t id, std::vector<TileFrame> frames) {
    if (t.animationTable.size() <= id) t.animationTable.resize(id + 1);
    t.animationTable[id] = std::move(frames);
}

} // namespace

TEST_SUITE(TilemapAnimationSuite)

    TEST_CASE(DefaultTilemapHasNoAnimation) {
        Tilemap t;
        // animationTable empty -> resolveAnimatedTileId returns input
        CHECK_INT_EQ(resolveAnimatedTileId(t, 5u), 5u);
        CHECK_INT_EQ(resolveAnimatedTileId(t, 0u), 0u);
        // First tick sets baseline and does nothing else
        tickTilemapAnimation(t, 1'000'000);  // 1 second
        CHECK(t.hasBeenTicked);
        CHECK_INT_EQ(static_cast<int>(t.lastTickUs), 1'000'000);
        // No frames registered, so animationState stays empty
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx.size()), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs.size()), 0);
    }

    TEST_CASE(AddAnimationThenTickAdvances) {
        Tilemap t;
        // Source tileId 7 -> [frameTileId=10 for 10ms, frameTileId=20 for 20ms].
        // Cycle = 10 + 20 = 30ms.
        setAnimationEntry(t, 7, {
            TileFrame{10, 10},
            TileFrame{20, 20},
        });
        // Initial baseline
        tickTilemapAnimation(t, 0);
        // 10ms later: frame 0 (duration 10) expires -> advance to frame 1,
        // elapsed = 0. The cycle is 30ms total so 10ms is *not* a full cycle.
        tickTilemapAnimation(t, 10'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[7]), 1);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[7]), 0);
    }

    TEST_CASE(AnimationLoopsAfterFullSequence) {
        Tilemap t;
        // 2-frame loop with durations 10ms + 20ms = 30ms total cycle.
        setAnimationEntry(t, 3, {
            TileFrame{100, 10},
            TileFrame{200, 20},
        });
        tickTilemapAnimation(t, 0);
        // One full cycle = 30ms
        tickTilemapAnimation(t, 30'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[3]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[3]), 0);

        // Two full cycles = 60ms
        tickTilemapAnimation(t, 60'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[3]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[3]), 0);
    }

    TEST_CASE(AnimationRemainderAccumulatesAcrossSmallDeltas) {
        Tilemap t;
        // Single 100ms frame
        setAnimationEntry(t, 2, {TileFrame{42, 100}});
        tickTilemapAnimation(t, 0);

        // First tick: 30ms elapsed; frame stays at 0, elapsed = 30.
        tickTilemapAnimation(t, 30'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[2]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[2]), 30);

        // Second tick: another 30ms; total elapsed 60ms; still on frame 0.
        tickTilemapAnimation(t, 60'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[2]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[2]), 60);

        // Third tick: another 30ms; total elapsed 90ms; still on frame 0.
        tickTilemapAnimation(t, 90'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[2]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[2]), 90);

        // Fourth tick: another 30ms; total elapsed 120ms; frame
        // advances (100ms), elapsed carries 20ms.
        tickTilemapAnimation(t, 120'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[2]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[2]), 20);
    }

    TEST_CASE(AnimationZeroDurationTreatedAsNoOp) {
        Tilemap t;
        // A sequence of one zero-duration frame: durationMs == 0 ->
        // the loop break fires immediately and the frame never advances.
        // elapsedMs accumulates indefinitely because the loop never
        // subtracts (this is fine — the frame never advances either).
        setAnimationEntry(t, 9, {TileFrame{99, 0}});
        tickTilemapAnimation(t, 0);
        // 1 second = 1'000'000 us = 1'000 ms. The delta accumulates
        // fully because no frame duration ever fires the subtract branch.
        tickTilemapAnimation(t, 1'000'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[9]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[9]), 1'000);
    }

    TEST_CASE(TickAtSameTimeNoOp) {
        Tilemap t;
        setAnimationEntry(t, 1, {
            TileFrame{10, 50},
            TileFrame{20, 50},
        });
        tickTilemapAnimation(t, 1'000'000);
        // Same time twice -> deltaMs == 0 -> no advance
        tickTilemapAnimation(t, 1'000'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[1]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[1]), 0);
    }

    TEST_CASE(ReversedClockClampsToZero) {
        Tilemap t;
        setAnimationEntry(t, 4, {
            TileFrame{40, 30},
            TileFrame{80, 30},
        });
        tickTilemapAnimation(t, 1'000'000);  // baseline at 1s

        // now < lastTickUs -> clamp delta to 0; no advance.
        tickTilemapAnimation(t, 500'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[4]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[4]), 0);

        // lastTickUs was clamped to 500'000 (the new `now`).
        // A subsequent forward jump by 30ms -> frame advances once.
        tickTilemapAnimation(t, 530'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[4]), 1);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[4]), 0);
    }

    TEST_CASE(GetAnimatedTileIdReturnsCurrentFrame) {
        Tilemap t;
        // Source tileId 6 -> [frame 0 = id 100, frame 1 = id 200]
        setAnimationEntry(t, 6, {
            TileFrame{100, 50},
            TileFrame{200, 50},
        });
        tickTilemapAnimation(t, 0);

        // Initially frame 0 -> resolves to 100
        CHECK_INT_EQ(resolveAnimatedTileId(t, 6u), 100u);

        // Advance 60ms -> frame 1 with remainder 10ms
        tickTilemapAnimation(t, 60'000);
        CHECK_INT_EQ(resolveAnimatedTileId(t, 6u), 200u);

        // Advance another 50ms -> loop to frame 0
        tickTilemapAnimation(t, 110'000);
        CHECK_INT_EQ(resolveAnimatedTileId(t, 6u), 100u);

        // Out-of-range source tileId -> unchanged
        CHECK_INT_EQ(resolveAnimatedTileId(t, 999u), 999u);
    }

    TEST_CASE(MultipleTilesIndependentAnimation) {
        Tilemap t;
        setAnimationEntry(t, 5, {TileFrame{50, 100}});              // 100ms frame
        setAnimationEntry(t, 6, {TileFrame{60, 50}, TileFrame{61, 50}});  // 50ms + 50ms
        tickTilemapAnimation(t, 0);

        // 60ms tick: tile 5 elapsed = 60, tile 6 advanced once -> frame 1 elapsed 10.
        tickTilemapAnimation(t, 60'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[5]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[5]), 60);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[6]), 1);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[6]), 10);
    }

    TEST_CASE(FirstTickIsBaseline) {
        Tilemap t;
        setAnimationEntry(t, 2, {TileFrame{99, 50}});

        // First tick at far-future `nowUs`: only sets the baseline,
        // does NOT advance frames (no initial jump).
        tickTilemapAnimation(t, 999'999'999);
        CHECK(t.hasBeenTicked);
        CHECK_INT_EQ(static_cast<int>(t.lastTickUs), 999'999'999);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[2]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[2]), 0);

        // Second tick 50ms later: frame advances once (50ms), elapsed = 0.
        tickTilemapAnimation(t, 999'999'999 + 50'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[2]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[2]), 0);
    }

    // ----- P3J.4 / A-1 batch-state coverage (design.md §13.28) -----
    //
    // Five cases lock the batch-tick invariant: 100 entries all
    // advance in lock-step, no cross-entry aliasing on state vectors,
    // ensureStateSize lazy-grows exactly once on the first real tick,
    // table extent stays unchanged across ticks, and a huge delta
    // caps cleanly at the loop boundary (no elapsed accumulation
    // overflow / no frame index runaway).

    TEST_CASE(BatchTick_AnimatesAllEntriesExactlyOnce) {
        Tilemap t;
        // Register 100 entries (tileIds 0..99), each with 3 frames
        // of 100 ms. Tick at 100 ms advances every entry's frame
        // by exactly one slot (frameIdx 0 -> 1, elapsedMs reset to 0).
        for (uint32_t i = 0; i < 100u; ++i) {
            setAnimationEntry(t, i, {TileFrame{i * 10u,        100u},
                                     TileFrame{i * 10u + 1u,  100u},
                                     TileFrame{i * 10u + 2u,  100u}});
        }
        tickTilemapAnimation(t, 0);                 // baseline
        tickTilemapAnimation(t, 100'000);           // 100 ms delta
        // Every entry advanced exactly once.
        for (uint32_t i = 0; i < 100u; ++i) {
            CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[i]), 1);
            CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[i]), 0);
        }
    }

    TEST_CASE(BatchTick_NoCrossEntryAliasing) {
        Tilemap t;
        setAnimationEntry(t, 0, {TileFrame{10, 10}, TileFrame{11, 10}});
        setAnimationEntry(t, 1, {TileFrame{20, 30}, TileFrame{21, 30}});
        tickTilemapAnimation(t, 0);
        tickTilemapAnimation(t, 20'000);            // 20 ms delta
        // Tile 0: 20 ms = A (10) + B (10) = full cycle -> back to A.
        // Tile 1: 20 ms < 30 ms -> still at C.
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[0]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[0]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[1]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[1]), 20);
    }

    TEST_CASE(BatchTick_EnsureStateSizeLazyGrow) {
        Tilemap t;
        // Fresh: state vectors are empty.
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx.size()), 0);
        for (uint32_t i = 0; i < 100u; ++i) {
            setAnimationEntry(t, i, {TileFrame{i, 10u}});
        }
        // Baseline tick: ensureStateSize runs even on the no-advance
        // path, so state size matches the table extent.
        tickTilemapAnimation(t, 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx.size()), 100);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs.size()), 100);
        // Real tick: state size stays at 100 (no per-entry resize).
        tickTilemapAnimation(t, 10'000);
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx.size()), 100);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs.size()), 100);
    }

    TEST_CASE(BatchTick_TableExtentUnchangedAfterTick) {
        Tilemap t;
        for (uint32_t i = 0; i < 100u; ++i) {
            setAnimationEntry(t, i, {TileFrame{i, 50u}, TileFrame{i + 100u, 50u}});
        }
        tickTilemapAnimation(t, 0);
        for (int k = 0; k < 5; ++k) {
            tickTilemapAnimation(t, (k + 1) * 50'000);
        }
        CHECK_INT_EQ(static_cast<int>(t.animationTable.size()), 100);
    }

    TEST_CASE(BatchTick_LargeDeltaCapsAtLoopBoundary) {
        Tilemap t;
        // 4 frames of 10 ms each = 40 ms loop.
        setAnimationEntry(t, 0, {TileFrame{100, 10},
                                 TileFrame{101, 10},
                                 TileFrame{102, 10},
                                 TileFrame{103, 10}});
        tickTilemapAnimation(t, 0);
        // 10 s delta = 10 000 ms / 40 ms = 250 full loops.
        tickTilemapAnimation(t, 10'000'000);
        // After 250 full loops, frameIdx wraps back to 0; elapsedMs
        // is exactly 0 (all 10 000 ms is consumed by frame durations).
        CHECK_INT_EQ(static_cast<int>(t.animationState.currentFrameIdx[0]), 0);
        CHECK_INT_EQ(static_cast<int>(t.animationState.elapsedMs[0]), 0);
    }

TEST_SUITE_END