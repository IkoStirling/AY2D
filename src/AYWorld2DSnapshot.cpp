// AYWorld2DSnapshot.cpp — P3H.2 in-AY2D snapshot builder.
//
// design.md §13.14 (P3H.2 changelog): out-of-line `build()` so
// future additions (e.g. a derived "diff vs previous snapshot"
// helper, or a windowed average for per-frame counters) do not
// require header churn.

#include "AYWorld2DSnapshot.h"

#include <algorithm>
#include <utility>

namespace ayt::ay2d {

World2DSnapshot World2DSnapshot::build(const World2D& world) noexcept {
    World2DSnapshot out;
    out.entries.reserve(world.entries.size());
    for (const auto& e : world.entries) {
        out.entries.push_back(TilemapEntryView{
            e.handle,
            e.layer,
            e.sortingKey,
        });
    }
    out.counters = world.counters.snapshot();
    // P3I.4 / §13.23: copy the world epoch so `diff()` can
    // short-circuit the "world did not change" case in O(1).
    out.resourceEpoch = world.resourceEpochValue();
    return out;
}

World2DSnapshotDiff
World2DSnapshot::diff(const World2DSnapshot& old) const {
    // P3I.4 / §13.23 L-3I-7: O(1) fast path. The
    // `resourceEpoch` of the two snapshots is the same iff
    // the world has not been touched between the two
    // `build()` calls. The "same world" precondition is a
    // docs lock (L-3I-8): cross-world `diff` is undefined
    // because `resourceEpoch` is a world-local counter and
    // can collide. A consumer that swaps worlds is expected
    // to drop both snapshots and rebuild.
    if (resourceEpoch == old.resourceEpoch) {
        return World2DSnapshotDiff{};
    }

    // Build two index arrays of `const TilemapEntryView*`.
    // Sorting the pointers (rather than the views themselves)
    // keeps the underlying `entries` vectors untouched, so
    // `diff` is genuinely const (no observable mutation).
    std::vector<const TilemapEntryView*> lhs;
    std::vector<const TilemapEntryView*> rhs;
    lhs.reserve(entries.size());
    rhs.reserve(old.entries.size());
    for (const auto& e : entries) lhs.push_back(&e);
    for (const auto& e : old.entries) rhs.push_back(&e);

    // Sort by (handle.id, handle.generation). The handle
    // ordering is the same as the diff merge key, so a
    // two-pointer linear walk lands every entry into the
    // correct `added` / `removed` / `modified` bucket
    // without a hash map.
    const auto keyCmp = [](const TilemapEntryView* a,
                           const TilemapEntryView* b) {
        if (a->handle.id != b->handle.id)
            return a->handle.id < b->handle.id;
        return a->handle.generation < b->handle.generation;
    };
    std::sort(lhs.begin(), lhs.end(), keyCmp);
    std::sort(rhs.begin(), rhs.end(), keyCmp);

    World2DSnapshotDiff out;

    // Two-pointer linear merge. ABA safety: when the same id
    // appears with different generations, the (id, gen) key
    // distinguishes them and they land in `removed` (old gen)
    // and `added` (new gen) — never in `modified`. This is
    // the behavior asserted in test case 5.
    size_t i = 0, j = 0;
    while (i < lhs.size() && j < rhs.size()) {
        const auto& ne = *lhs[i];
        const auto& oe = *rhs[j];
        if (ne.handle.id < oe.handle.id ||
            (ne.handle.id == oe.handle.id &&
             ne.handle.generation < oe.handle.generation)) {
            // New entry has no counterpart in old.
            out.added.push_back(ne);
            ++i;
        } else if (oe.handle.id < ne.handle.id ||
                   (oe.handle.id == ne.handle.id &&
                    oe.handle.generation < ne.handle.generation)) {
            // Old entry has no counterpart in new.
            out.removed.push_back(oe);
            ++j;
        } else {
            // Same (id, generation) — potentially modified.
            if (ne.layer != oe.layer || ne.sortingKey != oe.sortingKey) {
                World2DSnapshotDiff::ModifiedEntry m;
                m.handle         = ne.handle;
                m.oldLayer       = oe.layer;
                m.newLayer       = ne.layer;
                m.oldSortingKey  = oe.sortingKey;
                m.newSortingKey  = ne.sortingKey;
                out.modified.push_back(m);
            }
            ++i;
            ++j;
        }
    }
    // Drain the rest of each side.
    while (i < lhs.size()) out.added.push_back(*lhs[i++]);
    while (j < rhs.size()) out.removed.push_back(*rhs[j++]);

    return out;
}

} // namespace ayt::ay2d