#pragma once
// AYWorld2DSnapshot.h — P3H.2 in-AY2D read-only value type.
//
// design.md §13.14 (P3H.2 changelog) + §13.PF (C5 TilemapBinding
// deprecation): this header ships `TilemapEntryView` + `World2DSnapshot`
// as the read-only debug-introspection surface for AY2D.
//
// Earlier plan-mode discussion considered `IWorld2DDebug` (a pure-
// virtual interface), which would force `World2D` to gain a vtable
// and break its POD-ish nature. The Plan-agent's reshape (§13.PF C5)
// settled on a value-type snapshot instead: no vtable, no
// inheritance, no `Entry&` exposure, no dangling `resource` pointer
// to a forward-declared `IAYTilemap`.
//
// The snapshot is built via `World2DSnapshot::build(world)` and
// holds plain values that exist on `World2D` today:
//
//   * `size()`                  — number of registered tilemaps
//   * `entries` (vector of views) — handle / layer / sortingKey
//   * `counters` (Snapshot)     — relaxed atomic snapshot
//
// What is intentionally NOT in the snapshot (each is the home of a
// future cross-module PR):
//   * The `resource` pointer (`Entry::resource` is `nullptr` until
//     the `.aytilemap` loader PR lands; §3 + §4.2.1).
//   * Chunk-source composition (Phase 4 streaming PR; §18.5).
//   * World bounds / extent (World2D carries no extent today).
//
// Lifetime: the snapshot is a value type. The caller may keep it
// after the originating `World2D` mutates; the snapshot reflects
// the state at `build()` time. The `TilemapEntryView::handle`
// retains ABA-safety via the same generation bits as the live
// handle.

#include <cstdint>
#include <vector>

#include "AY2DCounters.h"
#include "AYWorld2D.h"

namespace ayt::ay2d {

// `TilemapEntryView` is defined in `AYWorld2D.h` (P3H.3 §13.19:
// World2D::foreachTilemapView is a header-inline template that
// needs the full definition, so the struct now lives next to
// World2D and is re-exported via the include below). This
// header re-uses the same struct via the include above; no
// re-declaration here.
//
// The struct deliberately excludes `resource` per §13.PF C5 /
// C9: at HEAD `World2D::Entry::resource` is always `nullptr`
// (the `.aytilemap` loader PR is a cross-module concern per
// §4.2.1) and exposing `IAYTilemap*` would hand out a dangling
// pointer to an incomplete type.

// P3I.4 / §13.23: the result of `World2DSnapshot::diff`. The
// three vectors are sorted by `(handle.id, handle.generation)`
// ascending. An empty result means either "the two snapshots
// are identical" or "the world did not change since the
// previous snapshot" (the O(1) fast path).
//
// Defined BEFORE `World2DSnapshot` because `World2DSnapshot::diff`
// returns this type — a method declaration needs the return
// type to be at least forward-declared (and the inner
// `ModifiedEntry` struct to be complete, since the diff
// method's declaration must be parsable before its body is
// compiled).
struct World2DSnapshotDiff {
    // A modified entry: an entry that exists in BOTH the old
    // and the new snapshot (same `(id, generation)`) but whose
    // `layer` and/or `sortingKey` differ. The handle is the
    // common one; the consumer does not need to disambiguate
    // ABA cases because `(id, generation)` is exact.
    struct ModifiedEntry {
        TilemapHandle handle{};
        uint32_t      oldLayer      = 0;
        uint32_t      newLayer      = 0;
        uint32_t      oldSortingKey = 0;
        uint32_t      newSortingKey = 0;
    };

    std::vector<TilemapEntryView> added;
    std::vector<TilemapEntryView> removed;
    std::vector<ModifiedEntry>    modified;

    [[nodiscard]] bool empty() const noexcept {
        return added.empty() && removed.empty() && modified.empty();
    }
};

// Read-only value-type snapshot of `World2D`. Built via
// `World2DSnapshot::build(const World2D&)`. The snapshot is a
// point-in-time copy; mutations to the originating world do not
// affect the snapshot.
struct World2DSnapshot {
    // Number of registered tilemaps (== `world.entries.size()`).
    [[nodiscard]] uint32_t size() const noexcept {
        return static_cast<uint32_t>(entries.size());
    }

    // Per-entry views in registration order. Order matches
    // `world.entries` (the insertion order — `addTilemap`
    // appends; `removeTilemap` erases; `swapTilemap` keeps).
    std::vector<TilemapEntryView> entries;

    // Telemetry snapshot (relaxed atomic load; not internally
    // consistent across fields per §10.1.1 + F-8).
    Ay2DCounters::Snapshot counters;

    // P3I.4 / §13.23: copy of `world.resourceEpoch` at
    // `build()` time. Used by `diff()` to short-circuit the
    // case "the world has not changed at all since the
    // previous snapshot" in O(1) — without this field every
    // `diff()` would have to walk both `entries` vectors to
    // reach the same conclusion.
    uint64_t resourceEpoch = 0;

    // Build a snapshot of `world` at this moment. `world` is
    // observed via const-ref so the caller may pass a non-mutable
    // world (e.g. a global registered world). Implementation
    // lives in `src/AYWorld2DSnapshot.cpp` (out-of-line so future
    // additions — e.g. a derived "diff vs previous snapshot"
    // helper — do not require header churn).
    [[nodiscard]] static World2DSnapshot build(const World2D& world) noexcept;

    // P3I.4 / §13.23: diff `*this` (the NEW snapshot) against
    // `old` (the OLD snapshot). Returns three vectors:
    //   * `added`    — entries in *this, not in old
    //   * `removed`  — entries in old, not in *this
    //   * `modified` — entries in both whose (layer, sortingKey)
    //                  differ between old and *this
    //
    // Both `added` and `removed` are sorted by `(handle.id,
    // handle.generation)` ascending. `modified` is sorted the
    // same way; each `ModifiedEntry` carries both the old and
    // new (layer, sortingKey) so the consumer can react to
    // either direction.
    //
    // O(1) fast path: when `resourceEpoch` matches between the
    // two snapshots AND the two come from the same world, the
    // result is empty (the `entries` vectors are necessarily
    // identical). Same-world precondition is a doc lock, not
    // an enforced field — see §13.23.
    //
    // NOT marked `noexcept`: the function allocates three
    // `std::vector`s. This is the only public method in the
    // AY2D world-shape surface that is allowed to throw
    // `std::bad_alloc`; design.md §13.23 records the rationale.
    [[nodiscard]] World2DSnapshotDiff diff(const World2DSnapshot& old) const;
};

} // namespace ayt::ay2d