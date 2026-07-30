// AYTilemap.cpp — Phase 2 chunk-source loader bridge.
//
// design.md §6.2 + §6.3 + §11 Phase 2 / F-18.
//
// All other Tilemap methods are inline in the header (sibling
// `MeshComponent` pattern: short member functions stay inline).
// loadChunkFromSource is the only "real" .cpp file because it
// integrates the chunk-source interface and may grow into a real
// async pump (Phase 3+) without forcing header churn.
//
// Out of scope (Phase 5+ or cross-module PR):
//   - .aytilemap loader / .aytilemap binary format (cross-module
//     PR to AYResource/interface/assetsDefs/IAYTilemap.h).
//   - Per-tile flagsAt backing store (Phase 5+ replaces the
//     `Empty` default with a real .aytilemap-backed data source).
//   - Asynchronous completion via EventBus (Phase 3+).

#include "AYTilemap.h"

#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"
#include "AYTilemapChunkSource.h"

namespace ayt::ay2d {

bool loadChunkFromSource(Tilemap&             t,
                        ITilemapChunkSource* source,
                        ChunkCoord           coord) noexcept {
    if (source == nullptr) {
        t.loadState = TileLoadState::Failed;
        return false;
    }
    t.loadState = TileLoadState::Loading;

    const ChunkRequestHandle handle = source->requestChunk(coord);
    if (!handle.isValid()) {
        // Source refused immediately (queue full / budget). Leave the
        // previous tile array intact; mark loadState Failed so the
        // ECS inspector can surface it (design.md F-18).
        t.loadState = TileLoadState::Failed;
        return false;
    }

    ChunkData chunk;
    if (!source->tryGetChunk(coord, chunk)) {
        // Phase 2 does not yet pump an event loop — synchronous load.
        // The handle was issued; on a real async source we'd register
        // a completion callback. For now, mark Loading and let the
        // caller retry. Phase 3+ swaps to actual async + EventBus.
        t.loadState = TileLoadState::Loading;
        return false;
    }

    // Width mismatch is a contract violation — do not partial-up.
    if (chunk.mode != t.mode) {
        source->cancelChunk(handle);
        t.loadState = TileLoadState::Failed;
        return false;
    }

    if (t.mode == TileIdPackMode::Narrow16) {
        t.tileIds16 = std::move(chunk.tileIds16);
    } else {
        t.tileIds32 = std::move(chunk.tileIds32);
    }
    t.loadState = TileLoadState::Loaded;
    // Phase 3C (design.md §14.2): successful delivery bumps the
    // resident count to the new vector size and counts as one
    // mutation. Failure paths (null source / width mismatch /
    // handle invalid / tryGetChunk returns false) leave both
    // counters untouched — verified by Test_CountersWired.
    const size_t residentCount = (t.mode == TileIdPackMode::Narrow16)
        ? t.tileIds16.size()
        : t.tileIds32.size();
    t.counters.tiles_resident.store(residentCount, std::memory_order_relaxed);
    t.counters.tiles_mutated.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

// P3D.2 (§13.17): world-space AABB of one cell, centered on
// `cellToWorld(c)` (R-3E.5 cell-center) with extent = half the
// tile dimensions. Pure 4-line composition; kept out-of-line so
// future SIMD / bulk variants can grow here without header
// churn. The naive corner-port `min = cellToWorld(c); max =
// cellToWorld(c+{1,1})` would be off by half a cell because
// `cellToWorld` returns the cell center, not the corner.
//
// `cellOrigin` defaults to world `(0, 0)` to match the P3E
// world-coord `setTile` / `getTile` overloads; a future cross-
// module PR (camera composition, §4.2.1) lifts this to a
// member-field-driven origin.
ayt::math::FRectangle Tilemap::aabbOfCell(TileCoord c) const noexcept {
    using ayt::math::FVector2;
    // `fromCenterExtent(center, extent)` is named `extent` but
    // multiplies by 0.5 internally (see MathTypes.cpp:2039-2042),
    // so we pass the FULL tile dimensions. Passing half-extent
    // would shrink the AABB by 4x.
    const FVector2 center = cellToWorld(
        c,
        FVector2{0.0f, 0.0f},
        static_cast<float>(tileWidth),
        static_cast<float>(tileHeight));
    const FVector2 extent{
        static_cast<float>(tileWidth),
        static_cast<float>(tileHeight),
    };
    return ayt::math::FRectangle::fromCenterExtent(center, extent);
}

} // namespace ayt::ay2d
