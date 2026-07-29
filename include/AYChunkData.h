#pragma once
// AYChunkData.h — chunk payload structure (Phase 2, header-only).
//
// design.md §6.2: A ChunkData is the unit of tile-id storage that
// ITilemapChunkSource delivers to the cache. The struct holds exactly
// one of two vector payloads (`tileIds16` or `tileIds32`) — never both,
// never mixed — selected by the `mode` flag (mirrors TileIdPackMode
// declared in AYTileCoord.h, included here so this header is
// self-contained).
//
// Why vector-of-tile-id (not a raw pointer / span): the loader is
// the only authority on ownership / lifetime, and the chunk is opaque
// to consumers (Tilemap). Vector gives RAII lifetime + reserve + reuse
// for the source. Phase 4 streaming will likely swap the storage for a
// mapped-on-demand sparse array; that swap is the `ITilemapChunkSource`
// boundary's job, not this header's.

#include <cstdint>
#include <vector>

#include "AYChunkRequestHandle.h"
#include "AYTileCoord.h"
#include "AYTileLoadState.h"

namespace ayt::ay2d {

struct ChunkData {
    ChunkCoord                coord { 0, 0 };
    TileIdPackMode            mode  = TileIdPackMode::Narrow16;
    // One and only one of tileIds16 / tileIds32 is populated. Phase 2
    // tilemap loader contracts guarantee no mixed-width buffers.
    std::vector<uint16_t>     tileIds16;
    std::vector<uint32_t>     tileIds32;
    uint64_t                  versionStamp = 0;  // monotonic per-chunk; bumped by source on reload

    // Helper: returns the size in bytes of the actually-populated
    // payload. Useful for telemetry (design.md §10.1.1
    // `ay2d_chunk_io_bytes`).
    [[nodiscard]] uint64_t populatedBytes() const noexcept {
        return mode == TileIdPackMode::Narrow16
            ? static_cast<uint64_t>(tileIds16.size() * sizeof(uint16_t))
            : static_cast<uint64_t>(tileIds32.size() * sizeof(uint32_t));
    }
};

} // namespace ayt::ay2d
