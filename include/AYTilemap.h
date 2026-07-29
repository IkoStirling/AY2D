#pragma once
// AYTilemap.h — 2D grid container.
//
// design.md §3 + §6: Tilemap holds a finite (cols x rows) tile-id
// array per the chosen TileIdPackMode (declared in AYTileCoord.h), a
// per-tile slot for CollisionFlags (per design.md §8.1), and a
// `loadState` for the ECS inspector (design.md F-18).
//
// `TileCoord` addressing is the public API surface: callers do not
// touch a flat-array index. Bounds checks return a default-constructed
// tile-id rather than crashing, matching design.md §11 / Phase 2
// load failure path contract.
//
// Member methods are inline because they are short, branch-predictable,
// and called from the tilemap draw system on the hot path. The
// "complex" cases — loadChunkFromSource / resize that may grow the
// underlying vector — are still inline because the impl is one line
// away; the chunk-source request stays in src/AYTilemap.cpp.

#include <cstdint>
#include <vector>

#include "AYAtlasDesc.h"
#include "AYTileAnimation.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"

namespace ayt::ay2d {

// Forward-declared in AYChunkRequestHandle.h; included via AYTileCoord.h
// transitively (TileCoord.h doesn't depend on it), so we include here
// directly.
class ITilemapChunkSource;

struct Tilemap {
    uint32_t           tileWidth        = 0;
    uint32_t           tileHeight       = 0;
    uint32_t           defaultTileId    = 0;  // resource metadata (design.md §6.3)
    uint32_t           cols             = 0;  // axis-aligned tile grid (design.md §6.1)
    uint32_t           rows             = 0;
    TileIdPackMode     mode             = TileIdPackMode::Narrow16;
    TileLoadState      loadState        = TileLoadState::Unloaded;

    // Flat storage. Only ONE of these is populated at a time; pick is
    // driven by `mode`. Lazy allocation: both vectors stay empty until
    // either setTile or loadChunkFromSource forces a resize.
    std::vector<uint16_t> tileIds16;
    std::vector<uint32_t> tileIds32;

    // Phase 3B: per-tile animation table + state.
    // design.md §7.2: (tileId -> [frameTileId, durationMs]*) + state.
    // `animationTable.size()` is the largest registered sourceTileId + 1
    // (sparse; index lookup is O(1) by tileId when in-range, no-op when
    // out-of-range). Hot-path index bound: TileIdPackMode::Narrow16 max
    // (65 535) caps the table extent.
    TileAnimationTable animationTable;
    TileAnimationState animationState;

    // Phase 3B: last-tick baseline (Present-lane time). Default = 0
    // meaning "never ticked"; the first tick sets this and returns
    // without advancing frames (no initial jump from a stale `now`).
    // Stored as raw us (int64_t) — the value-type `ayt::time::TimePoint`
    // is owned by the caller; this is the cache of the last tick's
    // clock reading.
    int64_t lastTickUs    = 0;
    bool    hasBeenTicked = false;

    // -----------------------------------------------------------------------
    // Member helpers
    // -----------------------------------------------------------------------

    // Set the tile-id at (col, row). Out-of-range writes are silently
    // dropped (no allocation, no exception) — design.md §6.3 defaults
    // take over for ECS drawability.
    void setTile(TileCoord cell, uint32_t tileId) noexcept {
        if (cols == 0 || rows == 0) return;
        if (cell.x < 0 || cell.y < 0) return;
        const uint32_t col = static_cast<uint32_t>(cell.x);
        const uint32_t row = static_cast<uint32_t>(cell.y);
        if (col >= cols || row >= rows) return;
        const size_t idx = static_cast<size_t>(row) * cols + col;
        const size_t expected = static_cast<size_t>(cols) * rows;
        if (mode == TileIdPackMode::Narrow16) {
            if (tileId > 0xFFFFu) return;
            // Fill on first write. The fill value is `defaultTileId`
            // (clamped to 16-bit range) so an in-range cell that the
            // user has not explicitly setTile()ed reads as the
            // default — matching design.md §6.3 + the
            // DefaultTileIdAppliedOnRead unit-test invariant.
            if (tileIds16.size() != expected) {
                const uint16_t fill = (defaultTileId > 0xFFFFu)
                    ? uint16_t{0}
                    : static_cast<uint16_t>(defaultTileId);
                tileIds16.assign(expected, fill);
            }
            tileIds16[idx] = static_cast<uint16_t>(tileId);
        } else {
            if (tileIds32.size() != expected) {
                tileIds32.assign(expected, defaultTileId);
            }
            tileIds32[idx] = tileId;
        }
    }

    // Get the tile-id at (col, row). Out-of-range reads return
    // `defaultTileId` so ECS consumers stay safe (design.md §6.3).
    // In-range reads return the underlying storage value, which is
    // either `defaultTileId` (lazy-fill default) or whatever
    // `setTile` last wrote.
    [[nodiscard]] uint32_t getTile(TileCoord cell) const noexcept {
        if (cols == 0 || rows == 0) return defaultTileId;
        if (cell.x < 0 || cell.y < 0) return defaultTileId;
        const uint32_t col = static_cast<uint32_t>(cell.x);
        const uint32_t row = static_cast<uint32_t>(cell.y);
        if (col >= cols || row >= rows) return defaultTileId;
        const size_t idx = static_cast<size_t>(row) * cols + col;
        if (mode == TileIdPackMode::Narrow16) {
            return idx < tileIds16.size()
                ? static_cast<uint32_t>(tileIds16[idx])
                : defaultTileId;
        }
        return idx < tileIds32.size() ? tileIds32[idx] : defaultTileId;
    }

    [[nodiscard]] bool isInRange(TileCoord cell) const noexcept {
        if (cols == 0 || rows == 0) return false;
        if (cell.x < 0 || cell.y < 0) return false;
        return static_cast<uint32_t>(cell.x) < cols
            && static_cast<uint32_t>(cell.y) < rows;
    }

    [[nodiscard]] uint64_t tileCount() const noexcept {
        return static_cast<uint64_t>(cols) * static_cast<uint64_t>(rows);
    }

    // Phase 5+ replacement: returns Empty for any cell (no per-tile
    // backing store yet). Consumers (ITileCollisionQuery::isBlocked)
    // treat Empty as "no collision".
    [[nodiscard]] uint32_t flagsAtRaw(TileCoord cell) const noexcept {
        (void)cell;
        return 0u;  // Empty bit (1<<6) — see design.md §8.1
    }

    // Replace the grid dimensions + storage width. Resets storage to
    // empty; the caller is expected to reload chunk data via
    // loadChunkFromSource.
    void resizeGrid(uint32_t newCols, uint32_t newRows, TileIdPackMode newMode) noexcept {
        if (newCols == 0 || newRows == 0) return;
        cols = newCols;
        rows = newRows;
        mode = newMode;
        tileIds16.clear();
        tileIds32.clear();
        loadState = TileLoadState::Unloaded;
    }

    void clear() noexcept {
        tileIds16.clear();
        tileIds32.clear();
        loadState = TileLoadState::Unloaded;
    }
};

// Free function: pump a chunk delivery from `source` into `t`. Lives
// outside the struct so future non-trivial paths (async pumps,
// partial-up on width mismatch, dual-source merging) can grow into
// separate .cpp bodies without churning the header.
//
// Defined in src/AYTilemap.cpp. Returns true iff `t.loadState` ends
// up as `Loaded`. A Failed / Loading transition may be retried later
// (the failure path doesn't bar future attempts).
[[nodiscard]] bool loadChunkFromSource(Tilemap&             t,
                                      ITilemapChunkSource* source,
                                      ChunkCoord           coord) noexcept;

// Phase 3B: advance per-tile animation state by the time elapsed
// since the last call. `nowUs` is `ayt::time::Clock::gameNow().toUs()`
// (or `performanceNowUs()` for tests). Free function (not member)
// so the future `TilemapAnimationTickSystem` ECS system can call
// it without friending, and so tests can drive it directly.
//
// Defined in src/AYTilemapAnimation.cpp.
//
// Behavior (design.md §7.2 + §9.4):
//   * First call (`hasBeenTicked == false`) sets the baseline and
//     returns without advancing frames (no initial jump).
//   * Reversed clock (nowUs < lastTickUs) clamps delta to 0 (R-7
//     spirit: never let time run backwards).
//   * deltaMs == 0 is a no-op (no allocation, no walk).
//   * For each tileId that has a non-empty animation entry, walks
//     that entry: elapsedMs += deltaMs; while elapsedMs >=
//     frame.durationMs, advance frame index (mod entry.size()) and
//     subtract durationMs from elapsedMs. Loops around the sequence.
//   * Animation state vectors lazily resize to the table extent
//     (allocation only when a new higher tileId is registered).
void tickTilemapAnimation(Tilemap& t, int64_t nowUs) noexcept;

// Phase 3B: resolve the live tile id under animation. Returns the
// `sourceTileId` unchanged when no animation entry exists (fast no-op
// for the no-animation path — the common case).
//
// `sourceTileId` is the raw value from tileIds16/32 (what `setTile`
// wrote). The animated path looks up `animationTable[sourceTileId]`
// and returns `frames[currentFrameIdx % frames.size()].frameTileId`.
[[nodiscard]] uint32_t resolveAnimatedTileId(const Tilemap& t,
                                            uint32_t      sourceTileId) noexcept;

} // namespace ayt::ay2d
