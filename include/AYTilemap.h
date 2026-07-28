#pragma once
// AYTilemap.h — Phase 0 placeholder (no implementation; design.md §3).
//
// Real definition lands in Phase 2+. See design.md §6 for world limits /
// chunking / streaming and §9 for the .aytilemap L1 format.
//
// Phase 1+ ships the TileIdPackMode enum + the basic Tilemap struct so
// the §6.1 storage-width lock can be compile-checked in unit tests.
// Phase 2 fleshes out the rest (chunked storage, animation table, etc.).
#include <cstdint>

namespace ayt::ay2d {

// design.md §6.1: tile-id storage width. Narrow16 is the default
// (65 535 distinct tiles). Wide32 is reserved for the rare case where
// the title legitimately needs more than 65 k distinct tile types —
// converters must justify Wide32 in a code-review comment. Loaders
// reject a `.aytilemap` whose declared mode does not match the runtime
// mode (F-1 audit patch).
enum class TileIdPackMode : uint8_t {
    Narrow16 = 0,
    Wide32   = 1,
};

struct Tilemap {
    uint32_t tileWidth     = 0;
    uint32_t tileHeight    = 0;
    uint32_t defaultTileId = 0;  // resource metadata (design.md §6.3)
};

} // namespace ayt::ay2d
