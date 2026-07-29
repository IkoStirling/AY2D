#pragma once
// AYSpriteDrawCmd.h — Phase 3F sprite scene data carrier.
//
// design.md §17.1 + R-3F.1: AY2D ships its **own** POD record
// for the sprite scene built by `buildSpriteScene(...)`. The
// struct is **not** a `DrawItem` wrapper — that translation is
// the cross-module PR to AYRenderer's `DrawItem::payload`
// (§4.2.1 ownership lock). Today the helper builds a
// `std::vector<SpriteDrawCmd>` that the future `RenderSystem2D`
// ECS cross-module PR will translate into AYRenderer's
// `DrawItem::payload` shape.
//
// Pure data carrier. No virtual methods. No allocation.
// Stdlib-only headers + in-AY2D AYMath + AYSprite. No bgfx,
// no bx, no AYRenderer.
//
// Layout (design.md §17.3):
//   * 1 u32 (4B)            packedSortKey
//   * pad (0B; Float3x3 alignment handled by struct align)
//   * 9 floats (36B)        worldMatrix  (Float3x3 row-major flat)
//   * 2 FVector2 (16B)      sourceRectMin + sourceRectMax
//   * 1 FVector4 (16B)      colorRGBA
//   * 1 u8 + 3B pad         flip (SpriteFlip bitfield, padded
//                            to next 4B for the u32 below)
//   * 1 u32 (4B)            layerMaskSnapshot
//   --------------------
//   ~ 88 B per cmd, all-static. std::vector<SpriteDrawCmd>
//   reuses the same allocator as std::vector<Sprite>.

#include <cstdint>

#include "aymath/MathTypes.h"

#include "AYSprite.h"

namespace ayt::ay2d {

struct SpriteDrawCmd {
    uint32_t packedSortKey = 0u;  // (layer << 24) | (sortingKey & 0x00FFFFFF)
    ayt::math::Float3x3 worldMatrix = ayt::math::Float3x3::identity();
    ayt::math::FVector2 sourceRectMin{0.0f, 0.0f};
    ayt::math::FVector2 sourceRectMax{1.0f, 1.0f};
    ayt::math::FVector4 colorRGBA   {1.0f, 1.0f, 1.0f, 1.0f};
    uint8_t  flip = 0u;            // SpriteFlip bitfield (cast
                                   //   through SpriteFlip when read)
    uint32_t layerMaskSnapshot = 0u;
};

} // namespace ayt::ay2d
