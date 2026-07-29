#pragma once
// AYTileCoord.h — 2D integer grid coordinates + storage-width enum
// (Phase 2, header-only).
//
// design.md §3 + §6: World coordinates are integer grid vectors (no
// fixed-point; this is what `IVector2` in AYMath would be, but AY2D
// keeps its own concrete type for:
//
//   - explicit "tile coord" semantics that don't need operator
//     overloads / SIMD padding,
//   - free-form evolution (e.g. adding explicit chunkCoord / layer
//     fields later without polluting AYMath's `IVector2` API),
//   - tighter compile error messages when a function accidentally
//     receives a world position instead of a tile cell.
//
// TileCoord is what code uses to address a single tile in a Tilemap.
// ChunkCoord is a separate type because the chunk grid is anchored
// to `chunkSize` and an off-by-one between the two would corrupt
// chunk arithmetic.
//
// The TileIdPackMode enum lives in this header so any consumer of the
// 2D grid vocabulary sees the same width option. It is intentionally
// 1 byte (`uint8_t`) to honor design.md §6.1 / §10.2 byte-size lock.

#include <cstdint>

namespace ayt::ay2d {

struct TileCoord {
    int32_t x = 0;
    int32_t y = 0;
};

struct ChunkCoord {
    int32_t x = 0;
    int32_t y = 0;
};

// Tile-id storage width. Narrow16 is the default (65 535 distinct
// tiles). Wide32 is reserved for the rare case where the title
// legitimately needs more than 65 k distinct tile types — converters
// must justify Wide32 in a code-review comment (design.md §6.1 / F-1).
enum class TileIdPackMode : uint8_t {
    Narrow16 = 0,
    Wide32   = 1,
};

} // namespace ayt::ay2d

