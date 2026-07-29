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

} // namespace ayt::ay2d
