// Test_TilemapBlockedTileIds.cpp — P3I.1 / §13.20 + §13.PF C6-R1 + R3.
//
// design.md §13.20 / R3: `Tilemap::tileCollisionFlags` (per-tile-id
// `CollisionFlags` table) drives `flagsAtRaw`. Three-segment evaluation
// (§13.20 L-3I-2, R3-evolved):
//   1. !isInRange(c)                       -> Empty  (OOB short-circuit)
//   2.  tileCollisionFlags.empty()         -> Empty  (back-compat fast path)
//   3.  tileCollisionFlags.find(getTile(c)):
//        miss  -> Empty
//        hit   -> the stored CollisionFlags bitmask (Solid / OneWay / ...)
//
// The first six cases below exercise that order (formerly the bare
// blocked-id set; now the full flags table). Case 4 is the lock for
// segment 1 (a populated table must NOT turn OOB cells non-Empty);
// case 5 is the lock for the §13.PF C6-R1 retained clauses (the adapter
// is a zero-change pass-through; neither Tilemap nor
// TilemapCollisionQueryAdapter override `isBlocked`). The R3 cases
// (7-9) cover non-Solid flags: OneWay, Slope_L|Solid, Hazard.

#include "AY2D/Tilemap.h"
#include "AY2D/TilemapCollisionAdapter.h"

#include "AYTest.h"

using namespace ayt::ay2d;

namespace {

// Fill an existing `Tilemap` into a 4x4 Narrow16 grid with
// `defaultTileId == 99` and distinct per-cell tile ids. We
// mutate in place because `Tilemap` is non-copyable (it
// carries `Ay2DCounters` with `std::atomic` members, deleted
// copy ctor).
void fillTestTilemap(Tilemap& m) noexcept {
    m.cols = 4u;
    m.rows = 4u;
    m.mode = TileIdPackMode::Narrow16;
    m.defaultTileId = 99u;
    // Lazy-fill: setTile fills the storage; set distinct ids
    // for the 16 cells so flagsAtRaw can distinguish them.
    for (uint32_t y = 0; y < 4u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            m.setTile(TileCoord{int32_t(x), int32_t(y)}, x + y * 4u);
        }
    }
}

} // namespace

TEST_SUITE(TilemapBlockedTileIdsSuite)

    TEST_CASE(EmptyFlagTable_FlagsAtRawIsEmptyEverywhere) {
        // §13.20 L-3I-2 segment 2 — empty table is bit-identical to
        // v0.1.17 behavior. Probe 4 distinct cells (corner, edge,
        // mid, OOB) — all must be Empty.
        Tilemap m;
        fillTestTilemap(m);
        CHECK_TRUE(m.tileCollisionFlags.empty());
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{3, 3}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{-1, -1}),
            static_cast<uint32_t>(CollisionFlags::Empty));
    }

    TEST_CASE(SolidFlagHit_ReturnsSolid) {
        // §13.20 L-3I-2 segment 3 — table hit returns the stored flags,
        // miss returns Empty. Cell (1,0) holds tileId 1, cell (2,0)
        // holds tileId 2, cell (0,0) holds tileId 0.
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[2u] = CollisionFlags::Solid;

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{2, 0}),
            static_cast<uint32_t>(CollisionFlags::Solid));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{1, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));

        // Add a second solid entry; both must hit Solid, others Empty.
        m.tileCollisionFlags[7u] = CollisionFlags::Solid;
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{2, 0}),
            static_cast<uint32_t>(CollisionFlags::Solid));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{3, 1}),
            static_cast<uint32_t>(CollisionFlags::Solid));
    }

    TEST_CASE(FlagRemoved_RevertsToEmpty) {
        // §13.20: erase + clear both restore the Empty branch.
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[5u] = CollisionFlags::Solid;

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{1, 1}),
            static_cast<uint32_t>(CollisionFlags::Solid));

        m.tileCollisionFlags.erase(5u);
        CHECK_TRUE(m.tileCollisionFlags.empty());
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{1, 1}),
            static_cast<uint32_t>(CollisionFlags::Empty));

        // Re-populate then clear; the empty-table fast path kicks in.
        m.tileCollisionFlags[8u] = CollisionFlags::Solid;
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 2}),
            static_cast<uint32_t>(CollisionFlags::Solid));
        m.tileCollisionFlags.clear();
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 2}),
            static_cast<uint32_t>(CollisionFlags::Empty));
    }

    TEST_CASE(OutOfRange_StaysEmpty_EvenWhenDefaultTileIdHasFlags) {
        // §13.20 L-3I-2 segment 1 — OOB short-circuit MUST win
        // over the table lookup. `getTile` is OOB-safe and returns
        // `defaultTileId`, so if we give `defaultTileId` a flag
        // without this short-circuit, an OOB probe would silently
        // turn non-Empty and break the §8.1 contract (OOB == Empty).
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[m.defaultTileId] = CollisionFlags::Solid;  // = 99u

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{-1, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, -1}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{4, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 4}),
            static_cast<uint32_t>(CollisionFlags::Empty));
    }

    TEST_CASE(AdapterIsBlocked_FollowsFlagTable_NoOverride) {
        // §13.PF C6-R1 retained clauses: `Tilemap` does NOT
        // override `isBlocked`; `TilemapCollisionQueryAdapter`
        // does NOT override `isBlocked` either — the
        // `ITileCollisionQuery::isBlocked` base default
        // (`flagsAt(c) != Empty`) is what runs.
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[3u] = CollisionFlags::Solid;
        TilemapCollisionQueryAdapter adapter(m);

        // Hit: cell (3, 0) holds tileId 3 — Solid via base default.
        CHECK_TRUE(adapter.flagsAt(TileCoord{3, 0}) == CollisionFlags::Solid);
        CHECK_TRUE(adapter.isBlocked(TileCoord{3, 0}));

        // Miss: cell (0, 0) holds tileId 0 — Empty via base default.
        CHECK_TRUE(adapter.flagsAt(TileCoord{0, 0}) == CollisionFlags::Empty);
        CHECK_FALSE(adapter.isBlocked(TileCoord{0, 0}));

        // OOB: short-circuit Empty (lock); base default isBlocked=false.
        CHECK_TRUE(adapter.flagsAt(TileCoord{-1, -1}) == CollisionFlags::Empty);
        CHECK_FALSE(adapter.isBlocked(TileCoord{-1, -1}));
    }

    TEST_CASE(TileIds16And32Paths_BothConsultFlagTable) {
        // §13.20: the table is consulted AFTER getTile, so both
        // Narrow16 and Wide32 storage paths must work. We rebuild
        // a Wide32 grid and probe once for each mode.
        Tilemap m16;  // Narrow16
        fillTestTilemap(m16);
        m16.tileCollisionFlags[11u] = CollisionFlags::Solid;
        CHECK_INT_EQ(
            m16.flagsAtRaw(TileCoord{3, 2}),  // holds 11
            static_cast<uint32_t>(CollisionFlags::Solid));

        Tilemap m32;
        m32.cols = 4u;
        m32.rows = 4u;
        m32.mode = TileIdPackMode::Wide32;
        m32.defaultTileId = 0u;
        for (uint32_t y = 0; y < 4u; ++y) {
            for (uint32_t x = 0; x < 4u; ++x) {
                m32.setTile(TileCoord{int32_t(x), int32_t(y)}, x + y * 4u);
            }
        }
        m32.tileCollisionFlags[11u] = CollisionFlags::Solid;
        CHECK_INT_EQ(
            m32.flagsAtRaw(TileCoord{3, 2}),
            static_cast<uint32_t>(CollisionFlags::Solid));

        // Sanity: the unrelated miss cell stays Empty under both modes.
        CHECK_INT_EQ(
            m16.flagsAtRaw(TileCoord{0, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m32.flagsAtRaw(TileCoord{0, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
    }

    // ---- R3: non-Solid flags round-trip through flagsAtRaw ----

    TEST_CASE(OneWayFlag_ReturnsOneWay) {
        // R3: a tile id flagged OneWay (no Solid bit) reads back as OneWay,
        // not Solid. isBlocked (base default `!= Empty`) is still true —
        // OneWay is "occupied" — but the Solid bit is clear, which is what
        // the bridge uses to decide whether to mint a solid static body.
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[2u] = CollisionFlags::OneWay;

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{2, 0}),
            static_cast<uint32_t>(CollisionFlags::OneWay));
        CHECK_FALSE(m.flagsAtRaw(TileCoord{2, 0}) ==
                    static_cast<uint32_t>(CollisionFlags::Solid));
        TilemapCollisionQueryAdapter adapter(m);
        CHECK_TRUE(adapter.flagsAt(TileCoord{2, 0}) == CollisionFlags::OneWay);
        CHECK_TRUE(adapter.isBlocked(TileCoord{2, 0}));  // != Empty
    }

    TEST_CASE(SlopeAndSolidCompositeFlag_ReturnsBothBits) {
        // R3: a slope tile can carry Solid | Slope_L simultaneously.
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[5u] =
            CollisionFlags::Slope_L | CollisionFlags::Solid;

        const uint32_t f = m.flagsAtRaw(TileCoord{1, 1});  // holds 5
        CHECK_TRUE((f & static_cast<uint32_t>(CollisionFlags::Solid)) != 0u);
        CHECK_TRUE((f & static_cast<uint32_t>(CollisionFlags::Slope_L)) != 0u);
        CHECK_INT_EQ(f,
            static_cast<uint32_t>(CollisionFlags::Slope_L |
                                  CollisionFlags::Solid));
    }

    TEST_CASE(HazardFlag_ReturnsHazard_NotSolid) {
        // R3: a Hazard tile reads back as Hazard; the Solid bit is clear.
        Tilemap m;
        fillTestTilemap(m);
        m.tileCollisionFlags[10u] = CollisionFlags::Hazard;  // cell (2,2) holds 10

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{2, 2}),
            static_cast<uint32_t>(CollisionFlags::Hazard));
        CHECK_FALSE((m.flagsAtRaw(TileCoord{2, 2}) &
                    static_cast<uint32_t>(CollisionFlags::Solid)) != 0u);
    }

TEST_SUITE_END
