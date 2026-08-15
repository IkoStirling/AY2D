#pragma once
// AY2D/AtlasDesc.h — atlas metadata contract (Phase 2).
//
// design.md §5.5: this struct is the L2-side descriptor that goes
// alongside the uploaded atlas texture. Sized identically to the
// Phase 0 stub in design.md §5.5; extended with `defaultTileSize`
// (so a .aytilemap header can override per-tile width/height if
// needed; today the per-atlas tileWidth/tileHeight stays canonical).
//
// Header-only: no .cpp for this struct. The Shader Variant
// (`tilemap_9tap` / Bilinear / Nearest) is selected from `filter`
// at material load time per design.md §5.5.

#include <cstdint>

namespace ayt::ay2d {

// TileFilter — design.md §5.5. Default = Bilinear.
enum class TileFilter : uint8_t {
    Nearest      = 0,
    Bilinear     = 1,
    Bilinear9Tap = 2,  // opt-in; requires shader variant "tilemap_9tap"
};

// TileWrap — design.md §5.5 (F-15). Per-axis wrap for atlas sampling.
// Clamp is the safe default; Repeat is required for scroll / parallax
// backgrounds; Mirror is reserved for shader-side effects.
enum class TileWrap : uint8_t {
    Clamp  = 0,
    Repeat = 1,
    Mirror = 2,
};

// AtlasDesc — atlas metadata that drives the tile sampler.
//
// The struct is laid out POD so it can be memcpy'd in and out of a
// .ayatlas L1 header without conversion cost. Field order matches
// the disk format described in design.md §9.1 — adding new fields
// requires an Extension four-cc chunk, never reshuffle.
struct AtlasDesc {
    uint32_t  atlasWidthTexels   = 0;
    uint32_t  atlasHeightTexels  = 0;
    uint32_t  tileWidthTexels    = 0;
    uint32_t  tileHeightTexels   = 0;
    uint32_t  tilesPerRow        = 0;  // derived: atlasWidthTexels / tileWidthTexels
    uint32_t  tilesPerColumn     = 0;  // derived: atlasHeightTexels / tileHeightTexels
    uint32_t  gutter             = 1;  // design.md §5.5 L-8 default; 0 saves atlas bytes when Nearest-only
    TileFilter filter           = TileFilter::Bilinear;
    TileWrap   wrapU            = TileWrap::Clamp;
    TileWrap   wrapV            = TileWrap::Clamp;
};

// Validate an AtlasDesc. Returns true iff `tilesPerRow * tilesPerColumn
// > 0`, tile size divides the atlas size cleanly (so that the per-tile
// UV math is unambiguous), and gutter is small enough to fit inside a
// single tile (otherwise per-tile edges would steal texels from the
// neighbors).
//
// Validation rule (design.md §5.2): `gutter < min(tileWidth, tileHeight) / 2`.
[[nodiscard]] constexpr bool isValidAtlasDesc(const AtlasDesc& d) noexcept {
    if (d.atlasWidthTexels == 0 || d.atlasHeightTexels == 0) return false;
    if (d.tileWidthTexels  == 0 || d.tileHeightTexels  == 0) return false;
    if (d.tilesPerRow == 0 || d.tilesPerColumn == 0)        return false;
    if (d.atlasWidthTexels  != d.tileWidthTexels  * d.tilesPerRow)   return false;
    if (d.atlasHeightTexels != d.tileHeightTexels * d.tilesPerColumn) return false;
    const uint32_t minTile = d.tileWidthTexels < d.tileHeightTexels
                            ? d.tileWidthTexels
                            : d.tileHeightTexels;
    return d.gutter < minTile / 2;
}

} // namespace ayt::ay2d
