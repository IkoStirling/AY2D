#pragma once
// AYSprite.h — Phase 0 placeholder (no implementation; design.md §3).
//
// Real definition lands in Phase 2+. Sprite is sampler-agnostic; the
// pixel-perfect-vs-smooth decision lives on the AYResource L2 material
// (design.md §3 / §3.1 lock: "Sprite MUST NOT carry a bgfx::TextureHandle").
#include <cstdint>
namespace ayt::ay2d {
struct Sprite {
    uint8_t flip     = 0;   // bit 0 = horizontal, bit 1 = vertical
    uint8_t layer    = 0;   // 0..31 (design.md §7.4)
    uint32_t sortingKey = 0;
};
} // namespace ayt::ay2d
