#pragma once
// AY2D/WorldAabb.h — Phase 3F camera world-AABB helper.
//
// design.md §17.4 + R-3F.2: derive the camera's world-space AABB
// from `OrthographicCamera`. The derivation is intentionally
// pure-math (no AYRenderer include). The math is cheap enough
// to keep inline; the helper is a 1-line composition of
// `OrthographicCamera`'s already-public methods.
//
// The AABB intentionally does NOT factor `zoom` (R-3F.4 / §17.4)
// — the canonical AY2D camera model is `zoom == 1.0`. Future
// cross-module PRs that require zoom-aware pre-cull can extend
// this surface; today's in-AY2D scope uses 1:1 world-to-pixel.
//
// Degenerate inputs (`viewSize <= 0`, `viewport.heightPx <= 0`)
// return an empty `FRectangle` (min == max). `FRectangle::intersects`
// on an empty rect is false, so the AABB-based cull drops every
// sprite — that is the documented R-3F.2 behavior.

#include <cstdint>

#include "AYMath/MathTypes.h"  // FRectangle + FVector2

#include "AY2D/OrthographicCamera.h"

namespace ayt::ay2d {

[[nodiscard]] inline ayt::math::FRectangle WorldAabb(
    const OrthographicCamera& cam) noexcept {
    if (cam.viewSize <= 0.0f || cam.viewport.heightPx <= 0) {
        // Empty rect: min == max == (0, 0). FRectangle::intersects
        // returns false for this case (verified in AYMath unit
        // tests; the cross-module PR's `payload` translation does
        // not need to special-case empty).
        return ayt::math::FRectangle{
            ayt::math::FVector2{0.0f, 0.0f},
            ayt::math::FVector2{0.0f, 0.0f}};
    }
    const float half   = cam.viewSize * 0.5f;
    const float aspect = cam.viewportAspect();
    return ayt::math::FRectangle{
        ayt::math::FVector2{cam.positionX - half * aspect,
                            cam.positionY - half},
        ayt::math::FVector2{cam.positionX + half * aspect,
                            cam.positionY + half}};
}

} // namespace ayt::ay2d
