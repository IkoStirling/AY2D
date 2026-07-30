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

    TEST_CASE(AdapterRaycastAlwaysMiss) {
        // §11 Phase 5 row exit gate: "Interface compiles; unit
        // tests for cell lookup." A real axis-aligned tile-grid
        // walker lands with the cross-module PR (§4.2.1); today
        // every raycast misses.
        Tilemap m;
        m.cols = 4;
        m.rows = 4;
        TilemapCollisionQueryAdapter adapter(m);
        Ray2D r;
        r.origin    = {0.0f, 0.0f};
        r.direction = {1.0f, 0.0f};  // east
        RaycastHit2D hit = adapter.raycast(r, /*maxDistance=*/100.0f);
        CHECK_FALSE(hit.hit);
        CHECK_TRUE(hit.flags == CollisionFlags::None);
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

TEST_SUITE_END