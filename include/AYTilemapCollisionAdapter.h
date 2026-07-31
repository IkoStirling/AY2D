#pragma once
// AYTilemapCollisionAdapter.h — Phase 5 concrete adapter.
//
// design.md §8.1 + §11 Phase 5 row: the in-AY2D `TilemapCollisionQueryAdapter`
// is the only concrete implementation of `ITileCollisionQuery` AY2D ships.
// It delegates `flagsAt` to `Tilemap::flagsAtRaw` (which Phase 5 fixes to
// return `CollisionFlags::Empty` for cells with no per-tile flag data).
// `raycast` is a placeholder (always miss); real tile-grid walking lands
// with the consumer-side cross-module PR (§4.2.1 to AYPhysics maintainer).
//
// Lifetime: the adapter holds a reference to a `Tilemap`. The caller
// (e.g. an ECS system or test) MUST guarantee the tilemap outlives the
// adapter. The adapter is non-owning; `release()` does nothing.

#include "AYTileCollision.h"
#include "AYTilemap.h"

namespace ayt::ay2d {

class TilemapCollisionQueryAdapter final : public ITileCollisionQuery {
public:
    explicit TilemapCollisionQueryAdapter(const Tilemap& map) noexcept
        : _map(&map) {}

    ~TilemapCollisionQueryAdapter() override = default;

    TilemapCollisionQueryAdapter(const TilemapCollisionQueryAdapter&) = delete;
    TilemapCollisionQueryAdapter& operator=(const TilemapCollisionQueryAdapter&) = delete;

    // §8.1: delegates to `Tilemap::flagsAtRaw`. Out-of-range cells
    // collapse to `CollisionFlags::Empty` because `flagsAtRaw`
    // ignores the `cell` argument today (Phase 5 placeholder body).
    [[nodiscard]] CollisionFlags flagsAt(TileCoord cell) const noexcept override;

    // §8.1 default impl: `flagsAt(c) != CollisionFlags::Empty`.
    // Inherited from `ITileCollisionQuery`; not overridden here.

    // §8.1 + §13.35: 2D Amanatides-Woo DDA walker (in-AY2D,
    // geometry-only, no resolver per §8.2). Walks the tile grid
    // from `worldToCell(ray.origin)` toward `ray.direction`,
    // advancing one cell per dominant-axis boundary crossing.
    // The first cell where `flagsAtRaw(c) != CollisionFlags::Empty`
    // (L-3D-1 — matches default `isBlocked` per §13.PF C6-R1)
    // terminates the walk and fills `RaycastHit2D`.
    //
    // t semantics (L-3D-2): the returned `hit.t` is along the
    // ORIGINAL `ray.direction` (pre-normalization), so
    // `pointAt(t) == origin + t * direction` always holds
    // (matches §8.1 Ray2D doc). The walker normalizes
    // internally for DDA bookkeeping.
    //
    // tMin semantics (L-3D-5): cells whose entry-t < `ray.tMin`
    // are skipped without flag test (the origin cell is always
    // tested; if it is solid, the reported `t` is `tMin`).
    //
    // maxDistance: hard cutoff (L-3D-6). A hit whose entry t
    // exceeds `maxDistance` is reported as no-hit; `t ==
    // maxDistance` is inclusive.
    //
    // Degenerate `ray.direction == (0, 0)`: returns the no-hit
    // sentinel (L-3D-3).
    //
    // Origin OOB: snaps to the nearest in-grid cell before
    // stepping (L-3D-4).
    //
    // Pure CPU geometry. No allocation. No bgfx. No cross-module
    // PR for this slice (D3 in-AY2D; the resolver consumer
    // remains §4.2.1).
    [[nodiscard]] RaycastHit2D raycast(Ray2D ray, float maxDistance) const noexcept override;

    // Re-target the adapter at a different tilemap. The previous
    // tilemap is not modified (this is a pointer swap, not a copy).
    // `nullptr` is rejected — the adapter always references a real
    // tilemap; "no collision surface" is expressed via the
    // tilemap's own load state.
    void setTilemap(const Tilemap& map) noexcept { _map = &map; }
    [[nodiscard]] const Tilemap* tilemap() const noexcept { return _map; }

private:
    const Tilemap* _map;
};

} // namespace ayt::ay2d