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
    return true;
}

} // namespace ayt::ay2d
