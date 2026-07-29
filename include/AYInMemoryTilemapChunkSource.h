#pragma once
// AYInMemoryTilemapChunkSource.h — Phase 2 chunk-source backend
// implementation contract.
//
// design.md §6.2: a chunk source is the unit test's mount point
// for the production async .aytilemap-backed source. The InMemory
// variant keeps every chunk fully resident + synchronous so unit
// tests can verify Tilemap::loadChunkFromSource end-to-end without
// spinning up AYResource or the .aytilemap loader.
//
// Production chunks are never delivered through this backend
// (it's only useful for tests + the upcoming `TilemapParallaxDemo`
// offline cache). The .aytilemap-backed `AYResourceChunkSource`
// lands via cross-module PR in Phase 3+ (design.md §4.2.1).
//
// Policy: LRU eviction (default; mirrors `AYResourceCache`'s
// strong-ref pattern, design.md §6.2 + §6.2.1). Distance and
// TimeWindow policies arrive with Phase 4 streaming
// (design.md §6.2).

#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "AY2DCounters.h"
#include "AYChunkData.h"
#include "AYChunkRequestHandle.h"
#include "AYTileCoord.h"
#include "AYTilemapChunkSource.h"

namespace ayt::ay2d {

class InMemoryTilemapChunkSource final : public ITilemapChunkSource {
public:
    using LRUList  = std::list<std::pair<ChunkCoord, ChunkData>>;
    using MapKey   = int64_t;
    static MapKey packKey(ChunkCoord c) noexcept {
        return (static_cast<int64_t>(c.x) << 32) | static_cast<uint32_t>(c.y);
    }

    // `capacity` is the resident-chunk cap (analogous to
    // design.md §6.2 `TilemapBudget::maxChunksResident`). 0 means
    // unlimited (the unit-test default).
    explicit InMemoryTilemapChunkSource(uint32_t capacity = 0) noexcept
        : _capacity(capacity) {}

    ~InMemoryTilemapChunkSource() override = default;

    InMemoryTilemapChunkSource(const InMemoryTilemapChunkSource&) = delete;
    InMemoryTilemapChunkSource& operator=(const InMemoryTilemapChunkSource&) = delete;

    // Pre-populate the cache with a fully-formed chunk payload.
    // Used by tests + editor-time map authoring. Returns false if a
    // chunk with the same coord already exists (no silent overwrite).
    bool put(ChunkCoord coord, ChunkData data) noexcept;

    [[nodiscard]] uint32_t residentCount() const noexcept { return static_cast<uint32_t>(_cache.size()); }
    [[nodiscard]] uint32_t capacity() const noexcept     { return _capacity; }

    // ITilemapChunkSource:
    [[nodiscard]] ChunkRequestHandle requestChunk(ChunkCoord coord) noexcept override;
    [[nodiscard]] bool               tryGetChunk(ChunkCoord coord, ChunkData& out) noexcept override;
    [[nodiscard]] bool               isResident(ChunkCoord coord) const noexcept override;
    void                            cancelChunk(ChunkRequestHandle handle) noexcept override;

    // LRU helpers exposed for implementation reuse. These let
    // AYTilemap.cpp (and .cpp-side test handlers) touch a node without
    // keeping the lookup map duplicated. They are class members rather
    // than namespace-scope functions so the private fields stay
    // encapsulated.
    //
    // LRU invariant: the back of `_cache` is MRU (most-recently used);
    // the front is LRU (least-recently used). `putBack` is the only
    // insertion point, `touch` moves the touched node to the back, and
    // `evictIfNeeded` removes from the front. This ordering is what
    // makes the Test_InMemoryTilemapChunkSource::EvictionAtCapacity
    // test's "touch (0,0), then put (2,0)" sequence evict (1,0) and
    // keep (0,0).
    void touch(MapKey key) noexcept;
    void putBack(ChunkCoord coord, ChunkData&& data) noexcept;
    void eraseByKey(MapKey key) noexcept;
    [[nodiscard]] bool contains(MapKey key) const noexcept;
    [[nodiscard]] LRUList::iterator find(MapKey key) noexcept;
    [[nodiscard]] bool empty() const noexcept { return _cache.empty(); }

    // Telemetry view (design.md §10.1.1). The chunk source writes
    // its slice of the counters here; consumers (the owning
    // World2D) may merge into their own counters via copy. The
    // source keeps its own counters because the chunk source
    // lifetime is independent of the world's per-frame reset.
    [[nodiscard]] const Ay2DCounters& counters() const noexcept { return _counters; }
    [[nodiscard]]       Ay2DCounters& counters()       noexcept { return _counters; }

private:
    uint32_t  _capacity = 0;
    // Phase 3: chunk request ids split into 24-bit index + 8-bit
    // generation. `_nextRequestIndex` is the monotonic counter; the
    // generation is bumped every time the index wraps (max 16 M
    // outstanding requests -> generation wraps around 256 retries).
    uint32_t  _nextRequestIndex   = 1;  // 0 = invalid (kInvalidId)
    uint32_t  _nextRequestGen     = 1;  // gen 0 reserved for invalid
    LRUList   _cache;
    std::unordered_map<MapKey, LRUList::iterator> _index;

    // Phase 3 telemetry. The source's slice of the world counters.
    // See `counters()` accessor.
    Ay2DCounters _counters;

    // Pending requests: handle id -> (coord, request_time_us).
    // Filled in requestChunk; drained when the matching put() lands
    // OR when cancelChunk fires. Keyed by the full 32-bit id (so
    // ABA attempts see a 32-bit mismatch even when the index
    // portion collides).
    struct PendingEntry {
        ChunkCoord coord;
        uint64_t   requestTimeUs = 0;
    };
    std::unordered_map<uint32_t, PendingEntry> _pending;

    void evictIfNeeded() noexcept;
};

} // namespace ayt::ay2d
