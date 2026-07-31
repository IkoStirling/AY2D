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
// Layout (design.md §17.3, corrected by §13.26 / P3J.2):
//   The total struct size is 112 B with alignof 16 (driven by
//   FVector2 / FVector4 SIMD alignment on MSVC). Earlier P3F
//   estimates assumed 88 B / align 4; that was wrong.
//   Field-by-field:
//     offset   0   packedSortKey          u32  (4B)
//     offset   4   worldMatrix            Float3x3 (36B, align 4)
//     offset  40   [pad to 16-align]      8B
//     offset  48   sourceRectMin          FVector2 (16B, align 16)
//     offset  64   sourceRectMax          FVector2 (16B, align 16)
//     offset  80   colorRGBA              FVector4 (16B, align 16)
//     offset  96   flip                   u8 (1B)
//     offset  97   [pad to 4-align]       3B
//     offset 100   layerMaskSnapshot      u32 (4B)
//     offset 104   [trail pad to 16]      8B
//     total    112 B
//   std::vector<SpriteDrawCmd> reuses the same allocator as
//   std::vector<Sprite>.

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
