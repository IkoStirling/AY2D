#pragma once
// AYTilemap.h — Phase 0 placeholder (no implementation; design.md §3).
//
// Real definition lands in Phase 2+. See design.md §6 for world limits /
// chunking / streaming and §9 for the .aytilemap L1 format.
#include <cstdint>
namespace ayt::ay2d {
struct Tilemap {
    uint32_t tileWidth     = 0;
    uint32_t tileHeight    = 0;
    uint32_t defaultTileId = 0;  // resource metadata (design.md §6.3)
};
} // namespace ayt::ay2d
