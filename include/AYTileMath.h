#pragma once
// AYTileMath.h — Phase 3E world↔cell coordinate math layer.
//
// design.md §16: pure-math helpers that translate between world
// (float) and cell (integer) coordinates. The layer is **pure
// math** — every OOB guard happens downstream at the
// `setTile` / `getTile` boundary. Helpers do NOT cache any state
// on `Tilemap`; consumers compose them as needed.
//
// World-space framing:
//   * `cellOrigin` is the world position of cell `(0, 0)`'s
//     corner. Default in the subsystem = `(0, 0)` (see design
//     note §16.3; future camera-derived origin lands in
//     cross-module ECS PR).
//   * `cellSizeW` / `cellSizeH` are floats; real tilemaps hold
//     them as `uint32_t tileWidth` / `tileHeight` — the cast is
//     the caller's responsibility (matches the rest of §16).
//   * `cellToWorld` returns the **cell center**, not the corner,
//     so editor picks anchor on the visible square (R-3E.5).

#include <cmath>
#include <cstdint>

#include "AYMath/MathTypes.h"

#include "AYTileCoord.h"
#include "AYTileRect.h"

namespace ayt::ay2d {

// Convert a world-space point to a cell coord. Floors both axes
// toward `-infinity` regardless of sign (R-3E.1). Does NOT clamp
// to grid bounds — callers route through `setTile` / `getTile`
// which inherit the existing OOB-drop + `defaultTileId` contract
// (design.md §6.3).
//
// `cellSizeW == 0` or `cellSizeH == 0` returns `TileCoord{0, 0}`
// (R-3E.6) — this is a defensive guard; real tilemaps always
// have non-zero cell size.
[[nodiscard]] inline TileCoord worldToCell(
    ayt::math::FVector2 world,
    ayt::math::FVector2 cellOrigin,
    float               cellSizeW,
    float               cellSizeH) noexcept {
    if (cellSizeW <= 0.0f || cellSizeH <= 0.0f) return TileCoord{};
    // floorf is the standard "round toward -infinity" primitive
    // in `<cmath>`. We keep the conversion explicit so the floor
    // is visible at the helper boundary (no operator overloading
    // hiding it). Negative results are allowed here; downstream
    // `setTile` / `getTile` drop them.
    const float lx = (world.x - cellOrigin.x) / cellSizeW;
    const float ly = (world.y - cellOrigin.y) / cellSizeH;
    const int32_t cx = static_cast<int32_t>(floorf(lx));
    const int32_t cy = static_cast<int32_t>(floorf(ly));
    return TileCoord{cx, cy};
}

// Convert a cell coord to the world-space point at its **center**
// (R-3E.5). The center is inside the cell interior, so a
// `worldToCell(cellToWorld(c))` round-trip stays at `c`. (Editor
// picks on cell edges exhibit the typical half-pixel variation
// that matches Unity / SDL_RectF convention.)
//
// `cellSizeW == 0` returns `cellOrigin` (R-3E.6).
[[nodiscard]] inline ayt::math::FVector2 cellToWorld(
    TileCoord           cell,
    ayt::math::FVector2 cellOrigin,
    float               cellSizeW,
    float               cellSizeH) noexcept {
    if (cellSizeW <= 0.0f || cellSizeH <= 0.0f) return cellOrigin;
    // Center of cell (cx, cy) sits at
    //   cellOrigin + (cx + 0.5) * cellSize
    // The `+ 0.5f` is the half-cell offset; together with `floor`
    // in `worldToCell` it gives the editor pick a deterministic
    // anchor.
    const float wx = cellOrigin.x
        + (static_cast<float>(cell.x) + 0.5f) * cellSizeW;
    const float wy = cellOrigin.y
        + (static_cast<float>(cell.y) + 0.5f) * cellSizeH;
    return ayt::math::FVector2{wx, wy};
}

// Convert a world-space AABB to the half-open `[x0, x1) x
// [y0, y1)` cell rect that touches it. Returns `TileRect{}` if
// the AABB is degenerate (`worldMax <= worldMin`) or if the AABB
// does not overlap the cell grid at all (R-3E.3 / R-3E.4).
//
// The pure-math variant leaves the cell extent unclamped; the
// caller clamps with `clampToGrid(rect, cols, rows)` when a
// `Tilemap` is involved (already ships from Phase 3D).
[[nodiscard]] inline TileRect aabbOverlappingCells(
    ayt::math::FVector2 worldMin,
    ayt::math::FVector2 worldMax,
    ayt::math::FVector2 cellOrigin,
    float               cellSizeW,
    float               cellSizeH) noexcept {
    if (cellSizeW <= 0.0f || cellSizeH <= 0.0f) return TileRect{};
    if (worldMax.x <= worldMin.x)             return TileRect{};
    if (worldMax.y <= worldMin.y)             return TileRect{};

    // Floor worldMin's axes (lowest cell the AABB touches) and
    // ceil worldMax's axes (one past the highest cell).
    const float lX0 = (worldMin.x - cellOrigin.x) / cellSizeW;
    const float lY0 = (worldMin.y - cellOrigin.y) / cellSizeH;
    const float lX1 = (worldMax.x - cellOrigin.x) / cellSizeW;
    const float lY1 = (worldMax.y - cellOrigin.y) / cellSizeH;

    const int32_t x0 = static_cast<int32_t>(floorf(lX0));
    const int32_t y0 = static_cast<int32_t>(floorf(lY0));
    // ceilf-style upper bound: `floor(x)` for the strict upper
    // edge. We want the cell coord one past the last cell the
    // AABB touches, i.e. ceil(lX1) with the half-open
    // `[x0, x1)` interpretation.
    const int32_t x1 = static_cast<int32_t>(ceilf(lX1));
    const int32_t y1 = static_cast<int32_t>(ceilf(lY1));

    TileRect r{x0, y0, x1, y1};
    if (isEmpty(r)) return TileRect{};
    return r;
}

// Read-side guard. "Is this cell inside the grid?" — same as
// `Tilemap::isInRange(cell)` but free-standing so it can be
// called without a `Tilemap` instance (Phase 5 collision
// probes land here; Phase 6 perf pre-cull uses it to skip
// cells before hitting `getTile`).
[[nodiscard]] constexpr bool isCellInWorldBounds(
    TileCoord cell,
    uint32_t  cols,
    uint32_t  rows) noexcept {
    if (cell.x < 0 || cell.y < 0)              return false;
    return static_cast<uint32_t>(cell.x) < cols
        && static_cast<uint32_t>(cell.y) < rows;
}

} // namespace ayt::ay2d
