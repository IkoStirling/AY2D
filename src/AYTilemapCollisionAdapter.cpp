// AYTilemapCollisionAdapter.cpp — Phase 5 concrete adapter body.
//
// design.md §8.1 + §11 Phase 5 row + §13.35 D3.
//
// Today the adapter is a thin delegator: `flagsAt` forwards to
// `Tilemap::flagsAtRaw`, which Phase 5 fixes to return
// `CollisionFlags::Empty` for any cell (no per-tile flag
// storage yet; that ships with the cross-module PR to
// AYResource for the `.aytilemap` binary).
//
// `raycast` is an axis-aligned Amanatides-Woo 2D DDA walker
// (D3 / §13.35). The walker is geometry-only (no collision
// resolver per §8.2); the consumer-side resolver (broadphase,
// character controller, normal-based correction) lands via
// the cross-module PR to AYPhysics (§4.2.1).

#include "AY2D/TilemapCollisionAdapter.h"

#include "AY2D/TileCollision.h"
#include "AY2D/TileMath.h"

#include <cmath>
#include <limits>

namespace ayt::ay2d {

CollisionFlags TilemapCollisionQueryAdapter::flagsAt(TileCoord cell) const noexcept {
    if (_map == nullptr) return CollisionFlags::Empty;
    // flagsAtRaw currently ignores `cell` and returns the placeholder
    // Empty (post-Phase-5 body fix). The cast `static_cast<CollisionFlags>`
    // is well-defined: the uint32_t payload is one of the
    // CollisionFlags enumerators by construction.
    return static_cast<CollisionFlags>(_map->flagsAtRaw(cell));
}

RaycastHit2D TilemapCollisionQueryAdapter::raycast(Ray2D ray, float maxDistance) const noexcept {
    // §8.1 + §13.35 — Amanatides-Woo 2D DDA walker (in-AY2D, geometry
    // only; no collision resolver per §8.2). Early-exit predicate =
    // `flagsAtRaw(c) != CollisionFlags::Empty` (L-3D-1).

    // L-3D-3: degenerate direction → no-hit sentinel.
    if (ray.direction.x == 0.0f && ray.direction.y == 0.0f) {
        return RaycastHit2D{};
    }
    if (_map == nullptr) return RaycastHit2D{};
    if (_map->tileWidth == 0u || _map->tileHeight == 0u) return RaycastHit2D{};

    const uint32_t cols = _map->cols;
    const uint32_t rows = _map->rows;
    if (cols == 0u || rows == 0u) return RaycastHit2D{};

    const float tileW = static_cast<float>(_map->tileWidth);
    const float tileH = static_cast<float>(_map->tileHeight);
    const ayt::math::FVector2 cellOrigin{0.0f, 0.0f};

    // L-3D-2: walker normalizes internally for DDA bookkeeping; the
    // returned `hit.t` is along the ORIGINAL `ray.direction` (so
    // `pointAt(t) == origin + t * direction` always holds).
    const float dx = ray.direction.x;
    const float dy = ray.direction.y;
    const float invDx = (dx != 0.0f) ? 1.0f / dx : 0.0f;
    const float invDy = (dy != 0.0f) ? 1.0f / dy : 0.0f;

    // L-3D-4: OOB origin → snap to nearest in-grid cell before
    // stepping. worldToCell may return negative or OOB coords;
    // snap explicitly here.
    TileCoord cell = worldToCell(ray.origin, cellOrigin, tileW, tileH);
    if (!isCellInWorldBounds(cell, cols, rows)) {
        if (cell.x < 0) cell.x = 0;
        else if (static_cast<uint32_t>(cell.x) >= cols) cell.x = static_cast<int32_t>(cols - 1u);
        if (cell.y < 0) cell.y = 0;
        else if (static_cast<uint32_t>(cell.y) >= rows) cell.y = static_cast<int32_t>(rows - 1u);
    }

    const int32_t stepX = (dx > 0.0f) ? 1 : (dx < 0.0f) ? -1 : 0;
    const int32_t stepY = (dy > 0.0f) ? 1 : (dy < 0.0f) ? -1 : 0;

    // World-space center of the *current* cell along each axis
    // (cell CENTER per R-3E.5, AY2D/TileMath.h:18-19). Walker derives
    // edges by `center +/- 0.5 * tileSize * sign`; this is the
    // half-cell trap documented in AY2D/Tilemap.h:203-208.
    auto cellCenterX = [&](TileCoord c) -> float {
        return cellToWorld(c, cellOrigin, tileW, tileH).x;
    };
    auto cellCenterY = [&](TileCoord c) -> float {
        return cellToWorld(c, cellOrigin, tileW, tileH).y;
    };

    const float kInf = std::numeric_limits<float>::infinity();

    // tMaxX/Y = parametric distance (along ORIGINAL `direction`)
    // at which the ray crosses the next X / Y cell boundary from
    // inside the current cell.
    const float tMaxX = (stepX != 0)
        ? (cellCenterX(cell) + 0.5f * tileW * static_cast<float>(stepX) - ray.origin.x) * invDx
        : kInf;
    const float tMaxY = (stepY != 0)
        ? (cellCenterY(cell) + 0.5f * tileH * static_cast<float>(stepY) - ray.origin.y) * invDy
        : kInf;
    const float tDeltaX = (stepX != 0) ? tileW * std::fabs(invDx) : kInf;
    const float tDeltaY = (stepY != 0) ? tileH * std::fabs(invDy) : kInf;

    // L-3D-5: origin cell is always tested. If it is solid, reported
    // t = tMin (monotone: t >= tMin always).
    RaycastHit2D hit{};
    hit.cell  = cell;
    hit.flags = static_cast<CollisionFlags>(_map->flagsAtRaw(cell));
    if (hit.flags != CollisionFlags::Empty) {
        if (ray.tMin <= maxDistance) {
            hit.hit = true;
            hit.t = ray.tMin;
            hit.point = ray.pointAt(hit.t);
        }
        return hit;
    }

    float tNextX = tMaxX;
    float tNextY = tMaxY;
    int32_t curX = cell.x;
    int32_t curY = cell.y;

    // Safety cap: at most cols + rows steps covers any path through
    // the grid (every step crosses at least one cell boundary).
    const int64_t maxSteps = static_cast<int64_t>(cols) + static_cast<int64_t>(rows) + 1;
    for (int64_t step = 0; step < maxSteps; ++step) {
        const float tEntry = std::fmin(tNextX, tNextY);

        // L-3D-6: hard cutoff (inclusive: `t == maxDistance` still
        // reports the hit below).
        if (tEntry > maxDistance) break;

        // L-3D-5: skip cells whose entry t is behind tMin without
        // flag test.
        if (tEntry < ray.tMin) {
            if (tNextX <= tNextY) {
                curX += stepX;
                tNextX += tDeltaX;
            } else {
                curY += stepY;
                tNextY += tDeltaY;
            }
            if (!isCellInWorldBounds(TileCoord{curX, curY}, cols, rows)) break;
            continue;
        }

        // Advance one cell along whichever boundary is nearer.
        const bool xWon = (tNextX <= tNextY);
        if (xWon) {
            curX += stepX;
            tNextX += tDeltaX;
        } else {
            curY += stepY;
            tNextY += tDeltaY;
        }
        const TileCoord next{curX, curY};
        if (!isCellInWorldBounds(next, cols, rows)) break;
        const CollisionFlags f = static_cast<CollisionFlags>(_map->flagsAtRaw(next));
        if (f != CollisionFlags::Empty) {
            hit.hit   = true;
            hit.t     = tEntry;
            hit.cell  = next;
            hit.flags = f;
            // `hit.point` is the world-space entry boundary along the
            // dominant axis. X-wins: x is the cell boundary, y is the
            // ray `pointAt(t).y`. Y-wins: symmetric.
            if (xWon) {
                const float bx = cellCenterX(TileCoord{curX - stepX, curY})
                               + 0.5f * tileW * static_cast<float>(stepX);
                hit.point = ayt::math::FVector2{bx, ray.pointAt(tEntry).y};
            } else {
                const float by = cellCenterY(TileCoord{curX, curY - stepY})
                               + 0.5f * tileH * static_cast<float>(stepY);
                hit.point = ayt::math::FVector2{ray.pointAt(tEntry).x, by};
            }
            return hit;
        }
    }
    return RaycastHit2D{};
}

} // namespace ayt::ay2d