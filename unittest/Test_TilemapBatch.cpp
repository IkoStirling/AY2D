// Test_TilemapBatch.cpp — Phase 3D batch tile-fill tests.
//
// design.md §15.5 (10-case matrix) + §15.4 (counter contract):
//   * One batch operation = one mutation event (tiles_mutated += 1)
//   * First-write lazy-fill bumps tiles_resident to full slot count
//     (one bump, not per-cell)
//   * Empty / fully-out-of-range / mode-mismatch / no-size inputs
//     are no-ops
//   * Partial clamp is allowed (write the in-range overlap only)
//
// All assertions use real counters().snapshot() deltas — same
// discipline as Phase 3C Test_CountersWired.cpp.

#include "AYTilemap.h"

#include "AYTest.h"
#include "AYTileCoord.h"
#include "AYTileRect.h"

using namespace ayt::ay2d;

TEST_SUITE(TilemapBatchSuite)

    TEST_CASE(SetTileRangeBasicRectangleOverwritesEveryCell) {
        // 4×4 grid; setTileRange over (1,1)..(3,3) -> cols {1,2},
        // rows {1,2}. cells inside read 7; outside cells read 0
        // (default). Section 15.5 case 1.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        const TileRect r{1, 1, 3, 3};
        CHECK(setTileRange(t, r, 7u));

        // Inside the rect: every cell reads 7.
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{1, 1})), 7u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{2, 1})), 7u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{1, 2})), 7u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{2, 2})), 7u);

        // Outside the rect: untouched cells still read 0
        // (defaultTileId default).
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{0, 0})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{3, 3})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{3, 0})), 0u);
    }

    TEST_CASE(SetTileRangeOutOfRangeIsNoOp) {
        // Rect entirely above-right of the grid; should clamp to
        // empty and return false, with no counter delta.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        const auto base = t.counters.snapshot();
        // {10, 10, 20, 20} — fully outside, even after clamping.
        CHECK_FALSE(setTileRange(t, TileRect{10, 10, 20, 20}, 5u));
        // post-clamp empty rects return false but we already failed.
        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated);
        CHECK_INT_EQ(s.tiles_resident, base.tiles_resident);
    }

    TEST_CASE(SetTileRangePartialClampWritesOverlapOnly) {
        // Rect {2, 2, 8, 8} on a 4×4 grid clamps to {2, 2, 4, 4}.
        // Section 15.5 case 3.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        CHECK(setTileRange(t, TileRect{2, 2, 8, 8}, 9u));

        // The 2×2 corner at (2,2)..(3,3) reads 9.
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{2, 2})), 9u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{3, 2})), 9u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{2, 3})), 9u);
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{3, 3})), 9u);

        // Outside the clamp: untouched, still 0.
        CHECK_INT_EQ(static_cast<uint32_t>(t.getTile(TileCoord{1, 1})), 0u);
    }

    TEST_CASE(SetTileRangeEmptyRectIsNoOp) {
        // Empty rect (x1 <= x0); no clamp, no write, no delta.
        // Section 15.5 case 4.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        const auto base = t.counters.snapshot();
        CHECK_FALSE(setTileRange(t, TileRect{2, 2, 2, 2}, 7u));
        CHECK_FALSE(setTileRange(t, TileRect{3, 3, 2, 5}, 7u));
        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated);
    }

    TEST_CASE(FillTileOverwritesEntireGridAsOneMutation) {
        // Fill 4×3 with 9 — exactly ONE mutation event regardless of
        // cell count. Section 15.5 case 5.
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);
        const auto base = t.counters.snapshot();
        fillTile(t, 9u);
        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated + 1u);
        for (uint32_t r = 0; r < t.rows; ++r) {
            for (uint32_t c = 0; c < t.cols; ++c) {
                CHECK_INT_EQ(
                    static_cast<uint32_t>(t.getTile(TileCoord{
                        static_cast<int32_t>(c),
                        static_cast<int32_t>(r)})),
                    9u);
            }
        }
    }

    TEST_CASE(CopyTileRangeSameModeRoundTrips) {
        // src filled with 7 via fillTile; copy 2×2 block into dst.
        // Section 15.5 case 6.
        Tilemap src;
        src.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        fillTile(src, 7u);
        Tilemap dst;
        dst.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        // Pretend we haven't touched dst yet — clear counters.
        dst.counters.resetAll();
        // Note: resetAll is a test-only introspection helper; the
        // mutation count goes back to 0 from this point so the
        // test asserts a *clean* +1, no leftover from resizeGrid.

        CHECK(copyTileRange(dst, TileRect{1, 1, 3, 3},
                            src, TileCoord{0, 0}));
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{1, 1})), 7u);
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{2, 2})), 7u);
        // cells outside dstRect stay at defaultTileId (0).
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{0, 0})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{3, 3})), 0u);
        // One mutation event for the successful copy.
        CHECK_INT_EQ(dst.counters.snapshot().tiles_mutated, 1u);
    }

    TEST_CASE(CopyTileRangeModeMismatchIsNoOp) {
        // src is Wide32, dst is Narrow16 — refuse without writing.
        // Section 15.5 case 7.
        Tilemap src;
        src.resizeGrid(4, 4, TileIdPackMode::Wide32);
        fillTile(src, 0xDEADu);
        Tilemap dst;
        dst.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        dst.counters.resetAll();
        CHECK_FALSE(copyTileRange(dst, TileRect{0, 0, 2, 2},
                                 src, TileCoord{0, 0}));
        // dst unchanged — still defaultTileId (0).
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{0, 0})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{1, 1})), 0u);
        // No delta.
        CHECK_INT_EQ(dst.counters.snapshot().tiles_mutated, 0u);
        CHECK_INT_EQ(dst.counters.snapshot().tiles_resident, 0u);
    }

    TEST_CASE(CopyTileRangePartialClampPartialCopy) {
        // copyTileRange dst rect {2, 2, 8, 8} on 4×4 dst clamps to
        // {2, 2, 4, 4}. The src span srcOrigin=(0,0)..(6,6) on a
        // 4×4 src clamps to (0,0)..(4,4). Effective intersection:
        // dst cols {2, 3}, rows {2, 3} receive src cols {0, 1},
        // rows {0, 1}. Section 15.5 case 8.
        Tilemap src;
        src.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        // Distinct ids so we can tell rows / cols apart after copy.
        // src.setTile((c, r), 10u + r * 100u + c) so (0,0)=10, (1,0)=11,
        // (0,1)=110, (1,1)=111.
        for (uint32_t r = 0; r < src.rows; ++r) {
            for (uint32_t c = 0; c < src.cols; ++c) {
                src.setTile(TileCoord{
                    static_cast<int32_t>(c),
                    static_cast<int32_t>(r)},
                    10u + r * 100u + c);
            }
        }
        Tilemap dst;
        dst.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        dst.counters.resetAll();

        // dst rect {2,2,8,8} clamps to {2,2,4,4} (w=2, h=2). srcOrigin
        // (0,0) yields src span (0,0)..(2,2) clamped to (0,0)..(4,4),
        // intersection (0,0)..(2,2). copyTileRange dst(2..4, 2..4)
        // ← src(0..2, 0..2).
        CHECK(copyTileRange(dst, TileRect{2, 2, 8, 8},
                            src, TileCoord{0, 0}));

        // dst(2,2) ← src(0,0) = 10
        // dst(3,2) ← src(1,0) = 11
        // dst(2,3) ← src(0,1) = 110
        // dst(3,3) ← src(1,1) = 111
        CHECK_INT_EQ(static_cast<uint32_t>(
            dst.getTile(TileCoord{2, 2})), 10u + 0u * 100u + 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(
            dst.getTile(TileCoord{3, 2})), 10u + 0u * 100u + 1u);
        CHECK_INT_EQ(static_cast<uint32_t>(
            dst.getTile(TileCoord{2, 3})), 10u + 1u * 100u + 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(
            dst.getTile(TileCoord{3, 3})), 10u + 1u * 100u + 1u);

        // Outside the effective copy on dst: untouched, defaultTileId (0).
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{0, 0})), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(dst.getTile(TileCoord{1, 1})), 0u);

        // One mutation event.
        CHECK_INT_EQ(dst.counters.snapshot().tiles_mutated, 1u);
    }

    TEST_CASE(FirstWriteLazyFillFromBatchBumpsResidentOnce) {
        // 4×4 grid (16 slots); first batch op that grows the
        // storage bumps tiles_resident to 16 — ONE bump, NOT 16.
        // Section 15.5 case 9 + R-3D.4.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        const auto base = t.counters.snapshot();
        // resizeGrid counted once (tiles_mutated = 1) and resident
        // is 0 from the clear.
        CHECK_INT_EQ(base.tiles_resident, 0u);
        CHECK_INT_EQ(base.tiles_mutated, 1u);

        fillTile(t, 11u);
        const auto s = t.counters.snapshot();
        CHECK_INT_EQ(s.tiles_resident, 16u);
        CHECK_INT_EQ(s.tiles_mutated, base.tiles_mutated + 1u);

        // A second batch leaves resident untouched but adds another
        // mutation — locks "second batch DOES NOT re-fire resident".
        (void)setTileRange(t, TileRect{0, 0, 2, 2}, 22u);
        const auto s2 = t.counters.snapshot();
        CHECK_INT_EQ(s2.tiles_resident, 16u);
        CHECK_INT_EQ(s2.tiles_mutated, s.tiles_mutated + 1u);
    }

    TEST_CASE(GridRectAccessorSpansWholeGrid) {
        // section 15.5 case 10.
        Tilemap t;
        t.resizeGrid(4, 3, TileIdPackMode::Narrow16);
        const TileRect r = gridRect(t);
        CHECK_INT_EQ(r.x0, 0);
        CHECK_INT_EQ(r.y0, 0);
        CHECK_INT_EQ(r.x1, 4);
        CHECK_INT_EQ(r.y1, 3);
        CHECK_FALSE(isEmpty(r));
        CHECK_INT_EQ(area(r), 12);
        CHECK_INT_EQ(static_cast<int64_t>(isEmpty(TileRect{})),
                     static_cast<int64_t>(true));
    }

TEST_SUITE_END
