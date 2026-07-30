// Test_World2DSnapshot.cpp — P3H.2 World2DSnapshot value-type tests.
//
// design.md §13.14 (P3H.2 changelog) + §13.PF (C5 TilemapBinding
// deprecation).
//
// All assertions are pure CPU, no bgfx, no cross-module deps.
// The snapshot is read-only; mutating the originating World2D
// after `build()` must NOT affect the snapshot.

#include "AYWorld2D.h"
#include "AYWorld2DSnapshot.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(World2DSnapshotSuite)

    TEST_CASE(EmptyWorldProducesEmptyEntries) {
        // §13.14 case 1: empty World2D -> empty entries; size
        // matches; counters snapshot is default-initialised.
        World2D world;
        World2DSnapshot snap = World2DSnapshot::build(world);
        CHECK_INT_EQ(snap.size(), 0u);
        CHECK_INT_EQ(static_cast<uint32_t>(snap.entries.size()), 0u);
        // counters snapshot fields are all zero on a fresh world.
        CHECK_INT_EQ(snap.counters.chunk_io_reject, 0u);
        CHECK_INT_EQ(snap.counters.tilemaps_in_world, 0u);
    }

    TEST_CASE(SizeMatchesWorldSize) {
        // §13.14 case 2: `World2DSnapshot::size()` matches
        // `World2D::size()` after add/remove. Verifies the
        // snapshot tracks registry mutations correctly via the
        // build-time copy (not a live reference).
        World2D world;
        const TilemapHandle h1 = world.addTilemap(/*layer=*/2, /*sortingKey=*/0xABCDEFu);
        const TilemapHandle h2 = world.addTilemap(/*layer=*/5, /*sortingKey=*/0x123456u);
        CHECK_INT_EQ(world.size(), 2u);

        World2DSnapshot snap = World2DSnapshot::build(world);
        CHECK_INT_EQ(snap.size(), 2u);
        CHECK_INT_EQ(snap.entries.size(), static_cast<size_t>(2));
        CHECK_TRUE(snap.entries[0].handle == h1);
        CHECK_TRUE(snap.entries[1].handle == h2);

        // Mutate the world after build: snapshot must not change.
        (void)world.addTilemap(/*layer=*/1, /*sortingKey=*/0u);
        CHECK_INT_EQ(world.size(), 3u);
        CHECK_INT_EQ(snap.size(), 2u);  // snapshot is decoupled
    }

    TEST_CASE(CountersSnapshotIsStableAcrossMutations) {
        // §13.14 case 3: the counters snapshot is a stable copy.
        // Bumping a counter on the world after build() does NOT
        // affect the snapshot's counters field.
        World2D world;
        World2DSnapshot snap1 = World2DSnapshot::build(world);
        CHECK_INT_EQ(snap1.counters.tilemaps_in_world, 0u);

        // Mutate: add one tilemap (counters.tilemaps_in_world
        // bumps to 1 per P3C §14.2 wiring).
        (void)world.addTilemap(/*layer=*/0, /*sortingKey=*/0u);
        CHECK_INT_EQ(world.counters.tilemaps_in_world.load(), 1u);

        // Original snapshot is unchanged.
        CHECK_INT_EQ(snap1.counters.tilemaps_in_world, 0u);

        // A fresh snapshot reflects the new state.
        World2DSnapshot snap2 = World2DSnapshot::build(world);
        CHECK_INT_EQ(snap2.counters.tilemaps_in_world, 1u);
    }

    TEST_CASE(EntryViewExposesHandleLayerSortingKey) {
        // §13.14 case 4: `TilemapEntryView` carries handle /
        // layer / sortingKey. `resource` is intentionally absent
        // (per §13.PF C5 + the dangling-nullptr concern). The
        // handle round-trips: id + generation bits preserved.
        World2D world;
        const TilemapHandle h = world.addTilemap(/*layer=*/7, /*sortingKey=*/0x00ABCDEFu);
        World2DSnapshot snap = World2DSnapshot::build(world);
        CHECK_INT_EQ(snap.entries.size(), static_cast<size_t>(1));
        CHECK_TRUE(snap.entries[0].handle == h);
        CHECK_INT_EQ(snap.entries[0].layer, 7u);
        CHECK_INT_EQ(snap.entries[0].sortingKey, 0x00ABCDEFu);
    }

    TEST_CASE(TilemapBindingStillCompilesForBackcompat) {
        // §13.14 case 5 + §13.PF C5: `TilemapBinding` is
        // deprecated but still present (additive only — no
        // breaking change). A future PR may remove the type
        // entirely once no consumer remains (grep across the
        // repo confirms zero consumers today). The test verifies
        // the type still exists and has the expected field set
        // by sizeof.
        static_cast<void>(sizeof(TilemapBinding));
        CHECK_TRUE(sizeof(TilemapBinding) >= sizeof(TilemapHandle));
        // Field offsets: `handle` is the first field, so its
        // address equals the struct's address.
        TilemapBinding b{};
        CHECK_INT_EQ(reinterpret_cast<uintptr_t>(&b.handle) -
                     reinterpret_cast<uintptr_t>(&b), 0u);
    }

TEST_SUITE_END