// AYInMemoryTilemapChunkSource.cpp — Phase 2 in-memory chunk source.
//
// design.md §6.2 + LRU eviction semantics. The class is a thin
// LRU cache + a request-id-to-coord pending map. Real backend
// (`.aytilemap` ↔ `AYResource::IAYTilemap`) lands via
// cross-module PR in Phase 3+; this file stays as the test /
// demo-immediate source.
//
// Member-style helpers (`touch` / `putFront` / `eraseByKey` /
// `contains` / `find`) are class members rather than namespace-
// scope free functions so private storage (`_cache`, `_index`) stays
// encapsulated while still being reusable from the .cpp-side impl.

#include "AYInMemoryTilemapChunkSource.h"

#include <algorithm>
#include <utility>

namespace ayt::ay2d {

void InMemoryTilemapChunkSource::touch(MapKey key) noexcept {
    auto it = _index.find(key);
    if (it == _index.end()) return;
    // LRU touch: move the node to the back (MRU end). Splice-before-
    // end is the canonical idiom; the iterator stability means the
    // lookup map's stored iterator remains valid across the splice.
    if (it->second != std::prev(_cache.end())) {
        _cache.splice(_cache.end(), _cache, it->second);
    }
    _index[key] = it->second;
}

void InMemoryTilemapChunkSource::putBack(ChunkCoord coord, ChunkData&& data) noexcept {
    _cache.emplace_back(coord, std::move(data));
    const MapKey key = packKey(coord);
    _index[key] = std::prev(_cache.end());
}

void InMemoryTilemapChunkSource::eraseByKey(MapKey key) noexcept {
    auto it = _index.find(key);
    if (it == _index.end()) return;
    _cache.erase(it->second);
    _index.erase(it);
}

bool InMemoryTilemapChunkSource::contains(MapKey key) const noexcept {
    return _index.find(key) != _index.end();
}

InMemoryTilemapChunkSource::LRUList::iterator
InMemoryTilemapChunkSource::find(MapKey key) noexcept {
    auto it = _index.find(key);
    if (it == _index.end()) return _cache.end();
    return it->second;
}

bool InMemoryTilemapChunkSource::put(ChunkCoord coord, ChunkData data) noexcept {
    const MapKey key = packKey(coord);
    if (contains(key)) return false;  // pre-existing

    // Insert at MRU end (back), then evict from LRU end (front).
    putBack(coord, std::move(data));
    evictIfNeeded();

    // Match any pending request for this coord. The pending map is
    // bookkeeping to keep request handles even when the source has
    // just delivered the chunk; clear matching entries so a future
    // `cancelChunk` is a clean no-op.
    for (auto it = _pending.begin(); it != _pending.end(); ) {
        if (packKey(it->second) == key) it = _pending.erase(it);
        else                              ++it;
    }
    return true;
}

void InMemoryTilemapChunkSource::evictIfNeeded() noexcept {
    if (_capacity == 0) return;
    // Drop from the front (least-recently-used).
    while (_cache.size() > _capacity) {
        const MapKey key = packKey(_cache.front().first);
        _cache.erase(_cache.begin());
        _index.erase(key);
    }
}

ChunkRequestHandle InMemoryTilemapChunkSource::requestChunk(ChunkCoord coord) noexcept {
    const MapKey key = packKey(coord);

    // Already resident — synthesise a handle and touch it to MRU.
    if (contains(key)) {
        touch(key);
        return ChunkRequestHandle{++_nextRequestId};
    }

    // Reserve a request id; the caller may later cancel, or the
    // matching put() lands and the next tryGetChunk delivers the
    // payload.
    const uint32_t id = ++_nextRequestId;
    _pending.emplace(id, coord);
    return ChunkRequestHandle{id};
}

bool InMemoryTilemapChunkSource::tryGetChunk(ChunkCoord coord, ChunkData& out) noexcept {
    const MapKey key = packKey(coord);
    if (!contains(key)) return false;

    // Copy out — Phase 2 keeps the cached payload intact so the LRU
    // still works for the next caller. `move()` would empty the
    // shared source on subsequent calls.
    const auto it = find(key);
    out = it->second;
    touch(key);
    return true;
}

bool InMemoryTilemapChunkSource::isResident(ChunkCoord coord) const noexcept {
    return contains(packKey(coord));
}

void InMemoryTilemapChunkSource::cancelChunk(ChunkRequestHandle handle) noexcept {
    if (!handle.isValid()) return;
    _pending.erase(handle.id());
}

} // namespace ayt::ay2d
