#pragma once
// AYSpriteSheet.h — P3H.1 in-AY2D SpriteSheet thin wrapper.
//
// design.md §13.18 (P3H.1 changelog) + §3 / §5.5:
// `SpriteSheet` is the in-AY2D value type that pairs an
// `AtlasDesc` (the L2 metadata, design.md §5.5) with a
// `texturePath` string. It is the consumer-facing "atlas +
// material reference" that the future cross-module PR
// (RenderPassSlot::Forward2DOpaque + DrawItem::payload,
// §4.2.1) consumes.
//
// Per §13.PF C4 reshape: the type is a thin wrapper, NOT
// a re-implementation of `AtlasDesc` / `AYTileSamplerUV`.
// All UV math delegates to the existing `tileUV()`
// helper (gutter + half-texel per §5.1 / §5.2 / L-7 / L-8);
// SpriteSheet adds the texture path + a `uvRect(tileId)`
// accessor that adapts the `TileUV` 4-float struct into
// the engine's `FRectangle` shape that `Sprite::sourceRect`
// already uses.
//
// L-3 lock: no `bgfx::TextureHandle` here — only an opaque
// path string. The future cross-module PR resolves the
// path into a real handle.
//
// Header-only: no .cpp. Adding a .cpp is a future-only move
// (e.g. SIMD bulk UV bake for a sprite-bake tool).

#include <cstdint>
#include <string>

#include "aymath/MathTypes.h"

#include "AYAtlasDesc.h"
#include "AYTileSamplerUV.h"

namespace ayt::ay2d {

// In-AY2D sprite sheet: an atlas + a path. The path is the
// `.ayatlas`-or-equivalent on-disk artifact; today it is a
// string the cross-module PR will resolve via AYResource.
// Header-only struct; trivially copyable (std::string is the
// only non-POD member; the struct is `std::is_trivially_copyable`
// when the underlying `std::string` impl is — see the
// companion test for the static check).
struct SpriteSheet {
    // The atlas metadata. POD; mirrors `AtlasDesc` exactly so
    // callers can read either interchangeably.
    AtlasDesc atlas;

    // The texture path (`.ayatlas` equivalent). Empty string
    // means "no path set"; the cross-module PR treats that as
    // a load failure (F-18 §11.3 contract).
    std::string texturePath;

    // Per-tile UV as an `FRectangle` (engine convention used
    // by `Sprite::sourceRectU0/V0/U1/V1`). Delegates to
    // `AYTileSamplerUV::tileUV` so gutter + half-texel
    // (§5.1 / §5.2) are applied identically to a Tilemap /
    // Sprite query. Out-of-range tile id yields a degenerate
    // rect (all zeros) — caller is responsible for validating
    // `tileId` against `atlas.tilesPerRow * atlas.tilesPerColumn`.
    [[nodiscard]] ayt::math::FRectangle uvRect(uint32_t tileId) const noexcept {
        const TileUV uv = tileUV(tileId, atlas);
        return ayt::math::FRectangle{uv.uMin, uv.vMin, uv.uMax, uv.vMax};
    }
};

} // namespace ayt::ay2d