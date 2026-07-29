// AYSpriteCulling.cpp — Phase 3F sprite scene builder.
//
// design.md §17.3 (algorithm) + §17.2 (locks):
//   * Three-step cull order: AABB → layer-mask → stable_sort
//     → emit. AABB is the cheapest and drops the most; layer-
//     mask is branch-predictable and runs next; sort runs on
//     the surviving set so it never wastes budget on culled
//     entries.
//   * No AYRenderer types in this TU. The future ECS system
//     cross-module PR to AYRenderer does the `SpriteDrawCmd`
//     → `DrawItem::payload` translation; today's helper stops
//     at the in-AY2D data carrier.
//   * `SpriteDrawCmd` is a pure POD; the helper copies fields
//     through, no allocation per cmd (caller reserves
//     `sprites.size()` on `out`).
//   * `std::stable_sort` (R-3F.6 / F-2): two sprites at the
//     same `packedSortKey` keep their input order.

#include "AYOrthographicCamera.h"
#include "AYSprite.h"
#include "AYSpriteDrawCmd.h"
#include "AYWorldAabb.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ayt::ay2d {

namespace {

// Phase 3F: derive the sprite's world-space AABB from its
// `worldMatrix` translation. The translation row lives in
// `m[6]` (X) and `m[7]` (Y) — the standard 2D affine layout:
//   [ sx  shx tx ]
//   [ shy sy  ty ]
//   [ 0   0   1  ]
// row-major flat storage (§17.3). Y translation row-major is
// at index 7 in the standard Float3x3 layout. We DO NOT factor
// scale / rotation (R-3F.4) — the per-sprite AABB is `center
// ± 0.5` regardless of matrix scale, which the future PR can
// extend via a per-sprite bounds field.
[[nodiscard]] ayt::math::FRectangle spriteAabbOf(
    const Sprite& s) noexcept {
    // Float3x3 has 9 floats stored row-major (verified at
    // aymath/MathTypes.h:551; Phase 3A踩坑 #15 — use m[i],
    // not row[i].x like Float4x4).
    const float cx = s.worldMatrix.m[6];
    const float cy = s.worldMatrix.m[7];
    return ayt::math::FRectangle{
        ayt::math::FVector2{cx - 0.5f, cy - 0.5f},
        ayt::math::FVector2{cx + 0.5f, cy + 0.5f}};
}

// Phase 3F: combine AABB intersection + layer-mask test in
// one branch-predictable gate. Returns true iff the sprite
// should be emitted into the scene.
[[nodiscard]] inline bool isSpriteVisible(
    const Sprite& s,
    const ayt::math::FRectangle& cameraRect,
    uint32_t layerMask) noexcept {
    // Step 1: AABB. The most common failure mode (sprite far
    // off-camera in a large map) drops here.
    if (!cameraRect.intersects(spriteAabbOf(s))) return false;
    // Step 2: layer-mask bit test. Layer is 0..31 (Sprite::layer
    // uint8_t). Mask 0xFFFFFFFF (default) passes all.
    return (layerMask & (1u << (s.layer & 0x1Fu))) != 0u;
}

} // namespace

void buildSpriteScene(
    const std::vector<Sprite>&              sprites,
    const OrthographicCamera&               camera,
    std::vector<SpriteDrawCmd>&             out) noexcept {
    // Step 0: preconditions. Empty input emits empty output;
    // degenerate camera emits empty output (R-3F.2). Both
    // fall through to the clear below.
    out.clear();

    if (sprites.empty()) return;

    // R-3F.2 lock: degenerate camera means empty output. We
    // do not rely on `FRectangle::intersects` semantic with
    // an empty rect — the strict-less compare in AYMath can
    // spuriously match on the (0, 0) edge. The cull-level
    // short-circuit is the authoritative gate.
    if (camera.viewSize <= 0.0f
        || camera.viewport.heightPx <= 0
        || camera.viewport.widthPx  <= 0) {
        return;
    }

    const ayt::math::FRectangle cameraRect = WorldAabb(camera);
    // Step 2 (effectively — AABB cull is fused into isSpriteVisible):
    // build a scratch list of pointers / indices. We cannot store
    // the cmd-thunk in `out` directly during the sort because we
    // want caller-controlled `out` capacity (caller reserve).
    // Instead: build a std::vector<size_t> of survivors. Then
    // emit. This means the helper has one extra allocation (the
    // index vector) for every call; for the canonical 10k-sprite
    // Phase 6 budget this is fine — 80 KB on the stack arena,
    // reuses whatever allocator the caller passed (default = heap).
    //
    // The alternative (sort-then-emit in-place into `out`) would
    // couple `out`'s allocation policy to the sort; not worth the
    // added constraint for P3F.
    std::vector<std::size_t> survivorIndices;
    survivorIndices.reserve(sprites.size());

    const uint32_t layerMask = camera.layerMask;
    for (std::size_t i = 0; i < sprites.size(); ++i) {
        if (isSpriteVisible(sprites[i], cameraRect, layerMask)) {
            survivorIndices.push_back(i);
        }
    }
    if (survivorIndices.empty()) return;

    // Step 4: stable sort the survivor indices by packedSortKey.
    // std::stable_sort on indices is the canonical F-2-friendly
    // way to keep input order for ties without a custom comparator.
    std::stable_sort(
        survivorIndices.begin(),
        survivorIndices.end(),
        [&sprites](std::size_t a, std::size_t b) noexcept {
            const uint32_t ka = sprites[a].packedSortKey();
            const uint32_t kb = sprites[b].packedSortKey();
            return ka < kb;
        });

    // Step 5: emit. Caller is responsible for reserving
    // `out` to at least `sprites.size()` (or `survivor count`).
    // We do NOT call `out.reserve(...)` here — that would force
    // an allocator coupling the helper shouldn't make. If the
    // caller reserves nothing, std::vector grows as needed.
    out.reserve(out.size() + survivorIndices.size());
    for (const std::size_t i : survivorIndices) {
        const Sprite& s = sprites[i];
        SpriteDrawCmd cmd{};
        cmd.packedSortKey       = s.packedSortKey();
        cmd.worldMatrix         = s.worldMatrix;
        cmd.sourceRectMin       = ayt::math::FVector2{
            s.sourceRectU0, s.sourceRectV0};
        cmd.sourceRectMax       = ayt::math::FVector2{
            s.sourceRectU1, s.sourceRectV1};
        cmd.colorRGBA           = ayt::math::FVector4{
            s.colorR, s.colorG, s.colorB, s.colorA};
        cmd.flip                = static_cast<uint8_t>(s.flip);
        cmd.layerMaskSnapshot   = layerMask;
        out.push_back(cmd);
    }
}

} // namespace ayt::ay2d
