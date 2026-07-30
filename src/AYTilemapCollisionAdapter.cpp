// AYTilemapCollisionAdapter.cpp — Phase 5 concrete adapter body.
//
// design.md §8.1 + §11 Phase 5 row.
//
// Today the adapter is a thin delegator: `flagsAt` forwards to
// `Tilemap::flagsAtRaw`, which Phase 5 fixes to return
// `CollisionFlags::Empty` for any cell (no per-tile flag
// storage yet; that ships with the cross-module PR to
// AYResource for the `.aytilemap` binary).
//
// `raycast` is a placeholder: always returns `RaycastHit2D{hit=false}`.
// A real axis-aligned tile-grid walker lands with the consumer
// side cross-module PR (§4.2.1 to AYPhysics maintainer).

#include "AYTilemapCollisionAdapter.h"

#include "AYTileCollision.h"

namespace ayt::ay2d {

CollisionFlags TilemapCollisionQueryAdapter::flagsAt(TileCoord cell) const noexcept {
    if (_map == nullptr) return CollisionFlags::Empty;
    // flagsAtRaw currently ignores `cell` and returns the placeholder
    // Empty (post-Phase-5 body fix). The cast `static_cast<CollisionFlags>`
    // is well-defined: the uint32_t payload is one of the
    // CollisionFlags enumerators by construction.
    return static_cast<CollisionFlags>(_map->flagsAtRaw(cell));
}

RaycastHit2D TilemapCollisionQueryAdapter::raycast(Ray2D /*ray*/, float /*maxDistance*/) const noexcept {
    // Placeholder per §11 Phase 5 row. The real implementation walks
    // an axis-aligned tile grid (DDA / Amanatides-Woo); it lands
    // with the cross-module PR to AYPhysics (§4.2.1) so the
    // resolver consumer drives the choice. Returning a default
    // RaycastHit2D means `hit=false`, no cell, no flags, no point.
    return RaycastHit2D{};
}

} // namespace ayt::ay2d