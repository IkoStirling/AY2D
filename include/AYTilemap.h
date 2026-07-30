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
#include <unordered_set>
#include <vector>

#include "aymath/MathTypes.h"

#include "AY2DCounters.h"
#include "AYAtlasDesc.h"
#include "AYTileAnimation.h"
#include "AYTileCollision.h"  // Phase 5: CollisionFlags used by flagsAtRaw body
#include "AYTileCoord.h"
#include "AYTileLoadState.h"
#include "AYTileMath.h"
#include "AYTileRect.h"

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

    // Phase 3C (design.md §14): tile-dimension counters.
    // In-AY2D scope; mutated by setTile (first-write + subsequent
    // writes), resizeGrid, clear, and loadChunkFromSource success
    // path. No-allocation rule: increments are std::atomic
    // fetch_add / store with relaxed ordering (counters are
    // telemetry, not sync points). Snapshot via `counters().snapshot()`.
    Ay2DCounters counters;

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

    // P3I.1 / §13.20: the set of tile ids that count as "blocked"
    // for `flagsAtRaw` / `ITileCollisionQuery::isBlocked`. Source of
    // truth = `.aytilemap` loader metadata (cross-module PR to
    // AYResource, §4.2.1). The AY2D side ships no mutator API: until
    // §11 fixes the ownership of "blocked list", it stays a passive
    // public POD so consumers (tests today; AYPhysics consumer in a
    // future cross-module PR) can populate it via the loader path.
    // Empty by default — the empty-set fast path keeps v0.1.17
    // behavior bit-identical for unpopulated tilemaps (§13.PF C6
    // retained clause).
    std::unordered_set<uint32_t> blockedTileIds{};

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
        bool firstWrite = false;
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
                firstWrite = true;
            }
            tileIds16[idx] = static_cast<uint16_t>(tileId);
        } else {
            if (tileIds32.size() != expected) {
                tileIds32.assign(expected, defaultTileId);
                firstWrite = true;
            }
            tileIds32[idx] = tileId;
        }
        // Phase 3C (§14.2): only successful writes count.
        //   * First-write lazy-fill bumps `tiles_resident` to the full
        //     expected slot count (one bump, not per-cell). Subsequent
        //     writes at already-allocated cells bump `tiles_mutated` by
        //     1 and leave `tiles_resident` untouched.
        //   * The same `tiles_mutated += 1` increment serves every
        //     successful write (first-write and later) so the counter
        //     mirrors mutation events, not "number of cells touched".
        if (firstWrite) {
            counters.tiles_resident.store(expected, std::memory_order_relaxed);
        }
        counters.tiles_mutated.fetch_add(1u, std::memory_order_relaxed);
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

    // Phase 3E (§16): world-coord overloads for `setTile` /
    // `getTile`. These internally delegate to the cell-coordinate
    // forms; the cell-coord contract (OOB drops / `defaultTileId`)
    // carries through unchanged. `cellOrigin` defaults to world
    // `(0, 0)` (P3E in-AY2D scope — non-zero origin lands with the
    // ECS camera integration PR).
    void setTile(ayt::math::FVector2 world, uint32_t tileId) noexcept {
        const TileCoord c = worldToCell(
            world,
            ayt::math::FVector2{0.0f, 0.0f},
            static_cast<float>(tileWidth),
            static_cast<float>(tileHeight));
        setTile(c, tileId);
    }

    [[nodiscard]] uint32_t getTile(ayt::math::FVector2 world) const noexcept {
        const TileCoord c = worldToCell(
            world,
            ayt::math::FVector2{0.0f, 0.0f},
            static_cast<float>(tileWidth),
            static_cast<float>(tileHeight));
        return getTile(c);
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

    // P3D.2 (§13.17): world-space AABB of one cell. Centered on
    // `cellToWorld(c)` (R-3E.5 cell-center) with extent = half the
    // tile dimensions. The naive corner-port `min = cellToWorld(c);
    // max = cellToWorld(c+{1,1})` is OFF BY HALF A CELL because
    // `cellToWorld` returns the cell center, not the corner.
    // Future cross-module PR adds a `cellOrigin` parameter (today
    // hard-coded to `{0, 0}`); see P3E world-coord overloads above.
    [[nodiscard]] ayt::math::FRectangle aabbOfCell(TileCoord c) const noexcept;

    // P3I.1 / §13.20 + §13.PF C6-R1 amendment: three-segment
    // evaluation. The signature is unchanged (§13.PF C8 retained:
    // `TileCoord`, `noexcept`); only the body evolves.
    //
    // 1. Out-of-range cells return `Empty` BEFORE the set lookup.
    //    Required because `getTile` is OOB-safe (returns
    //    `defaultTileId`); without this short-circuit a populated
    //    `blockedTileIds` containing `defaultTileId` would silently
    //    turn OOB cells into `Solid`, breaking the §8.1 contract
    //    that "no flag data == Empty".
    // 2. Empty `blockedTileIds` returns `Empty` fast (preserves
    //    v0.1.17 behavior bit-identically for unpopulated tilemaps
    //    — §13.PF C6 retained clause).
    // 3. Otherwise look up the cell's effective tile id; a hit
    //    returns `Solid`, a miss returns `Empty`. The `None` ban
    //    from §13.PF C6 is retained: this function never returns
    //    `None` (0).
    //
    // `isBlocked` is NOT overridden here — the
    // `ITileCollisionQuery::isBlocked` base default
    // (`flagsAt(c) != Empty`) handles the bool conversion. The
    // `TilemapCollisionQueryAdapter` is therefore a zero-change
    // pass-through (it calls this function via `flagsAt`).
    [[nodiscard]] uint32_t flagsAtRaw(TileCoord c) const noexcept {
        if (!isInRange(c))            return static_cast<uint32_t>(CollisionFlags::Empty);
        if (blockedTileIds.empty())   return static_cast<uint32_t>(CollisionFlags::Empty);
        return blockedTileIds.find(getTile(c)) != blockedTileIds.end()
             ? static_cast<uint32_t>(CollisionFlags::Solid)
             : static_cast<uint32_t>(CollisionFlags::Empty);
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
        // Phase 3C (§14.2): a successful resize is a mutation;
        // the cleared storage means `tiles_resident` resets to 0.
        counters.tiles_resident.store(0u, std::memory_order_relaxed);
        counters.tiles_mutated.fetch_add(1u, std::memory_order_relaxed);
    }

    void clear() noexcept {
        tileIds16.clear();
        tileIds32.clear();
        loadState = TileLoadState::Unloaded;
        // Phase 3C (§14.2): clear is a mutation; storage reset to 0.
        counters.tiles_resident.store(0u, std::memory_order_relaxed);
        counters.tiles_mutated.fetch_add(1u, std::memory_order_relaxed);
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

// Phase 3D (design.md §15): bulk write APIs. The three batch
// operations are **one mutation event each** for `tiles_mutated`
// counting purposes — regardless of how many cells they touch. The
// per-cell semantics for `tiles_resident` follow the same first-
// write lazy-fill pattern as `setTile` (§14.2 R-3D.4).

// Overwrite every cell inside `r` (half-open `[x0, x1)` x `[y0,
// y1)`) with `tileId`. Cells in `r` are clamped to [0, cols) x
// [0, rows). A rect with zero overlap (fully outside the grid)
// is a no-op (returns false, no counter delta). An empty rect
// (`isEmpty(r) == true`) is also a no-op. Successful writes
// (matching in-range overlap of ≥1 cell) return true and bump
// `tiles_mutated` by 1.
//
// Defined in src/AYTilemapBatch.cpp.
[[nodiscard]] bool setTileRange(Tilemap& t, TileRect r, uint32_t tileId) noexcept;

// Fill the entire grid with `tileId`. Logically equivalent to
// `setTileRange(t, gridRect(t), tileId)`; exists as a separate
// symbol so editor paint can express the intent directly. Always
// one mutation event when the grid is sized and non-empty.
//
// Defined in src/AYTilemapBatch.cpp.
void fillTile(Tilemap& t, uint32_t tileId) noexcept;

// Copy cells from `src` starting at `srcOrigin` into `dst` filling
// `dstRect`. dstRect is clamped to dst's grid; the source span is
// clamped to `src` bounds. Returns true iff at least one cell was
// copied. Width-mismatch (`src.mode != dst.mode`) is a contract
// violation that returns false without writing or bumping counters
// (mirrors `loadChunkFromSource` F-18 / §11.3). Successful copies
// (matching mode + ≥1 cell) bump `tiles_mutated` by 1.
//
// Defined in src/AYTilemapBatch.cpp.
[[nodiscard]] bool copyTileRange(Tilemap&       dst,
                                 TileRect       dstRect,
                                 const Tilemap& src,
                                 TileCoord      srcOrigin) noexcept;

// Whole-grid half-open rect. `{0, 0, cols, rows}` for a sized
// grid; `isEmpty` for an empty grid. Read-only helper; never
// bumps counters.
[[nodiscard]] TileRect gridRect(const Tilemap& t) noexcept;

// Phase 3E (§16.3): world-coord batch overload. Translates the
// world AABB to a cell rect via `aabbOverlappingCells`, then
// delegates to the cell-coord `setTileRange` — so the
// counter / lazy-fill / clamp contract is identical. Empty AABB
// or an AABB that does not overlap the grid is a no-op.
// Defined in src/AYTilemapBatch.cpp.
[[nodiscard]] bool setTileRange(Tilemap&             t,
                                ayt::math::FVector2  worldMin,
                                ayt::math::FVector2  worldMax,
                                uint32_t             tileId) noexcept;

} // namespace ayt::ay2d
