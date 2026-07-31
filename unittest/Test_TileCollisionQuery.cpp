// Test_TileCollisionQuery.cpp — Phase 5 collision types + adapter tests.
//
// design.md §8.1 + §13.13 (Phase 5 changelog) + §13.PF
// pre-flight retractions:
//   * C6 / `flagsAtRaw` Empty regression: body now returns
//     `CollisionFlags::Empty` (1<<6) not `None` (0).
//   * C6 / default `isBlocked` formula: `flagsAt(c) != Empty`.
//   * C8 / `TileCoord` round-trip: the shipped interface uses
//     `TileCoord` (consistent with all AY2D code).
//
// All assertions are pure CPU, no bgfx, no cross-module deps.

#include "AYTileCollision.h"
#include "AYTilemap.h"
#include "AYTilemapCollisionAdapter.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(TileCollisionQuerySuite)

    TEST_CASE(Ray2DPointAtParametrics) {
        // §8.1 doc: `pointAt(t)` returns `origin + t * direction`.
        Ray2D r;
        r.origin    = {1.0f, 2.0f};
        r.direction = {3.0f, 4.0f};
        CHECK_TRUE(r.pointAt(0.0f).x == 1.0f && r.pointAt(0.0f).y == 2.0f);
        CHECK_TRUE(r.pointAt(1.0f).x == 4.0f && r.pointAt(1.0f).y == 6.0f);
        CHECK_TRUE(r.pointAt(0.5f).x == 2.5f && r.pointAt(0.5f).y == 4.0f);
        // t < 0 still works (no clamp; doc says caller is
        // responsible for `t >= tMin`).
        CHECK_TRUE(r.pointAt(-1.0f).x == -2.0f && r.pointAt(-1.0f).y == -2.0f);
    }

    TEST_CASE(RaycastHit2DDefaultIsNoHit) {
        // §8.1: default `hit=false`, `t=0`, no cell, no flags,
        // no point. The "no hit" sentinel.
        RaycastHit2D hit;
        CHECK_FALSE(hit.hit);
        CHECK_INT_EQ(static_cast<int>(hit.t), 0);
        CHECK_TRUE(hit.cell.x == 0 && hit.cell.y == 0);
        CHECK_TRUE(hit.flags == CollisionFlags::None);
        CHECK_TRUE(hit.point.x == 0.0f && hit.point.y == 0.0f);
    }

    TEST_CASE(AdapterFlagsAtEmptyAndIsBlockedFalse) {
        // §8.1 + §13.PF (C6): `flagsAtRaw` returns Empty; the
        // adapter's `flagsAt` mirrors that; the inherited
        // default `isBlocked` returns `false` because
        // `Empty != Empty` is false.
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.mode = TileIdPackMode::Narrow16;
        TilemapCollisionQueryAdapter adapter(m);
        CHECK_TRUE(adapter.flagsAt(TileCoord{0, 0}) == CollisionFlags::Empty);
        CHECK_FALSE(adapter.isBlocked(TileCoord{0, 0}));
        CHECK_FALSE(adapter.isBlocked(TileCoord{3, 3}));
    }

    TEST_CASE(FlagsAtRawReturnsEmptyNotNone) {
        // §13.PF regression: `Tilemap::flagsAtRaw` body MUST
        // return `CollisionFlags::Empty` (1<<6 = 64), not
        // `CollisionFlags::None` (0). The pre-Phase-5 body
        // returned 0, which §8.1 explicitly forbids ("None MUST
        // NOT be used to mean 'empty'").
        Tilemap m;
        m.cols = 2;
        m.rows = 2;
        CHECK_INT_EQ(m.flagsAtRaw(TileCoord{0, 0}), 1u << 6);
        CHECK_INT_EQ(m.flagsAtRaw(TileCoord{99, 99}), 1u << 6);  // OOB cell also Empty
        // And the value is NOT zero (None sentinel is reserved
        // for "unset / unknown"; never for "no flag data").
        CHECK_TRUE(m.flagsAtRaw(TileCoord{0, 0}) != 0u);
    }

    TEST_CASE(TileCoordRoundTrip) {
        // §13.PF (C8): shipped interface uses `TileCoord`
        // (consistent with all AY2D code) not `IVector2`. Verify
        // the round-trip preserves the int32 fields.
        Tilemap m;
        m.cols = 100;
        m.rows = 100;
        TilemapCollisionQueryAdapter adapter(m);
        const TileCoord c{42, -7};
        // `flagsAt` accepts the TileCoord and returns Empty.
        CHECK_TRUE(adapter.flagsAt(c) == CollisionFlags::Empty);
        // `isBlocked` is `flagsAt(c) != Empty` — so for any
        // TileCoord the answer is false today.
        CHECK_FALSE(adapter.isBlocked(c));
    }

    TEST_CASE(AdapterOutOfRangeCellReturnsEmpty) {
        // `flagsAtRaw` ignores the cell argument today (Phase 5
        // placeholder body). Adapter must not crash on OOB and
        // must return Empty.
        Tilemap m;
        m.cols = 2;
        m.rows = 2;
        TilemapCollisionQueryAdapter adapter(m);
        // Out-of-range cells.
        CHECK_TRUE(adapter.flagsAt(TileCoord{-1, -1}) == CollisionFlags::Empty);
        CHECK_TRUE(adapter.flagsAt(TileCoord{99, 99}) == CollisionFlags::Empty);
        CHECK_FALSE(adapter.isBlocked(TileCoord{-1, -1}));
    }

    // ------------------------------------------------------------------
    // D3 (§13.35) raycast walker test matrix. The 5 cases below are
    // the reshape of `AdapterRaycastAlwaysMiss` (positive baseline) +
    // 4 cardinal-direction positive cases.
    // ------------------------------------------------------------------

    TEST_CASE(AdapterRaycastHitsSolidCellEast) {
        // D3 L-3D-1/L-3D-2/L-3D-4 positive baseline. 4x4 grid,
        // tile 7 at cell(2,1). Ray east from cell(0,1) center
        // walks through (1,1) (empty), enters (2,1) (solid).
        // cellToWorld({0,1}) = (16, 48) (cell center).
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{2, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 48.0f}; // cell (0,1) center
        r.direction = {1.0f, 0.0f};   // east
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hit.hit);
        CHECK_TRUE(hit.cell.x == 2 && hit.cell.y == 1);
        CHECK_TRUE(hit.flags == CollisionFlags::Solid);
        CHECK_FLOAT_EQ(hit.t, 32.0f * 1.5f, 1e-6f); // 1.5 cells east
        CHECK_FLOAT_EQ(hit.point.x, 32.0f * 2.0f, 1e-6f); // entry to cell (2,1)
        CHECK_FLOAT_EQ(hit.point.y, 48.0f, 1e-6f);
    }

    TEST_CASE(AdapterRaycastHitsSolidCellWest) {
        // D3 L-3D-1/L-3D-2 mirror of east. 4x4 grid, tile 7 at
        // cell(1,1). Ray west from cell(2,1) center enters
        // cell(1,1) immediately at the next boundary.
        // cellToWorld({2,1}) = (80, 48). Cell(1,1) east boundary
        // x=64 is 16 units west of origin (heading west).
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{1, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {80.0f, 48.0f}; // cell (2,1) center
        r.direction = {-1.0f, 0.0f};  // west
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hit.hit);
        CHECK_TRUE(hit.cell.x == 1 && hit.cell.y == 1);
        CHECK_TRUE(hit.flags == CollisionFlags::Solid);
        // t = 16 (boundary x=64 is 16 units west along direction (-1,0)).
        CHECK_FLOAT_EQ(hit.t, 16.0f, 1e-6f);
        CHECK_FLOAT_EQ(hit.point.x, 64.0f, 1e-6f); // boundary at x=64
        CHECK_FLOAT_EQ(hit.point.y, 48.0f, 1e-6f);
    }

    TEST_CASE(AdapterRaycastHitsSolidCellSouth) {
        // D3 L-3D-1/L-3D-2. 4x4 grid, tile 7 at cell(1,2). Ray
        // south (positive y) from cell(1,0) center walks
        // through (1,1) (empty), enters (1,2) (solid).
        // cellToWorld({1,0}) = (48, 16).
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{1, 2}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {48.0f, 16.0f}; // cell (1,0) center
        r.direction = {0.0f, 1.0f};   // south (positive y)
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hit.hit);
        CHECK_TRUE(hit.cell.x == 1 && hit.cell.y == 2);
        CHECK_TRUE(hit.flags == CollisionFlags::Solid);
        CHECK_FLOAT_EQ(hit.t, 32.0f * 1.5f, 1e-6f); // 1.5 cells south
        CHECK_FLOAT_EQ(hit.point.x, 48.0f, 1e-6f);
        CHECK_FLOAT_EQ(hit.point.y, 32.0f * 2.0f, 1e-6f); // boundary y=64
    }

    TEST_CASE(AdapterRaycastHitsSolidCellNorth) {
        // D3 L-3D-1/L-3D-2. 4x4 grid, tile 7 at cell(1,1). Ray
        // north (negative y) from cell(1,2) center enters
        // cell(1,1) immediately at the next boundary.
        // cellToWorld({1,2}) = (48, 80). Cell(1,2) north boundary
        // y=64 is 16 units north of origin (heading north).
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{1, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {48.0f, 80.0f}; // cell (1,2) center
        r.direction = {0.0f, -1.0f};  // north (negative y)
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hit.hit);
        CHECK_TRUE(hit.cell.x == 1 && hit.cell.y == 1);
        CHECK_TRUE(hit.flags == CollisionFlags::Solid);
        // t = 16 (boundary y=64 is 16 units along direction (0,-1)).
        CHECK_FLOAT_EQ(hit.t, 16.0f, 1e-6f);
        CHECK_FLOAT_EQ(hit.point.x, 48.0f, 1e-6f);
        CHECK_FLOAT_EQ(hit.point.y, 64.0f, 1e-6f); // boundary y=64
    }

    TEST_CASE(AdapterRaycastHitsDiagonal_NE) {
        // D3 L-3D-1/L-3D-2. Diagonal NE. 8x8 grid, tile 7 at
        // cell(3,3). Ray NE (unit) from cell(0,0) center.
        // Tie-break between x and y crossings is intentionally
        // NOT locked (no L-3D-7); the test asserts hit-cell is
        // among { (2,3), (3,2), (3,3) } which the walker may
        // reach depending on implementation choice. We require
        // that SOME solid cell is hit and the reported `t`
        // matches `pointAt(t) == origin + t * direction` (L-3D-2).
        // cellToWorld({0,0}) = (16, 16).
        Tilemap m;
        m.cols = 8;
        m.rows = 8;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{3, 3}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 16.0f}; // cell (0,0) center
        r.direction = {1.0f, 1.0f};   // NE (unit length)
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hit.hit);
        CHECK_TRUE(hit.flags == CollisionFlags::Solid);
        // Cell must be one of the cells whose boundary the ray
        // crosses before reaching (3,3).
        const int x = hit.cell.x;
        const int y = hit.cell.y;
        CHECK_TRUE((x == 2 && y == 3) || (x == 3 && y == 2) || (x == 3 && y == 3));
        // L-3D-2: pointAt(t) == origin + t * direction.
        const float px = r.origin.x + hit.t * r.direction.x;
        const float py = r.origin.y + hit.t * r.direction.y;
        CHECK_FLOAT_EQ(hit.point.x, px, 1e-6f);
        CHECK_FLOAT_EQ(hit.point.y, py, 1e-6f);
    }

    TEST_CASE(AdapterRaycastHitPointIsBoundaryNotCellCenter) {
        // D3 L-3D-2. East ray hits cell(2,1). The reported
        // `hit.point.x` must be the boundary x=64, NOT the
        // cell-center x=80 (= cellToWorld({2,1}).x).
        // cellToWorld({0,1}) = (16, 48) — origin.
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{2, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 48.0f};
        r.direction = {1.0f, 0.0f};
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hit.hit);
        CHECK_FLOAT_EQ(hit.point.x, 32.0f * 2.0f, 1e-6f); // boundary
        // Cell-center x for (2,1) is 80; we are explicitly NOT there.
        CHECK_TRUE(hit.point.x != 32.0f * 2.5f);
        CHECK_FLOAT_EQ(hit.point.y, 48.0f, 1e-6f);
    }

    TEST_CASE(AdapterRaycastMissesWhenNoSolidInPath) {
        // D3 L-3D-1 (no-hit). 4x4 grid with no blocked tiles;
        // ray east from cell(0,0) center exits the grid at
        // x=128 without hitting anything.
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 16.0f};
        r.direction = {1.0f, 0.0f};
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_FALSE(hit.hit);
        // No-hit sentinel per L-3D-3 / §13.13 case 5 retention.
        CHECK_TRUE(hit.flags == CollisionFlags::None);
        CHECK_INT_EQ(static_cast<int>(hit.t), 0);
    }

    TEST_CASE(AdapterRaycastRespectsMaxDistanceCutoff) {
        // D3 L-3D-6. 4x4 grid, tile 7 at cell(3,1). Ray east
        // from cell(0,1) center = (16, 48). Cell(3,1) entry t
        // = 2.5 cells * 32 = 80.0f. maxDistance = 50.0f;
        // walker must stop before reaching (3,1).
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{3, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 48.0f};
        r.direction = {1.0f, 0.0f};
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/50.0f);
        CHECK_FALSE(hit.hit);
        CHECK_TRUE(hit.flags == CollisionFlags::None);
    }

    TEST_CASE(AdapterRaycastHonorsTMinSkipsLeadingCell) {
        // D3 L-3D-5. 4x4 grid, tile 7 at cell(1,1). East ray
        // from cell(0,1) center = (16, 48) would normally
        // hit cell(1,1) at t=32.0f. Setting tMin = 100.0f
        // makes the walker skip cell (1,1) without testing.
        // Ray then walks (2,1) (empty), (3,1) (empty), exits
        // grid → no-hit.
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{1, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 48.0f};
        r.direction = {1.0f, 0.0f};
        r.tMin      = 100.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_FALSE(hit.hit);
        CHECK_TRUE(hit.flags == CollisionFlags::None);
        // Sanity: with tMin=0 the same setup DOES hit. Verify
        // the tMin path is the discriminator.
        r.tMin = 0.0f;
        RaycastHit2D hitNoTMin = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_TRUE(hitNoTMin.hit);
    }

    TEST_CASE(AdapterRaycastDegenerateDirectionReturnsSentinel) {
        // D3 L-3D-3. Degenerate (0,0) direction → no-hit
        // sentinel immediately (no progress possible).
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        m.tileWidth = 32;
        m.tileHeight = 32;
        m.setTile(TileCoord{2, 1}, 7u);
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {16.0f, 16.0f}; // cell (0,0) center
        r.direction = {0.0f, 0.0f};   // degenerate
        r.tMin      = 0.0f;
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/1000.0f);
        CHECK_FALSE(hit.hit);
        CHECK_INT_EQ(static_cast<int>(hit.t), 0);
        CHECK_TRUE(hit.flags == CollisionFlags::None);
    }

TEST_SUITE_END