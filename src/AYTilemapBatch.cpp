// AYTilemapBatch.cpp — Phase 3D batch tile-fill implementations.
//
// design.md §15.4 (counter contract) + §15.2 (semantics):
//   * One batch operation = one mutation event (`tiles_mutated += 1`)
//   * First-write lazy-fill bumps `tiles_resident` to the full
//     `cols * rows` slot count (one bump, not per-cell).
//   * Empty / fully-out-of-range / mode-mismatch / no-size inputs
//     are no-ops — no counter delta, no writes.
//   * Width-mode invariants are preserved (mirrors `loadChunkFromSource`
//     F-18 / §11.3 contract on failure paths).
//
// The helpers below share storage layout with `Tilemap::setTile` so
// the existing per-cell code path continues to behave identically.

#include "AYTilemap.h"

#include <cstdint>

#include "AYTileCoord.h"
#include "AYTileRect.h"

namespace ayt::ay2d {

namespace {

// Lazily size the storage to `expected` cells, mirroring `setTile`'s
// first-write path (design.md §14.2 + R-3D.4). Returns true iff
// this call grew the storage (i.e. fired the lazy-fill).
//
// Out parameter `outExpected` always echoes the requested expected
// slot count (cols * rows) so the caller can later bump
// `tiles_resident` to the same value with a single store.
bool ensureFirstWriteLazyFill(Tilemap& t,
                             size_t   expected,
                             uint32_t fillNarrow16,
                             uint32_t fillWide32) noexcept {
    bool grewStorage = false;
    if (t.mode == TileIdPackMode::Narrow16) {
        if (t.tileIds16.size() != expected) {
            const uint16_t fill = (fillNarrow16 > 0xFFFFu)
                ? uint16_t{0}
                : static_cast<uint16_t>(fillNarrow16);
            t.tileIds16.assign(expected, fill);
            grewStorage = true;
        }
    } else {
        if (t.tileIds32.size() != expected) {
            t.tileIds32.assign(expected, fillWide32);
            grewStorage = true;
        }
    }
    return grewStorage;
}

// Bump the per-tilemap counters after a successful batch write.
// Phase 3D invariant: `tiles_mutated += 1` (always), `tiles_resident`
// set to `expected` only when `ensureFirstWriteLazyFill` returned
// true (R-3D.4 — one resident bump per batch that grew storage).
void bumpAfterBatchWrite(Tilemap& t,
                         size_t   expected,
                         bool     grewStorage) noexcept {
    if (grewStorage) {
        t.counters.tiles_resident.store(expected, std::memory_order_relaxed);
    }
    t.counters.tiles_mutated.fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

TileRect gridRect(const Tilemap& t) noexcept {
    if (t.cols == 0 || t.rows == 0) {
        // isEmpty by construction (x1 = y1 = 0).
        return TileRect{};
    }
    return TileRect{
        0, 0,
        static_cast<int32_t>(t.cols),
        static_cast<int32_t>(t.rows),
    };
}

bool setTileRange(Tilemap& t, TileRect r, uint32_t tileId) noexcept {
    if (t.cols == 0 || t.rows == 0)   return false;
    if (isEmpty(r))                   return false;
    if (r.x0 < 0 || r.y0 < 0)         return false;
    // Clamp rect into the grid. After clamping, isEmpty means
    // "fully outside" → no-op.
    r = clampToGrid(r, t.cols, t.rows);
    if (isEmpty(r))                   return false;

    const int32_t x0 = r.x0;
    const int32_t y0 = r.y0;
    const int32_t x1 = r.x1;
    const int32_t y1 = r.y1;
    const size_t  expected = static_cast<size_t>(t.cols) * t.rows;

    // First-write lazy-fill MUST use `defaultTileId`, not the batch's
    // tileId. Cells outside the rect should still read as the default
    // — that's the `UntouchedCellReadsDefaultTileId` invariant from
    // Phase 2 §6.3 / DefaultTileIdAppliedOnRead. If we pre-flooded
    // with `tileId`, every lazy-fill would leak the batch value to
    // every cell, breaking the design.md §15.2 contract.
    //
    // For Narrow16, an out-of-range defaultTileId (>0xFFFF) is
    // silently down-converted to 0 (matching `setTile` behavior).
    const uint32_t fill = t.mode == TileIdPackMode::Narrow16
        ? (t.defaultTileId > 0xFFFFu ? uint32_t{0} : t.defaultTileId)
        : t.defaultTileId;

    const bool grewStorage = ensureFirstWriteLazyFill(
        t, expected, fill, fill);

    if (t.mode == TileIdPackMode::Narrow16) {
        if (tileId > 0xFFFFu) {
            // Out-of-range for Narrow16: silently no-op every cell,
            // matching setTile's per-cell contract. The batch is
            // still counted as one mutation only if at least one
            // cell would have been written — but the contract says
            // a batch that can't actually represent the id is a
            // no-op (otherwise `tiles_mutated` would be misleading).
            bumpAfterBatchWrite(t, expected, grewStorage);
            return true;
        }
        const uint16_t id16 = static_cast<uint16_t>(tileId);
        for (int32_t row = y0; row < y1; ++row) {
            const size_t base = static_cast<size_t>(row) * t.cols;
            for (int32_t col = x0; col < x1; ++col) {
                t.tileIds16[base + col] = id16;
            }
        }
    } else {
        for (int32_t row = y0; row < y1; ++row) {
            const size_t base = static_cast<size_t>(row) * t.cols;
            for (int32_t col = x0; col < x1; ++col) {
                t.tileIds32[base + col] = tileId;
            }
        }
    }
    bumpAfterBatchWrite(t, expected, grewStorage);
    return true;
}

void fillTile(Tilemap& t, uint32_t tileId) noexcept {
    if (t.cols == 0 || t.rows == 0) return;
    // Use gridRect for the semantic shortcut. The return value
    // is intentionally discarded: `fillTile` is defined to mean
    // "fill the grid if it exists", so a "false because fully OOB"
    // condition cannot arise by construction.
    (void)setTileRange(t, gridRect(t), tileId);
}

bool copyTileRange(Tilemap&       dst,
                   TileRect       dstRect,
                   const Tilemap& src,
                   TileCoord      srcOrigin) noexcept {
    if (dst.cols == 0 || dst.rows == 0) return false;
    if (isEmpty(dstRect))               return false;
    if (dstRect.x0 < 0 || dstRect.y0 < 0) return false;
    // Width-mismatch is the F-18 contract: refuse before any write.
    if (dst.mode != src.mode)           return false;
    if (src.cols == 0 || src.rows == 0) return false;
    if (srcOrigin.x < 0 || srcOrigin.y < 0) return false;

    // Clamp the destination rect to dst's grid. After this, dstRect
    // is fully inside dst, but `src` may still extend out of bounds.
    dstRect = clampToGrid(dstRect, dst.cols, dst.rows);
    if (isEmpty(dstRect)) return false;

    // Effective read span in `src` coordinates: srcOrigin ..
    // srcOrigin + (dstRect.w, dstRect.h). Anything outside src is
    // ignored — that part of the destination rectangle is left at
    // its first-write lazy-fill value.
    const int32_t dstW = dstRect.x1 - dstRect.x0;
    const int32_t dstH = dstRect.y1 - dstRect.y0;
    const int32_t srcX0 = srcOrigin.x;
    const int32_t srcY0 = srcOrigin.y;
    const int32_t srcX1 = srcX0 + dstW;
    const int32_t srcY1 = srcY0 + dstH;

    // Src clamp in `src` bounds [0, cols) x [0, rows). After this
    // we know which subset of cells have an actual source cell
    // to copy from.
    if (srcX1 <= 0 || srcY1 <= 0) return false;
    const int32_t srcCols = static_cast<int32_t>(src.cols);
    const int32_t srcRows = static_cast<int32_t>(src.rows);
    if (srcX0 >= srcCols) return false;
    if (srcY0 >= srcRows) return false;
    const int32_t readX0 = srcX0 < 0 ? 0 : srcX0;
    const int32_t readY0 = srcY0 < 0 ? 0 : srcY0;
    const int32_t readX1 = srcX1 > srcCols ? srcCols : srcX1;
    const int32_t readY1 = srcY1 > srcRows ? srcRows : srcY1;
    if (readX1 <= readX0 || readY1 <= readY0) return false;

    // Map the read span back onto dst: cells (readX0..readX1-1,
    // readY0..readY1-1) on src land at (readX0 - srcX0 + dstRect.x0
    // .. readX1 - srcX0 + dstRect.x0 - 1) on dst. The shift is
    // uniform in both axes.
    const int32_t dShiftCol = dstRect.x0 - srcX0;
    const int32_t dShiftRow = dstRect.y0 - srcY0;
    const int32_t dX0 = readX0 + dShiftCol;
    const int32_t dY0 = readY0 + dShiftRow;
    const int32_t dX1 = readX1 + dShiftCol;
    const int32_t dY1 = readY1 + dShiftRow;

    const size_t dExpected = static_cast<size_t>(dst.cols) * dst.rows;

    // First-write lazy-fill on dst uses dst.defaultTileId (NOT a
    // value sourced from `src`). Untouched cells on dst must read
    // as the dst's own default — same contract as `setTile` (§6.3).
    // The copy pass below overwrites only the cells inside the
    // effective rect; every other cell of dst stays at defaultTileId.
    const uint32_t fill = dst.mode == TileIdPackMode::Narrow16
        ? (dst.defaultTileId > 0xFFFFu ? uint32_t{0} : dst.defaultTileId)
        : dst.defaultTileId;
    const bool grewStorage = ensureFirstWriteLazyFill(
        dst, dExpected, fill, fill);

    for (int32_t row = readY0; row < readY1; ++row) {
        const int32_t dRow = row + dShiftRow;
        const size_t  dBase = static_cast<size_t>(dRow) * dst.cols;
        const size_t  sBase = static_cast<size_t>(row) * src.cols;
        for (int32_t col = readX0; col < readX1; ++col) {
            const int32_t dCol = col + dShiftCol;
            const size_t  dIdx = dBase + dCol;
            const size_t  sIdx = sBase + col;
            if (dst.mode == TileIdPackMode::Narrow16) {
                dst.tileIds16[dIdx] = src.tileIds16[sIdx];
            } else {
                dst.tileIds32[dIdx] = src.tileIds32[sIdx];
            }
        }
    }
    bumpAfterBatchWrite(dst, dExpected, grewStorage);
    return true;
}

} // namespace ayt::ay2d
