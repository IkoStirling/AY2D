#pragma once
// AY2D/Sprite.h — Phase 3B real impl.
//
// design.md §3 + §7: Sprite is a single-image or atlas sub-rect
// draw intent. The struct holds:
//   * worldMatrix    — Float3x3 affine (translation + scale +
//                      rotation; 2D scenes do not need perspective).
//   * sourceRect     — 4 floats in atlas UV space (u0, v0, u1, v1).
//   * color          — RGBA tint (4 floats, default = white).
//   * flip           — 2-bit flag (horizontal, vertical).
//   * layer          — 0..31 (design.md §7.4).
//   * sortingKey     — 0..0x00FFFFFF (§7.4).
//
// Sprite MUST NOT carry a bgfx handle (L-3 / §3.1). The render path
// resolves the material (or atlas path) elsewhere; the AYRenderer
// ECS system will look it up. This struct is a data carrier only.
//
// Animation timing is NOT on Sprite (§3 row "Does NOT own"). The
// per-tile animation table lives on Tilemap; Sprite flips in place
// via `flip` bits if the consumer wants sprite-sheet animation
// (sequence per AtlasDesc tile row), but the timing source is the
// shared `ayt::time::Clock::gameNow` — same as Tilemap anim.

#include <cstdint>

#include "AYMath/MathTypes.h"

namespace ayt::ay2d {

// design.md §7.3: 2-bit flip encoding.
enum class SpriteFlip : uint8_t {
    None       = 0,
    Horizontal = 1 << 0,
    Vertical   = 1 << 1,
    Both       = Horizontal | Vertical,
};

inline constexpr SpriteFlip operator|(SpriteFlip a, SpriteFlip b) noexcept {
    return static_cast<SpriteFlip>(
        static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr SpriteFlip operator&(SpriteFlip a, SpriteFlip b) noexcept {
    return static_cast<SpriteFlip>(
        static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline constexpr SpriteFlip& operator|=(SpriteFlip& a, SpriteFlip b) noexcept {
    a = a | b;
    return a;
}
inline constexpr SpriteFlip& operator&=(SpriteFlip& a, SpriteFlip b) noexcept {
    a = a & b;
    return a;
}
inline constexpr bool hasFlip(SpriteFlip f, SpriteFlip bit) noexcept {
    return static_cast<uint8_t>(f & bit) != 0;
}

struct Sprite {
    // Affine 2D world transform (translation + scale + rotation).
    // Default = identity. No perspective column (2D scene; design.md §5.3).
    ayt::math::Float3x3 worldMatrix = ayt::math::Float3x3::identity();

    // Atlas sub-rect in UV space (u0, v0, u1, v1). Default = full atlas.
    // The bottom-up convention from design.md §5.1: v0 is the bottom row.
    float sourceRectU0 = 0.0f;
    float sourceRectV0 = 0.0f;
    float sourceRectU1 = 1.0f;
    float sourceRectV1 = 1.0f;

    // RGBA tint. Default = white opaque (1, 1, 1, 1).
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
    float colorA = 1.0f;

    // 2-bit flip (design.md §7.3). Per-instance; no extra vertex buffer.
    SpriteFlip flip = SpriteFlip::None;

    // Layer + sorting key (design.md §7.4).
    uint8_t  layer      = 0;   // 0..31
    uint32_t sortingKey = 0;   // 0..0x00FFFFFF

    // design.md §7.4: (layer << 24) | (sortingKey & 0x00FFFFFF).
    // Layer is masked to 5 bits (0..31); sortingKey to 24 bits.
    [[nodiscard]] constexpr uint32_t packedSortKey() const noexcept {
        return (static_cast<uint32_t>(layer & 0x1Fu) << 24)
             | (sortingKey & 0x00FFFFFFu);
    }
};

} // namespace ayt::ay2d