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

// Read-only view of one `World2D::Entry`. Excludes `resource`
// because that field is always `nullptr` at HEAD (the `.aytilemap`
// loader PR ships the real resource via cross-module ownership —
// §3 + §4.2.1). Consumers that need the resource can read the
// `World2D::entries[i].resource` directly (no API in this header
// gates that).
//
// `TilemapEntryView` is plain-data (POD-equivalent) so the snapshot
// can be copied / moved without violating the World2D's invariants.
struct TilemapEntryView {
    TilemapHandle handle     {};       // default = invalid (id=0, gen=0)
    uint32_t      layer      = 0;      // 0..31
    uint32_t      sortingKey = 0;      // 0..0x00FFFFFF
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

    // Build a snapshot of `world` at this moment. `world` is
    // observed via const-ref so the caller may pass a non-mutable
    // world (e.g. a global registered world). Implementation
    // lives in `src/AYWorld2DSnapshot.cpp` (out-of-line so future
    // additions — e.g. a derived "diff vs previous snapshot"
    // helper — do not require header churn).
    [[nodiscard]] static World2DSnapshot build(const World2D& world) noexcept;
};

} // namespace ayt::ay2d