#pragma once
// AYWorld2D.h — Phase 3 real impl (Phase 0/1+/2 had a placeholder).
//
// design.md §3 + §3.4: World2D is the logical root of a 2D scene;
// it owns the tilemap registry, the resource layer dependency, and
// the `resourceEpoch` counter that lets downstream systems skip
// work on identical frames.
//
// Phase 3 (this commit) keeps the impl CPU-side only:
//   * `addTilemap(path|handle)` / `removeTilemap(id)` /
//     `swapTilemap(id, newPath)` — bumps `resourceEpoch` per
//     design.md §3.4.
//   * `resourceEpoch()` getter.
//   * `TilemapHandles` storage is a small vector-of-pairs (id + path)
//     for now; Phase 4 streaming replaces this with a hash map keyed
//     on `TilemapResourceHandle` (cross-module PR per §4.2.1).
//   * No GPU handles, no bgfx, no `RenderScene2D` build — those are
//     Phase 3+ cross-module concerns.
//
// `World2D` is intentionally NOT a singleton; multiple worlds can
// coexist (e.g. editor preview vs runtime). The lifetime is owned by
// the consumer.

#include <cstdint>
#include <utility>
#include <vector>

#include "AY2DCounters.h"

namespace ayt::ay2d {

struct TilemapHandle {
    uint32_t    id = 0;       // 0 = invalid; monotonic
    uint32_t    generation = 0;  // bumped on remove / swap (ABA safety)

    [[nodiscard]] constexpr bool operator==(const TilemapHandle& o) const noexcept {
        return id == o.id && generation == o.generation;
    }
    [[nodiscard]] constexpr bool operator!=(const TilemapHandle& o) const noexcept {
        return !(*this == o);
    }
};

struct [[deprecated("TilemapBinding is dead code; use TilemapEntryView via World2DSnapshot")]] TilemapBinding {
    TilemapHandle handle;
    uint32_t      layer = 0;     // 0..31
    uint32_t      sortingKey = 0;  // 0..0xFFFFFF
};

// Forward-declared to keep AYWorld2D.h free of AYResource include.
// The actual .aytilemap Loader wiring lands in Phase 3+ via the
// cross-module PR in design.md §4.2.1.
class IAYTilemap;

struct World2D {
    // Monotonically increasing counter. Bumped on every
    // addTilemap / removeTilemap / swapTilemap call, AND when a
    // resource handle resolves a new IResource instance (Phase 3+
    // hot-reload path). Not bumped on per-frame draw submission.
    // design.md §3.4.
    uint64_t resourceEpoch = 0;

    // Tilemap registry. Each entry is a (handle, layer, sortingKey)
    // tuple plus the resource pointer (may be null until the loader
    // resolves). The registry is a small vector because Phase 3
    // caps "instantiated tilemaps per world" at a few hundred —
    // beyond that, Phase 4 streaming replaces this with a hash map.
    struct Entry {
        TilemapHandle       handle;
        uint32_t            layer = 0;
        uint32_t            sortingKey = 0;
        IAYTilemap*         resource = nullptr;  // Phase 3+ root ptr (owning lifetime managed by AYResource)
    };

    std::vector<Entry> entries;

    // Telemetry counters (design.md §10.1.1). Owned by the
    // world; consumers mirror the AYPhysics pattern (see
    // AYPhysicsManager.h:77-79).
    Ay2DCounters counters;

    // -----------------------------------------------------------------------
    // Mutation API (all bump `resourceEpoch`).
    // -----------------------------------------------------------------------

    // Add a tilemap at `layer / sortingKey`. Returns the assigned
    // handle. The resource pointer is null until the loader resolves
    // it (Phase 3+ cross-module PR).
    [[nodiscard]] TilemapHandle addTilemap(uint32_t layer,
                                           uint32_t sortingKey) noexcept;

    // Remove a tilemap by handle. Returns true iff `handle` matched
    // an entry; false if the handle is invalid or the generation
    // bits don't match (basic ABA guard).
    bool removeTilemap(TilemapHandle handle) noexcept;

    // Swap a tilemap's (layer, sortingKey) in one atomic step. Same
    // return contract as removeTilemap.
    bool swapTilemap(TilemapHandle handle,
                     uint32_t newLayer,
                     uint32_t newSortingKey) noexcept;

    // Look up the tilemap binding by handle. Returns nullptr if not
    // found.
    [[nodiscard]] const Entry* find(TilemapHandle handle) const noexcept;
    [[nodiscard]]       Entry* find(TilemapHandle handle)       noexcept;

    // -----------------------------------------------------------------------
    // Read-only API.
    // -----------------------------------------------------------------------

    // Counter accessor (Section §3.4 lock — bumps only on the
    // specific events listed above).
    [[nodiscard]] uint64_t resourceEpochValue() const noexcept {
        return resourceEpoch;
    }

    [[nodiscard]] uint32_t size() const noexcept {
        return static_cast<uint32_t>(entries.size());
    }

    // Layer + sortingKey -> sort key (design.md §7.4):
    // `(layer << 24) | (sortingKey & 0x00FFFFFF)`.
    [[nodiscard]] static constexpr uint32_t packSortKey(uint32_t layer,
                                                        uint32_t sortingKey) noexcept {
        return ((layer & 0x1Fu) << 24) | (sortingKey & 0x00FFFFFFu);
    }

private:
    uint32_t _nextTilemapId = 1;            // 0 = invalid
    uint32_t _nextTilemapGeneration = 1;    // monotonic, ABA-safe

    void bumpEpoch() noexcept { ++resourceEpoch; }
    // Remove the entry whose handle matches exactly. Returns the
    // iterator + a bool signaling whether anything was removed.
    bool removeEntryByHandle(TilemapHandle handle);
};

} // namespace ayt::ay2d
