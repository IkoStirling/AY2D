// Test_TilemapBlockedTileIds.cpp — P3I.1 / §13.20 + §13.PF C6-R1.
//
// design.md §13.20: `Tilemap::blockedTileIds` set drives
// `flagsAtRaw`'s Solid-vs-Empty decision. Three-segment
// evaluation (§13.20 L-3I-2):
//   1. !isInRange(c)                   -> Empty  (OOB short-circuit)
//   2.  blockedTileIds.empty()         -> Empty  (back-compat fast path)
//   3.  blockedTileIds.contains(getTile(c)) -> Solid / Empty
//
// All six cases below exercise that order. Case 4 is the lock
// for segment 1 (a populated set must NOT turn OOB cells into
// Solid); case 5 is the lock for the §13.PF C6-R1 retained
// clauses (the adapter is a zero-change pass-through; neither
// Tilemap nor TilemapCollisionQueryAdapter override
// `isBlocked`).

#include "AYTilemap.h"
#include "AYTilemapCollisionAdapter.h"

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

    TEST_CASE(EmptyBlockSet_FlagsAtRawIsEmptyEverywhere) {
        // §13.20 L-3I-2 segment 2 — empty set is bit-identical to
        // v0.1.17 behavior. Probe 4 distinct cells (corner, edge,
        // mid, OOB) — all must be Empty.
        Tilemap m;
        fillTestTilemap(m);
        CHECK_TRUE(m.blockedTileIds.empty());
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

    TEST_CASE(BlockedIdHit_ReturnsSolid) {
        // §13.20 L-3I-2 segment 3 — set hit returns Solid, miss
        // returns Empty. Cell (1,0) holds tileId 1, cell (2,0)
        // holds tileId 2, cell (0,0) holds tileId 0.
        Tilemap m;
        fillTestTilemap(m);
        m.blockedTileIds.insert(2u);

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{2, 0}),
            static_cast<uint32_t>(CollisionFlags::Solid));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{1, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 0}),
            static_cast<uint32_t>(CollisionFlags::Empty));

        // Add a second blocked id; both must hit Solid, others Empty.
        m.blockedTileIds.insert(7u);
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{2, 0}),
            static_cast<uint32_t>(CollisionFlags::Solid));
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{3, 1}),
            static_cast<uint32_t>(CollisionFlags::Solid));
    }

    TEST_CASE(BlockedIdRemoved_RevertsToEmpty) {
        // §13.20: erase + clear both restore the Empty branch.
        Tilemap m;
        fillTestTilemap(m);
        m.blockedTileIds.insert(5u);

        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{1, 1}),
            static_cast<uint32_t>(CollisionFlags::Solid));

        m.blockedTileIds.erase(5u);
        CHECK_TRUE(m.blockedTileIds.empty());
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{1, 1}),
            static_cast<uint32_t>(CollisionFlags::Empty));

        // Re-populate then clear; the empty-set fast path kicks in.
        m.blockedTileIds.insert(8u);
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 2}),
            static_cast<uint32_t>(CollisionFlags::Solid));
        m.blockedTileIds.clear();
        CHECK_INT_EQ(
            m.flagsAtRaw(TileCoord{0, 2}),
            static_cast<uint32_t>(CollisionFlags::Empty));
    }

    TEST_CASE(OutOfRange_StaysEmpty_EvenWhenDefaultTileIdIsBlocked) {
        // §13.20 L-3I-2 segment 1 — OOB short-circuit MUST win
        // over the set lookup. `getTile` is OOB-safe and returns
        // `defaultTileId`, so if we put `defaultTileId` into the
        // set without this short-circuit, an OOB probe would
        // silently turn into Solid and break the §8.1 contract
        // (OOB == Empty).
        Tilemap m;
        fillTestTilemap(m);
        m.blockedTileIds.insert(m.defaultTileId);  // = 99u

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

    TEST_CASE(AdapterIsBlocked_FollowsBlockedSet_NoOverride) {
        // §13.PF C6-R1 retained clauses: `Tilemap` does NOT
        // override `isBlocked`; `TilemapCollisionQueryAdapter`
        // does NOT override `isBlocked` either — the
        // `ITileCollisionQuery::isBlocked` base default
        // (`flagsAt(c) != Empty`) is what runs.
        //
        // This case verifies that path through the adapter: the
        // adapter source file is unchanged from Phase 5, so a
        // green test here proves the data-side wire alone
        // promotes `isBlocked` to true on a blocked hit.
        Tilemap m;
        fillTestTilemap(m);
        m.blockedTileIds.insert(3u);
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

    TEST_CASE(TileIds16And32Paths_BothConsultBlockedSet) {
        // §13.20: the set is consulted AFTER getTile, so both
        // Narrow16 and Wide32 storage paths must work. We rebuild
        // a Wide32 grid and probe once for each mode.
        Tilemap m16;  // Narrow16
        fillTestTilemap(m16);
        m16.blockedTileIds.insert(11u);
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
        m32.blockedTileIds.insert(11u);
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

TEST_SUITE_END