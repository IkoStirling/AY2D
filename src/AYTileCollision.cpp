// AYTileCollision.cpp — Phase 5 out-of-line definitions for
// types declared in `AY2D/TileCollision.h`.
//
// design.md §8.1 + §11 Phase 5 row.
//
// Today the only out-of-line body is `Ray2D::pointAt`. Future
// additions (e.g. a SIMD `pointAt4` for batch raycasts) can grow
// in this TU without header churn.

#include "AY2D/TileCollision.h"

namespace ayt::ay2d {

ayt::math::FVector2 Ray2D::pointAt(float t) const noexcept {
    // `origin + t * direction`. The caller is responsible for
    // asserting `t >= tMin` (per §8.1 doc); the function does not
    // enforce it so a debug-overlay caller can sample any `t`.
    return ayt::math::FVector2{
        origin.x + t * direction.x,
        origin.y + t * direction.y,
    };
}

} // namespace ayt::ay2d