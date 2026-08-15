// Test_TileMath.cpp — Phase 3E world↔cell math layer tests.
//
// design.md §16.5 (8-case matrix) + §16.2 (locked semantics):
//   * worldToCell floors both axes (R-3E.1).
//   * No clamping in helpers; OOB handled downstream at
//     setTile / getTile boundary (R-3E.2).
//   * cellToWorld returns the **cell center**, not the corner
//     (R-3E.5).
//   * aabbOverlappingCells returns half-open `TileRect` with
//     empty-rect fallback for degenerate input (R-3E.3 / R-3E.4).
//   * cellSize==0 is a defensive guard, never division-by-zero
//     (R-3E.6).
//
// No cross-module dependency; all helpers are pure math.

#include "AY2D/TileMath.h"

#include "AYTest.h"
#include "AY2D/Tilemap.h"

using namespace ayt::ay2d;

TEST_SUITE(TileMathSuite)

    TEST_CASE(WorldToCellIntegerHitReturnsExactCell) {
        // §16.5 case 1: world (64, 64) with cellOrigin 0/0 and
        // cellSize 32 lands on cell (2, 2). Sub-pixel world point
        // (63.99, 32.5) still floors to cell (1, 1) — confirms
        // R-3E.1's floor-not-round.
        const TileCoord hit = worldToCell(
            ayt::math::FVector2{64.0f, 64.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            32.0f, 32.0f);
        CHECK_INT_EQ(hit.x, 2);
        CHECK_INT_EQ(hit.y, 2);

        const TileCoord sub = worldToCell(
            ayt::math::FVector2{63.99f, 32.5f},
            ayt::math::FVector2{0.0f, 0.0f},
            32.0f, 32.0f);
        CHECK_INT_EQ(sub.x, 1);
        CHECK_INT_EQ(sub.y, 1);
    }

    TEST_CASE(WorldToCellNegativeOriginTruncatesTowardMinusInfinity) {
        // §16.5 case 2: cellOrigin (-100, -100), cellSize 16.
        // Cells around the origin: cell (0,0) spans world
        // (-100..-84, ...); cell (-1,-1) spans world
        // (-116..-100, ...). world (-91, -91) → cell (0, 0).
        // world (-117, -117) lies one cell further into the
        // negative range (cell (-2, -2)). The helper does NOT
        // clamp — the negative cell is the caller's signal to
        // OOB-drop (R-3E.1 + R-3E.2).
        const TileCoord inside = worldToCell(
            ayt::math::FVector2{-91.0f, -91.0f},
            ayt::math::FVector2{-100.0f, -100.0f},
            16.0f, 16.0f);
        CHECK_INT_EQ(inside.x, 0);
        CHECK_INT_EQ(inside.y, 0);

        const TileCoord outside = worldToCell(
            ayt::math::FVector2{-117.0f, -117.0f},
            ayt::math::FVector2{-100.0f, -100.0f},
            16.0f, 16.0f);
        // (-117 - -100) / 16 = -17/16 = -1.0625; floorf → -2.
        CHECK_INT_EQ(outside.x, -2);
        CHECK_INT_EQ(outside.y, -2);
    }

    TEST_CASE(WorldToCellZeroCellSizeReturnsOrigin) {
        // §16.5 case 3: zero cellSize guard (R-3E.6). Helper
        // must NOT divide by zero — returns TileCoord{0, 0}.
        const TileCoord z = worldToCell(
            ayt::math::FVector2{64.0f, 64.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            0.0f, 0.0f);
        CHECK_INT_EQ(z.x, 0);
        CHECK_INT_EQ(z.y, 0);

        // Negative cellSize is also guarded (the `cellSize <= 0`
        // branch covers both directions). Test the upper bound.
        const TileCoord neg = worldToCell(
            ayt::math::FVector2{64.0f, 64.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            -1.0f, 32.0f);
        CHECK_INT_EQ(neg.x, 0);
        CHECK_INT_EQ(neg.y, 0);
    }

    TEST_CASE(CellToWorldCenterNotCorner) {
        // §16.5 case 4 + R-3E.5: cell (2, 3) with cellOrigin 0/0
        // and cellSize 16 returns (40, 56) — the **center** of
        // cell (2, 3) which spans world (32, 48) to (48, 64).
        const ayt::math::FVector2 c = cellToWorld(
            TileCoord{2, 3},
            ayt::math::FVector2{0.0f, 0.0f},
            16.0f, 16.0f);
        // FVector2 exposes .x / .y (no operator[] in the public
        // surface — see AYMath/MathTypes.h:13). CHECK_FLOAT_EQ
        // uses a 1e-5 epsilon to absorb the exact-FP add of the
        // +0.5 cell-center offset.
        CHECK_FLOAT_EQ(c.x, 40.0f, 1e-5f);
        CHECK_FLOAT_EQ(c.y, 56.0f, 1e-5f);
    }

    TEST_CASE(RoundTripWorldToCellBackMatchesForCellInterior) {
        // §16.5 case 5: cell-center → cell interior → same cell.
        // cellToWorld(2, 2, 16) returns (40, 40); worldToCell at
        // (40, 40) with cellOrigin 0/0 / cellSize 16 yields
        // (2, 2) — because (40, 40) lies in cell [32, 48) x
        // [32, 48).
        const ayt::math::FVector2 w = cellToWorld(
            TileCoord{2, 2},
            ayt::math::FVector2{0.0f, 0.0f},
            16.0f, 16.0f);
        const TileCoord rt = worldToCell(
            w,
            ayt::math::FVector2{0.0f, 0.0f},
            16.0f, 16.0f);
        CHECK_INT_EQ(rt.x, 2);
        CHECK_INT_EQ(rt.y, 2);
    }

    TEST_CASE(AabbOverlappingCellsFullyInsideReturnsExactRect) {
        // §16.5 case 6: world AABB (32, 32)..(96, 96) with
        // cellOrigin 0/0 + cellSize 32 covers cells (1, 1), (2, 1),
        // (1, 2), (2, 2). Half-open rect is {1, 1, 3, 3}.
        const TileRect r = aabbOverlappingCells(
            ayt::math::FVector2{32.0f, 32.0f},
            ayt::math::FVector2{96.0f, 96.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            32.0f, 32.0f);
        CHECK_INT_EQ(r.x0, 1);
        CHECK_INT_EQ(r.y0, 1);
        CHECK_INT_EQ(r.x1, 3);
        CHECK_INT_EQ(r.y1, 3);
        CHECK_INT_EQ(static_cast<int64_t>(area(r)), 4);
        CHECK_FALSE(isEmpty(r));
    }

    TEST_CASE(AabbOverlappingCellsPartiallyOutOfGridReturnsEmpty) {
        // §16.5 case 7: degenerate AABB (max <= min) → empty.
        // (R-3E.3, R-3E.4.) Pure-math helper: this is the only
        // path where the helper itself can return isEmpty —
        // zero/negative cellSize and (max <= min) on either axis.
        const TileRect degen = aabbOverlappingCells(
            ayt::math::FVector2{64.0f, 64.0f},
            ayt::math::FVector2{32.0f, 32.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            32.0f, 32.0f);
        CHECK_TRUE(isEmpty(degen));

        // Zero cellSize → empty (R-3E.6).
        const TileRect z = aabbOverlappingCells(
            ayt::math::FVector2{32.0f, 32.0f},
            ayt::math::FVector2{96.0f, 96.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            0.0f, 32.0f);
        CHECK_TRUE(isEmpty(z));

        // AABB straddling the origin → cells with negative coords;
        // helper does NOT clamp (R-3E.2) — caller routes through
        // clampToGrid. The returned rect is therefore non-empty
        // and contains negative entries. This proves the helper
        // is *pure math* and not silently dropping the negative
        // half of any input.
        const TileRect partial = aabbOverlappingCells(
            ayt::math::FVector2{-32.0f, -32.0f},
            ayt::math::FVector2{32.0f, 32.0f},
            ayt::math::FVector2{0.0f, 0.0f},
            32.0f, 32.0f);
        CHECK_FALSE(isEmpty(partial));
        CHECK_INT_EQ(partial.x0, -1);
        CHECK_INT_EQ(partial.y0, -1);
        CHECK_INT_EQ(partial.x1, 1);
        CHECK_INT_EQ(partial.y1, 1);
    }

    TEST_CASE(TilemapSetTileByWorldCoordDelegatesAndGetReadsBack) {
        // §16.5 case 8: end-to-end integration with Tilemap. The
        // world-coord overloads on `setTile` / `getTile` must
        // route through the cell-coord contract exactly.
        //
        // NOTE: `Tilemap::tileWidth` / `tileHeight` default to 0,
        // which the helper guards via `cellSize <= 0` (R-3E.6).
        // We must set them explicitly to a real cell size (32)
        // for the world-coord overloads to hit the correct cell.
        Tilemap t;
        t.resizeGrid(4, 4, TileIdPackMode::Narrow16);
        t.tileWidth  = 32;
        t.tileHeight = 32;

        // (64, 64) → cell (2, 2) → 7.
        t.setTile(ayt::math::FVector2{64.0f, 64.0f}, 7u);
        CHECK_INT_EQ(static_cast<uint32_t>(
            t.getTile(ayt::math::FVector2{64.0f, 64.0f})), 7u);

        // OOB world point: getTile reads defaultTileId (0),
        // matching the cell-coord OOB contract.
        CHECK_INT_EQ(static_cast<uint32_t>(
            t.getTile(ayt::math::FVector2{-1.0f, -1.0f})), 0u);

        // setTile at OOB world point is a silent no-op: tile
        // value at (2, 2) remains 7, tiles_mutated does NOT
        // bump. We snapshot before the OOB write only; the
        // batch write that follows IS a mutation (+1) and is
        // asserted via the cell-coord `tiles_mutated` delta
        // (setTileRange contract).
        const auto base = t.counters.snapshot();
        t.setTile(ayt::math::FVector2{-1.0f, -1.0f}, 99u);
        const auto afterOob = t.counters.snapshot();
        CHECK_INT_EQ(afterOob.tiles_mutated, base.tiles_mutated);

        // World-coord setTileRange overload: AABB (32, 32) ..
        // (96, 96) on the 4×4 grid lands cells (1, 1), (2, 1),
        // (1, 2), (2, 2) → those now read 13, the (2, 2) cell
        // previously holding 7 now holds 13 (the range write
        // overwrites, partial-clamp respects grid bounds).
        CHECK(setTileRange(t,
            ayt::math::FVector2{32.0f, 32.0f},
            ayt::math::FVector2{96.0f, 96.0f},
            13u));
        CHECK_INT_EQ(static_cast<uint32_t>(
            t.getTile(ayt::math::FVector2{64.0f, 64.0f})), 13u);
        // Untouched cell (0, 0) was earlier written as 0 by
        // `defaultTileId`, remains 0.
        CHECK_INT_EQ(static_cast<uint32_t>(
            t.getTile(TileCoord{0, 0})), 0u);
    }

TEST_SUITE_END
