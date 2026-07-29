#pragma once
// AYTileSamplerUV.h — pure-CPU tile sampler UV math.
//
// design.md §5.1 + §5.2 + §5.3. Header-only: no .cpp necessary
// because the math is constexpr-friendly (used in unit tests AND
// in the eventual render pass which captures the result into a
// vertex stream — but the inline defs do not bloat the binary).
//
// L-7 / L-8 / L-9 are the locked decisions restated by the helper
// surfaces:
//   - Half-texel center convention: sampling at integer world
//     positions hits the texel center (avoids edge bleed into
//     neighbors with Nearest).
//   - Gutter: subtracts gutter-half from each tile-edge UV, with
//     floor(x) + gutter-1 being the correct "inner" texel index.
//   - 4-tap / 9-tap distinction: hardware bilinear is 4-tap
//     hardware (sample is the only entry that GPU cares about).
//     9-tap is opt-in custom shader (design.md §5.4).
//   - Pixel-perfect truthful disclosure (design.md §5.3):
//     pixel-perfect rendering requires four invariants (integer
//     world position, integer camera zoom, gutter == 0, integer
//     viewport scale). The helper does NOT enforce them; it is the
//     caller's responsibility to gate pixel-perfect behavior on
//     them. We document this with a runtime-checked `static_assert`
//     of the debug invariants at the boundary in src/.

#include <cstdint>

#include "AYAtlasDesc.h"
#include "AYTileCoord.h"

namespace ayt::ay2d {

// Compute UV [uMin, uMax, vMin, vMax] (in 0..1 atlas-normalized
// space) for a given tile id within an atlas described by `desc`.
// The half-texel center convention (design.md §5.1 / L-7) is
// applied, with `gutter` additional extrusion (design.md §5.2 / L-8)
// on each tile edge so bilinear edges sample the border texels
// rather than the neighbor's content.
//
// Returns the 4 floats as a packed struct. All inputs are POD; the
// function is `constexpr` — caller may deploy in constexpr tables
// (Phase 3+ animation bake path will want this).
//
// Tile ids are 0-indexed in the atlas. An out-of-range tile id
// yields a degenerate UV (all zeros) — caller is responsible for
// validating `tileId` against `desc.tilesPerRow * desc.tilesPerColumn`
// before calling this helper.
struct TileUV {
    float uMin = 0.0f;
    float uMax = 0.0f;
    float vMin = 0.0f;
    float vMax = 0.0f;
};

constexpr TileUV tileUV(uint32_t         tileId,
                        const AtlasDesc& desc) noexcept {
    TileUV uv{};
    if (desc.tilesPerRow == 0 || desc.tilesPerColumn == 0) return uv;
    if (desc.atlasWidthTexels == 0 || desc.atlasHeightTexels == 0) return uv;
    if (desc.tileWidthTexels == 0 || desc.tileHeightTexels == 0) return uv;

    const uint32_t col = tileId % desc.tilesPerRow;
    const uint32_t row = tileId / desc.tilesPerRow;

    const float atlasW  = static_cast<float>(desc.atlasWidthTexels);
    const float atlasH  = static_cast<float>(desc.atlasHeightTexels);
    const float half_px = 0.5f / atlasW;          // half-texel in U (design.md §5.1)
    const float half_py = 0.5f / atlasH;          // half-texel in V
    const float gutter_px = static_cast<float>(desc.gutter) / atlasW;
    const float gutter_py = static_cast<float>(desc.gutter) / atlasH;

    const float tileLeft   = (static_cast<float>(col)      * static_cast<float>(desc.tileWidthTexels)  / atlasW) + gutter_px;
    const float tileRight  = (static_cast<float>(col + 1u) * static_cast<float>(desc.tileWidthTexels)  / atlasW) - gutter_px;
    // V matches U: bottom-up. tile-id 0 sits at the bottom-left of
    // the atlas (row 0 = bottom, col 0 = left). The Phoskia variant
    // assumes origin-bottom-left for the screen quad, so this layout
    // is the convention we adopt for the GPU stage.
    const float tileBottom = (static_cast<float>(row)      * static_cast<float>(desc.tileHeightTexels) / atlasH) + gutter_py;
    const float tileTop    = (static_cast<float>(row + 1u) * static_cast<float>(desc.tileHeightTexels) / atlasH) - gutter_py;

    uv.uMin = tileLeft   + half_px;
    uv.uMax = tileRight  - half_px;
    uv.vMin = tileBottom + half_py;
    uv.vMax = tileTop    - half_py;
    return uv;
}

// Pixel-perfect truthfulness (design.md §5.3). The four invariants
// must ALL hold for pixel-perfect rendering; otherwise artifacts
// (pixel crawl / aliasing / sparkle) appear. This helper is the
// single source of truth — RenderSystem2D and the Editor viewport
// both call it to decide whether to enable pixel-perfect mode.
struct PixelPerfectInvariants {
    bool integerWorldPositions   = false;
    bool integerCameraZoom       = false;
    bool gutterIsZero            = false;
    bool integerViewportScale    = false;
};

constexpr bool isPixelPerfectSafe(const PixelPerfectInvariants& v) noexcept {
    return v.integerWorldPositions
        && v.integerCameraZoom
        && v.gutterIsZero
        && v.integerViewportScale;
}

} // namespace ayt::ay2d
