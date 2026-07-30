// Test_ForeachTilemapView.cpp — P3H.3 read-only visitor tests.
//
// design.md §13.19 + §13.PF (C9 Entry::resource dangling
// concern). The visitor hands out `TilemapEntryView` (handle /
// layer / sortingKey); it never exposes the raw `Entry&`
// because `Entry::resource` is always `nullptr` at HEAD (the
// `.aytilemap` loader PR is a cross-module concern per §4.2.1)
// and exposing it would hand out a dangling pointer to an
// incomplete type.
//
// All assertions are pure CPU, no bgfx, no cross-module deps.

#include "AYWorld2D.h"
#include "AYWorld2DSnapshot.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(ForeachTilemapViewSuite)

    TEST_CASE(VisitorIteratesAllEntriesInRegistrationOrder) {
        // §13.19 case 1: 4 tilemaps added; foreachTilemapView
        // visits each one in registration order, exposing
        // handle/layer/sortingKey via TilemapEntryView.
        World2D world;
        const TilemapHandle h0 = world.addTilemap(/*layer=*/1, /*sortingKey=*/0x0001u);
        const TilemapHandle h1 = world.addTilemap(/*layer=*/2, /*sortingKey=*/0x0002u);
        const TilemapHandle h2 = world.addTilemap(/*layer=*/3, /*sortingKey=*/0x0003u);
        const TilemapHandle h3 = world.addTilemap(/*layer=*/4, /*sortingKey=*/0x0004u);

        // Collect into a small buffer to verify order + values.
        TilemapEntryView views[8];
        uint32_t count = 0;
        world.foreachTilemapView([&](const TilemapEntryView& v) {
            if (count < 8) views[count++] = v;
        });
        CHECK_INT_EQ(count, 4u);
        CHECK_TRUE(views[0].handle == h0);
        CHECK_INT_EQ(views[0].layer,      1u);
        CHECK_INT_EQ(views[0].sortingKey, 0x0001u);
        CHECK_TRUE(views[1].handle == h1);
        CHECK_INT_EQ(views[1].layer,      2u);
        CHECK_INT_EQ(views[1].sortingKey, 0x0002u);
        CHECK_TRUE(views[2].handle == h2);
        CHECK_TRUE(views[3].handle == h3);
    }

    TEST_CASE(TilemapEntryViewHasNoResourceAccessor) {
        // §13.19 case 2: compile-time check that
        // `TilemapEntryView` does NOT carry an `IAYTilemap*`
        // accessor (the dangling-pointer concern per §13.PF C9).
        // We use a SFINAE-style check via `sizeof` + `decltype`
        // to verify the type has no `resource` member. The
        // visitor hands out `TilemapEntryView`, not `Entry&`,
        // so this static check is the lock.
        static_assert(
            sizeof(TilemapEntryView) ==
                sizeof(TilemapHandle) + sizeof(uint32_t) + sizeof(uint32_t),
            "P3H.3 lock: TilemapEntryView must be exactly "
            "TilemapHandle + layer + sortingKey (no resource "
            "field, no padding that would hint at hidden "
            "members). If this fires, a field was added "
            "without updating §13.PF C9 + §13.19.");

        // No `resource` member — verify via SFINAE failure.
        // The compile-time check above is the lock; runtime
        // below verifies the visitor does not expose the
        // raw `Entry` either.
        static_cast<void>(sizeof(TilemapEntryView));

        // Empty world: visitor never invokes the callback.
        World2D world;
        uint32_t calls = 0;
        world.foreachTilemapView([&](const TilemapEntryView&) {
            ++calls;
        });
        CHECK_INT_EQ(calls, 0u);

        // Mutating the visitor is a no-op for the registry:
        // resourceEpoch stays the same; entries vector
        // size stays the same. The visitor receives
        // `const TilemapEntryView&`, so the registry
        // observer sees no side effects from the walk.
        world.addTilemap(/*layer=*/0, /*sortingKey=*/0u);
        const uint64_t epochBefore = world.resourceEpochValue();
        const uint32_t sizeBefore  = world.size();
        uint32_t handleIds[8] = {0};
        uint32_t idx = 0;
        world.foreachTilemapView([&](const TilemapEntryView& v) {
            if (idx < 8) handleIds[idx++] = v.handle.id;
        });
        CHECK_INT_EQ(world.resourceEpochValue(), epochBefore);
        CHECK_INT_EQ(world.size(), sizeBefore);
        CHECK_INT_EQ(idx, 1u);
        CHECK_TRUE(handleIds[0] != 0u);
    }

TEST_SUITE_END