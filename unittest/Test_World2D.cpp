// Test_World2D.cpp — Phase 3 World2D real-impl tests.
//
// design.md §3 + §3.4: World2D owns the tilemap registry and
// `resourceEpoch` counter. This file exercises the implementation
// in src/AYWorld2D.cpp.
//
// Coverage:
//   * `addTilemap` returns a valid handle and bumps epoch.
//   * `removeTilemap` removes the entry and bumps epoch.
//   * `swapTilemap` updates (layer, sortingKey) and bumps epoch.
//   * The cached ABA guard catches a stale handle (id collides
//     after remove via the generation bits).
//   * `packSortKey` matches §7.4 (layer << 24) | sortingKey.

#include <cstdint>

#include "AY2D/World2D.h"
#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(World2DSuite)

    TEST_CASE(FreshWorldHasZeroEpoch) {
        World2D w;
        CHECK_INT_EQ(w.resourceEpochValue(), 0u);
    }

    TEST_CASE(AddTilemapBumpsEpoch) {
        World2D w;
        const uint64_t before = w.resourceEpochValue();
        TilemapHandle h = w.addTilemap(0, 0);
        CHECK(h.id != 0);
        CHECK(w.resourceEpochValue() == before + 1);
        CHECK(w.size() == 1u);
    }

    TEST_CASE(RemoveTilemapBumpsEpoch) {
        World2D w;
        TilemapHandle h = w.addTilemap(0, 0);
        const uint64_t afterAdd = w.resourceEpochValue();
        CHECK(w.removeTilemap(h));
        CHECK(w.resourceEpochValue() == afterAdd + 1);
        CHECK(w.size() == 0u);
    }

    TEST_CASE(SwapTilemapUpdatesLayerAndSortingKey) {
        World2D w;
        TilemapHandle h = w.addTilemap(0, 0);
        const uint64_t afterAdd = w.resourceEpochValue();
        CHECK(w.swapTilemap(h, 5, 42));
        CHECK(w.resourceEpochValue() == afterAdd + 1);
        const World2D::Entry* e = w.find(h);
        CHECK(e != nullptr);
        CHECK_INT_EQ(e->layer, 5u);
        CHECK_INT_EQ(e->sortingKey, 42u);
    }

    TEST_CASE(RemoveTilemapInvalidatesHandle) {
        World2D w;
        TilemapHandle h = w.addTilemap(0, 0);
        CHECK(w.removeTilemap(h));
        // Same id after removal must NOT find anything (the
        // generation bit is bumped on remove).
        TilemapHandle stale = h;
        CHECK(w.find(stale) == nullptr);
        CHECK_FALSE(w.removeTilemap(stale));
    }

    TEST_CASE(AddThenRemoveThenAddGivesDifferentGeneration) {
        World2D w;
        TilemapHandle h1 = w.addTilemap(0, 0);
        CHECK(w.removeTilemap(h1));
        TilemapHandle h2 = w.addTilemap(0, 0);
        // The id portion is monotonic (advances even after
        // removal — the monotonic counter never reuses ids in
        // Phase 3). The generation is what distinguishes the
        // two handles. The (id, generation) pair is the
        // ABA-safe handle.
        CHECK(h1.generation != h2.generation);
        CHECK(h1 != h2);
    }

    TEST_CASE(PackSortKeyLayerAndSortingKey) {
        // design.md §7.4: (layer << 24) | (sortingKey & 0x00FFFFFF).
        CHECK_INT_EQ(World2D::packSortKey(0, 0), 0u);
        CHECK_INT_EQ(World2D::packSortKey(1, 0), 0x01000000u);
        CHECK_INT_EQ(World2D::packSortKey(0, 0x00ABCDEFu), 0x00ABCDEFu);
        CHECK_INT_EQ(World2D::packSortKey(2, 0x000000FFu), 0x020000FFu);
    }

    TEST_CASE(InvalidHandleIsHandled) {
        World2D w;
        TilemapHandle bad{};  // default = id 0
        CHECK_FALSE(w.removeTilemap(bad));
        CHECK_FALSE(w.swapTilemap(bad, 0, 0));
        CHECK(w.find(bad) == nullptr);
    }

    TEST_CASE(MultipleTilemapsAndFindAll) {
        World2D w;
        TilemapHandle h0 = w.addTilemap(0, 10);
        TilemapHandle h1 = w.addTilemap(1, 20);
        TilemapHandle h2 = w.addTilemap(2, 30);
        CHECK(w.size() == 3u);
        CHECK(w.find(h0) != nullptr);
        CHECK(w.find(h1) != nullptr);
        CHECK(w.find(h2) != nullptr);
        // Sort key encoding matches the (layer, sortingKey).
        const World2D::Entry* e = w.find(h1);
        CHECK(e != nullptr);
        CHECK_INT_EQ(World2D::packSortKey(e->layer, e->sortingKey),
                     World2D::packSortKey(1, 20));
    }

TEST_SUITE_END
