#pragma once
// AYTilemapChunkSource.h — async chunk IO interface (Phase 2).
//
// design.md §6.2 + §6.3: a Tilemap pulls chunks via this interface.
// Producers (e.g. .aytilemap loader, future procedural generator)
// implement `requestChunk` to materialize a chunk's tile-id payload
// asynchronously; consumers `tryGetChunk` to retrieve delivered chunks
// or `isResident` for hot-path predicates (e.g. "should I draw this
// area?").
//
// The interface is deliberately backend-agnostic. Phase 2 ships
// `InMemoryTilemapChunkSource` as a CPU stub; Phase 3+ swaps in
// `AYResource::IAYTilemap`-backed implementations via cross-module
// PR (design.md §4.2.1).
//
// Cancellation is supported so a streaming Tilemap can drop a chunk
// request when the camera moves away before IO completes
// (design.md §6.2.3 / R-7: chunk IO is Present-lane only).

#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYTileCoord.h"

namespace ayt::ay2d {

class ITilemapChunkSource {
public:
    virtual ~ITilemapChunkSource() = default;

    // Issue an async chunk request. Returns an opaque token used to
    // `cancelChunk` or to correlate future `tryGetChunk` results. The
    // returned handle may be Invalid (id == 0) if the source refuses
    // immediately (out of budget, queue full, etc.) — callers must
    // treat invalid handle as "no-op" rather than crash.
    [[nodiscard]] virtual ChunkRequestHandle requestChunk(ChunkCoord coord) noexcept = 0;

    // Try to retrieve a delivered chunk. Returns true iff the chunk
    // is currently cached AND fresh. `out` is only written when the
    // return is true. The source retains ownership of the popped
    // payload's underlying buffer (vectors are *moved* into `out`,
    // so a second `tryGetChunk` call returns false; single-shot
    // delivery is intentional).
    [[nodiscard]] virtual bool tryGetChunk(ChunkCoord coord, ChunkData& out) noexcept = 0;

    // Returns true if the chunk is currently held in the source's
    // cache (i.e. a future `tryGetChunk` would succeed without an
    // additional IO request). Used by hot-path predicates.
    [[nodiscard]] virtual bool isResident(ChunkCoord coord) const noexcept = 0;

    // Best-effort cancel of a previously issued request. No-op when
    // the request has already completed or the handle is invalid.
    virtual void cancelChunk(ChunkRequestHandle handle) noexcept = 0;
};

} // namespace ayt::ay2d
