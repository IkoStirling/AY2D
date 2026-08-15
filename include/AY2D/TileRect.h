#pragma once
// AY2D/TileRect.h — Phase 3D batch tile-fill rectangle type.
//
// design.md §15.3 + §15.7 R-3D.1: half-open `[min, max)` rect.
// A `TileRect{ 1, 2, 3, 4 }` covers columns {1, 2} and rows
// {2, 3} — three cells, NOT four. The 1×1 rect at (col, row) is
// expressed as `{col, row, col+1, row+1}`.
//
// Uses `int32_t` for coordinates so out-of-range inputs at the
// negative side can be detected / clamped in `setTileRange` /
// `copyTileRange` callers without overflow. Clamping to the grid
// is `clampToGrid` below.
//
// Lifetime: POD-equivalent. No allocation. Header-only so the
// batch helpers in `src/AYTilemapBatch.cpp` and the consumers
// (`include/AY2D/Tilemap.h`) share the same type without including
// each other's TU.
//
// New public API surface in Phase 3D; no cross-module PR needed
// (in-AY2D scope per design.md §15 + §4.2.1).

#include <cstdint>

namespace ayt::ay2d {

struct TileRect {
    int32_t x0 = 0;
    int32_t y0 = 0;
    int32_t x1 = 0;
    int32_t y1 = 0;
};

// Free helpers (inline constexpr as far as possible). All take
// `TileRect` by value — the type is 16 bytes and the helpers
// stay branch-predictable.

[[nodiscard]] constexpr bool isEmpty(TileRect r) noexcept {
    return r.x1 <= r.x0 || r.y1 <= r.y0;
}

[[nodiscard]] constexpr int64_t area(TileRect r) noexcept {
    return isEmpty(r)
        ? int64_t{0}
        : static_cast<int64_t>(r.x1 - r.x0)
        * static_cast<int64_t>(r.y1 - r.y0);
}

// Clamp `r` so `x0, y0` are non-negative and `x1, y1` are within
// [0, cols] x [0, rows] (note: x1 / y1 are EXCLUSIVE bounds so
// the legal max is `cols` / `rows`, not `cols-1` / `rows-1`).
// Result preserves half-open semantics.
//
// If the rect lies entirely outside the grid (after clamping)
// the result is `isEmpty` true.
//
// Used by `setTileRange` / `copyTileRange` to translate caller-
// supplied "wide" rectangles to a bounded write span.
[[nodiscard]] inline TileRect clampToGrid(TileRect r,
                                           uint32_t cols,
                                           uint32_t rows) noexcept {
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (static_cast<uint32_t>(r.x1) > cols) r.x1 = static_cast<int32_t>(cols);
    if (static_cast<uint32_t>(r.y1) > rows) r.y1 = static_cast<int32_t>(rows);
    return r;
}

} // namespace ayt::ay2d
