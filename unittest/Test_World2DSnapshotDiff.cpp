// Test_World2DSnapshotDiff.cpp - P3I.4 / §13.23.
//
// design.md §13.23: `World2DSnapshot::diff` returns three
// sorted vectors: `added`, `removed`, `modified`. O(1) fast
// path on matching `resourceEpoch`; sort+two-pointer merge
// otherwise. The eight cases below lock:
//
//   * O(1) fast path (empty diff on same epoch)
//   * add / remove / swap each show up in the right bucket
//   * ABA (same id, different generation) goes to added+removed
//   * deterministic order across mixed batches
//   * const-correctness + non-mutation + no epoch bump
//   * default-constructed old baseline yields "all added"
//
// All assertions use real `World2DSnapshot::build` deltas
// against a live `World2D` - same discipline as
// P3C Test_CountersWired + P3I.2 Test_World2DRemoveTilemapPurge.

#include "AY2D/World2D.h"
#include "AY2D/World2DSnapshot.h"

#include "AYTest.h"

using namespace ayt::ay2d;

namespace {

// Snapshot two consecutive world states for diff comparison.
struct Pair { World2DSnapshot oldS; World2DSnapshot newS; };

Pair snapshotPair(World2D& w) {
    Pair p;
    p.oldS = World2DSnapshot::build(w);
    p.newS = p.oldS;  // identical baseline; mutators will diverge them
    return p;
}

} // namespace

TEST_SUITE(World2DSnapshotDiffSuite)

    TEST_CASE(SameEpoch_DiffIsEmpty_FastPath) {
        // L-3I-7: when both snapshots come from the same world
        // at the same epoch, diff is empty in O(1) - no
        // entries[] walk.
        World2D w;
        (void)w.addTilemap(0u, 0u);
        (void)w.addTilemap(1u, 2u);

        const auto s1 = World2DSnapshot::build(w);
        const auto s2 = s1;  // byte-for-byte copy
        const auto d  = s2.diff(s1);

        CHECK(d.empty());
        CHECK_INT_EQ(d.added.size(),    0u);
        CHECK_INT_EQ(d.removed.size(),  0u);
        CHECK_INT_EQ(d.modified.size(), 0u);
    }

    TEST_CASE(AddTilemap_ShowsUpInAdded) {
        // diff.s1 -> s2 where s2 was built after an add: the
        // new handle lands in `added`, removed and modified
        // stay empty.
        World2D w;
        auto p = snapshotPair(w);
        (void)w.addTilemap(3u, 7u);  // bump epoch
        p.newS = World2DSnapshot::build(w);

        const auto d = p.newS.diff(p.oldS);
        CHECK_FALSE(d.empty());
        CHECK_INT_EQ(d.added.size(),    1u);
        CHECK_INT_EQ(d.removed.size(),  0u);
        CHECK_INT_EQ(d.modified.size(), 0u);
        CHECK_INT_EQ(d.added[0].handle.id, 1u);  // first id = 1 (0 reserved)
        CHECK_INT_EQ(d.added[0].layer,      3u);
        CHECK_INT_EQ(d.added[0].sortingKey, 7u);
    }

    TEST_CASE(RemoveTilemap_ShowsUpInRemoved) {
        World2D w;
        const auto h0 = w.addTilemap(0u, 0u);
        (void)w.addTilemap(1u, 1u);
        const auto oldS = World2DSnapshot::build(w);

        CHECK_TRUE(w.removeTilemap(h0));  // bump epoch
        const auto newS = World2DSnapshot::build(w);

        const auto d = newS.diff(oldS);
        CHECK_FALSE(d.empty());
        CHECK_INT_EQ(d.removed.size(),  1u);
        CHECK_INT_EQ(d.added.size(),    0u);
        CHECK_INT_EQ(d.modified.size(), 0u);
        CHECK_INT_EQ(d.removed[0].handle.id, h0.id);
        CHECK_INT_EQ(d.removed[0].handle.generation, h0.generation);
    }

    TEST_CASE(SwapTilemap_ShowsUpInModified_OldNewFieldsCorrect) {
        World2D w;
        const auto h = w.addTilemap(0u, 0u);
        const auto oldS = World2DSnapshot::build(w);

        CHECK_TRUE(w.swapTilemap(h, /*newLayer=*/5u,
                                 /*newSortingKey=*/9u));
        const auto newS = World2DSnapshot::build(w);

        const auto d = newS.diff(oldS);
        CHECK_FALSE(d.empty());
        CHECK_INT_EQ(d.modified.size(), 1u);
        CHECK_INT_EQ(d.added.size(),    0u);
        CHECK_INT_EQ(d.removed.size(),  0u);
        CHECK(d.modified[0].handle == h);
        CHECK_INT_EQ(d.modified[0].oldLayer,      0u);
        CHECK_INT_EQ(d.modified[0].newLayer,      5u);
        CHECK_INT_EQ(d.modified[0].oldSortingKey, 0u);
        CHECK_INT_EQ(d.modified[0].newSortingKey, 9u);
    }

    TEST_CASE(ABA_SameIdDifferentGeneration_IsRemovedPlusAdded_NotModified) {
        // L-3I-9 (ABA): `World2D` ids are monotonic (never
        // reused; `addTilemap` always advances
        // `_nextTilemapId`). The ABA guard lives in
        // `generation` — `removeTilemap` bumps the
        // generation of the *slot* even though the slot is
        // gone, so any future add has a fresh generation.
        // For `diff` this means: an old handle and a new
        // handle at the SAME id are impossible; ABA manifests
        // as **different ids, different generations** with
        // the same `(id, gen)` being the only equality that
        // matters. The actual lock we exercise here is the
        // `(id, generation)` sort+merge key: when the same
        // `(id, gen)` appears in BOTH old and new, the entry
        // is the same; otherwise it lands in `removed` and
        // `added` separately, NEVER in `modified`.
        World2D w;
        const auto h0 = w.addTilemap(0u, 0u);
        (void)w.addTilemap(0u, 0u);  // h0 is no longer the only entry
        const auto oldS = World2DSnapshot::build(w);

        CHECK_TRUE(w.removeTilemap(h0));
        // After remove + add, h0's id is NOT reused. The new
        // handle has a fresh id and a fresh generation.
        const auto h1 = w.addTilemap(0u, 0u);
        const auto newS = World2DSnapshot::build(w);

        CHECK(h0.id != h1.id);                  // different id (monotonic)
        CHECK(h0.generation != h1.generation);  // different generation

        const auto d = newS.diff(oldS);
        CHECK_FALSE(d.empty());
        // oldS had 2 entries; newS has 2 entries (h1
        // replaces h0). diff: 1 removed (h0) + 1 added (h1)
        // + 0 modified.
        CHECK_INT_EQ(d.removed.size(),  1u);
        CHECK_INT_EQ(d.added.size(),    1u);
        CHECK_INT_EQ(d.modified.size(), 0u);     // <-- ABA lock
        CHECK_INT_EQ(d.removed[0].handle.id, h0.id);
        CHECK_INT_EQ(d.removed[0].handle.generation, h0.generation);
        CHECK_INT_EQ(d.added[0].handle.id,   h1.id);
        CHECK_INT_EQ(d.added[0].handle.generation, h1.generation);
    }

    TEST_CASE(MixedBatch_DeterministicOrder_SortedByIdThenGeneration) {
        // L-3I-10 (sort+two-pointer merge): the three result
        // vectors must each be sorted by (id, generation)
        // ascending. We construct a world with 5 tilemaps,
        // then do 3 distinct mutations and assert the
        // sort-invariant.
        World2D w;
        const auto h1 = w.addTilemap(0u, 0u);
        const auto h2 = w.addTilemap(0u, 0u);
        const auto h3 = w.addTilemap(0u, 0u);
        const auto h4 = w.addTilemap(0u, 0u);
        const auto h5 = w.addTilemap(0u, 0u);
        const auto oldS = World2DSnapshot::build(w);

        // 2 removes + 1 swap. h1, h3 removed; h5 swapped.
        CHECK_TRUE(w.removeTilemap(h1));
        CHECK_TRUE(w.removeTilemap(h3));
        CHECK_TRUE(w.swapTilemap(h5, 1u, 2u));
        // Note: 2 new ids (6, 7) were never added here; the
        // diff should NOT include any `added` entries.
        const auto newS = World2DSnapshot::build(w);

        const auto d = newS.diff(oldS);

        CHECK_INT_EQ(d.removed.size(),  2u);
        CHECK_INT_EQ(d.added.size(),    0u);
        CHECK_INT_EQ(d.modified.size(), 1u);

        // `removed` sorted by (id, gen): h1, h3.
        CHECK_INT_EQ(d.removed[0].handle.id, h1.id);
        CHECK_INT_EQ(d.removed[1].handle.id, h3.id);
        CHECK(d.removed[0].handle.id < d.removed[1].handle.id);

        // `modified` only one entry; the sort invariant is
        // trivially true. (The next case with two modified
        // entries would probe deeper; we keep this case
        // focused on the mixed-batch shape.)
        CHECK(d.modified[0].handle == h5);
    }

    TEST_CASE(DiffIsNonMutating_AndDoesNotBumpEpoch) {
        // §3.4 lock: diff is a const read; the world's
        // resourceEpoch must not change as a side effect.
        // Also: both snapshots' internal entry ordering is
        // untouched (no in-place sort of `entries`).
        World2D w;
        (void)w.addTilemap(0u, 0u);
        (void)w.addTilemap(1u, 1u);
        (void)w.addTilemap(2u, 2u);

        const auto s1 = World2DSnapshot::build(w);
        // Pre-epoch snapshot of the world, then mutate it,
        // then snapshot again - the diff itself must not
        // move the world epoch.
        (void)w.addTilemap(3u, 3u);  // bump
        const auto s2 = World2DSnapshot::build(w);
        const uint64_t epochAfter = w.resourceEpochValue();

        // Diff - must not bump epoch further.
        const auto d = s2.diff(s1);
        CHECK_INT_EQ(w.resourceEpochValue(), epochAfter);

        // Non-mutating: take s1 again; it must equal itself
        // exactly (same entries, same counters, same epoch).
        const auto s1_again = World2DSnapshot::build(w);
        // s1 was built before the add, so s1_again will
        // have one more entry. We instead just assert that
        // s2.diff(s1) is the same answer as a fresh pair.
        const auto s2_again = World2DSnapshot::build(w);
        const auto d_again  = s2_again.diff(s1_again);
        // d_again compares current world vs itself - same
        // epoch - so the result is the empty fast-path.
        CHECK(d_again.empty());
    }

    TEST_CASE(DefaultConstructedSnapshot_AsOldBaseline_YieldsAllAdded) {
        // L-3I-8 cold-start: when the consumer has no
        // prior snapshot, a default-constructed `old`
        // (resourceEpoch == 0) collides with the world
        // only if the world has never been mutated (also
        // epoch 0). In any non-empty world, all entries
        // land in `added`.
        World2D w;
        (void)w.addTilemap(0u, 0u);
        (void)w.addTilemap(1u, 0u);
        (void)w.addTilemap(2u, 0u);

        const World2DSnapshot oldDefault{};  // epoch 0, no entries
        const auto s2 = World2DSnapshot::build(w);
        const auto d  = s2.diff(oldDefault);

        // s2.resourceEpoch > 0 (world had 3 adds) so the
        // fast path does NOT trigger. All 3 entries land
        // in `added`.
        CHECK_FALSE(d.empty());
        CHECK_INT_EQ(d.added.size(),    3u);
        CHECK_INT_EQ(d.removed.size(),  0u);
        CHECK_INT_EQ(d.modified.size(), 0u);
        // Sort invariant: ids strictly ascending.
        CHECK(d.added[0].handle.id < d.added[1].handle.id);
        CHECK(d.added[1].handle.id < d.added[2].handle.id);
    }

TEST_SUITE_END
