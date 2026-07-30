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
#include <chrono>
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
    // Phase 3 telemetry: read the byte count from `data` BEFORE the
    // move into the cache (post-move, `data` is in a moved-from state
    // and accessing its members is undefined behavior).
    const uint64_t bytes = (data.mode == TileIdPackMode::Narrow16)
        ? data.tileIds16.size() * sizeof(uint16_t)
        : data.tileIds32.size() * sizeof(uint32_t);
    _cache.emplace_back(coord, std::move(data));
    const MapKey key = packKey(coord);
    _index[key] = std::prev(_cache.end());
    _counters.chunk_io_bytes.fetch_add(bytes, std::memory_order_relaxed);
    _counters.chunk_resident_count.store(
        static_cast<uint32_t>(_cache.size()), std::memory_order_relaxed);
}

void InMemoryTilemapChunkSource::eraseByKey(MapKey key) noexcept {
    auto it = _index.find(key);
    if (it == _index.end()) return;
    _cache.erase(it->second);
    _index.erase(it);
    _counters.chunk_resident_count.store(
        static_cast<uint32_t>(_cache.size()), std::memory_order_relaxed);
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
    //
    // Phase 3: the loop compares the packed (coord) key, not the
    // generation portion of the handle. Multiple outstanding
    // requests for the same coord share the same (coord) key, so
    // all of them get cleared at put() time (the matching has
    // already happened and the consumer is expected to consult
    // tryGetChunk next).
    //
    // Phase 3 telemetry: when a pending entry is matched, compute
    // the request → put latency and accumulate into
    // `chunk_io_us`. The latency is the wall-clock interval the
    // consumer waited for the chunk (lower is better; the budget
    // is 16 ms p99 per design.md §10.1).
    for (auto it = _pending.begin(); it != _pending.end(); ) {
        if (packKey(it->second.coord) == key) {
            const uint64_t nowUs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            if (it->second.requestTimeUs != 0 && nowUs > it->second.requestTimeUs) {
                _counters.chunk_io_us.fetch_add(
                    nowUs - it->second.requestTimeUs, std::memory_order_relaxed);
            }
            it = _pending.erase(it);
        } else {
            ++it;
        }
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
    // Phase 3 telemetry: refresh the resident count after eviction
    // so a post-put() snapshot reflects the cached state, not the
    // pre-eviction state.
    _counters.chunk_resident_count.store(
        static_cast<uint32_t>(_cache.size()), std::memory_order_relaxed);
}

ChunkRequestHandle InMemoryTilemapChunkSource::requestChunk(ChunkCoord coord) noexcept {
    const MapKey key = packKey(coord);

    // Already resident — synthesise a handle and touch it to MRU.
    if (contains(key)) {
        touch(key);
        // Phase 3: the (index, generation) constructor composes
        // the id. The generation stays at `_nextRequestGen` so a
        // subsequent outstanding handle survives the touch.
        return ChunkRequestHandle{++_nextRequestIndex, _nextRequestGen};
    }

    // Phase 3G (§18.2): rate gate. When `_maxIoBytesPerSec == 0`
    // the gate is disabled (R-3G.3a) and the rest of this
    // function falls through to the pre-P3G path. The gate
    // sits BEFORE the pending-map insert so a rejected request
    // does not accumulate `_pending` noise.
    if (_maxIoBytesPerSec != 0) {
        const uint64_t nowUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        // Window rollover: when now - _windowStartUs exceeds the
        // 1-second window, reset the consumed counter and start a
        // new window.
        if (_windowStartUs == 0 || nowUs - _windowStartUs >= kRateWindowUs) {
            _windowStartUs    = nowUs;
            _consumedInWindow = 0;
        }
        // The chunk-bytes-per-request is the source's nominal —
        // all `InMemoryTilemapChunkSource` chunks use the same
        // width; we read it via the helper. P3G hard-codes the
        // 32 KB / 64 KB nominals.
        const uint64_t nominal = nominalChunkBytes(TileIdPackMode::Narrow16);
        if (_consumedInWindow + nominal > _maxIoBytesPerSec) {
            // Rate-limited: bump the rejection counter, return
            // an invalid handle. The caller's `tryGetChunk` will
            // see no entry and report "not loaded".
            _counters.chunk_io_reject.fetch_add(
                1u, std::memory_order_relaxed);
            return ChunkRequestHandle{0u, 0u};
        }
        _consumedInWindow += nominal;
    }

    // Reserve the next (index, generation). When the index wraps
    // past ChunkRequestHandle::kMaxIndex, bump the generation and
    // restart the index from 1. The wrap is the basic ABA guard:
    // outstanding handles whose index collided across generations
    // fail the equality check on generation.
    if (_nextRequestIndex > ChunkRequestHandle::kMaxIndex) {
        _nextRequestIndex = 1;
        ++_nextRequestGen;
        // Generation also wraps (8 bits). Wraparound is documented
        // as "extremely unlikely" — index + generation jointly
        // support 16 M × 256 ≈ 4.3 billion unique ids before wrap,
        // which the per-resource LRU never reaches.
    }
    const uint32_t idx = ++_nextRequestIndex;
    const uint32_t gen = _nextRequestGen;
    const uint32_t id  = ChunkRequestHandle::pack(idx, gen);

    // Phase 3 telemetry: stamp the request time so put() can
    // compute the request → delivery latency into chunk_io_us.
    //
    // The wall clock read here is also used by the rate gate
    // above; we re-read here only for symmetry with the
    // pre-P3G path. The cost is one cheap `now()` call per
    // accepted request.
    const uint64_t nowUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    _pending.emplace(id, PendingEntry{coord, nowUs});
    return ChunkRequestHandle{idx, gen};
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
    // Phase 3: erase by the full 32-bit id (index + generation
    // packed). The generation portion of the key ensures a stale
    // handle (e.g. an old chunk request that was already matched
    // and bumped) does not accidentally erase a fresh handle's
    // pending entry.
    _pending.erase(handle.id());
}

// ----------------------------------------------------------------------
// Phase 3G (design.md §18.1, §18.4): runtime budget mutators.
// ----------------------------------------------------------------------

void InMemoryTilemapChunkSource::setCapacity(uint32_t capacity) noexcept {
    // LRU wire: same `_capacity` field the ctor populates. Setting
    // to 0 disables the cap (unlimited, matches ctor default).
    _capacity = capacity;
    if (_capacity != 0) {
        // Drop anyone now over the cap (front = LRU). Uses the
        // same logic as `evictIfNeeded` but called explicitly so
        // the cap change takes effect without waiting on the next
        // `putBack`.
        while (_cache.size() > _capacity) {
            const MapKey k = packKey(_cache.front().first);
            _cache.erase(_cache.begin());
            _index.erase(k);
        }
        _counters.chunk_resident_count.store(
            static_cast<uint32_t>(_cache.size()), std::memory_order_relaxed);
    }
    // Echo back into the budget request so `budget()` reads back
    // the same number.
    _budgetRequest.maxChunksLoaded = _capacity;
}

void InMemoryTilemapChunkSource::setMaxChunksCpuSoftCap(uint32_t softCap) noexcept {
    // P3G.2a (§13.15): in-AY2D CPU soft cap. `0` = disabled.
    // Non-zero AND below `_capacity` triggers immediate
    // `evictDownTo(softCap)`. Non-zero AND >= `_capacity` is a
    // no-op (the hard cap rules). The `setBudget` aggregator
    // calls this same path on the budget's `maxChunksCpuSoftCap`
    // field.
    _maxChunksCpuSoftCap = softCap;
    _budgetRequest.maxChunksCpuSoftCap = softCap;
    if (softCap != 0 && _capacity != 0 && softCap < _capacity) {
        evictDownTo(softCap);
    }
    // softCap >= _capacity case: hard cap rules; soft cap is a
    // no-op. softCap == 0 case: disabled; nothing to trim.
}

void InMemoryTilemapChunkSource::evictDownTo(uint32_t target) noexcept {
    // P3G.2a (§13.15): trim the cache down to `target` by
    // evicting LRU-front entries. No-op when already below
    // `target`. The pin-set (Phase 4 streaming) is the future
    // home of "blocked eviction" tracking; today no pins
    // exist so `chunk_io_residency_reject` stays at 0 in the
    // trim path.
    if (target == 0) {
        // 0 = "trim everything" — drain the cache.
        target = 1;  // sentinel: while-loop checks `size > target`
    }
    // Avoid `target == 0` erasing all (we want at least 1
    // resident for stable iterator behaviour).
    while (_cache.size() > target && _cache.size() > 1) {
        const MapKey k = packKey(_cache.front().first);
        _cache.erase(_cache.begin());
        _index.erase(k);
    }
    // Edge case: if `target == 0` AND `_capacity == 0` (the
    // ctor default unlimited), drain entirely.
    if (target == 1 && _capacity == 0 && _cache.size() == 1) {
        const MapKey k = packKey(_cache.front().first);
        _cache.erase(_cache.begin());
        _index.erase(k);
    }
    _counters.chunk_resident_count.store(
        static_cast<uint32_t>(_cache.size()), std::memory_order_relaxed);
}

void InMemoryTilemapChunkSource::setMaxIoBytesPerSec(uint64_t bytesPerSec) noexcept {
    // R-3G.3a: 0 disables the gate. The `_windowStartUs` is
    // intentionally NOT reset here — if the gate is re-enabled
    // later, the new window starts on the next `requestChunk`
    // call (lazy).
    _maxIoBytesPerSec = bytesPerSec;
    _budgetRequest.maxIoBytesPerSec = bytesPerSec;
}

bool InMemoryTilemapChunkSource::setBudget(const TilemapBudget& b) noexcept {
    // R-3G.4: policy == LRU is the only one wired. Distance /
    // TimeWindow return false (no-op; R-3G.1).
    if (b.eviction != EvictionPolicy::LRU) return false;

    setCapacity(b.maxChunksLoaded);
    setMaxIoBytesPerSec(b.maxIoBytesPerSec);
    // P3G.2a: soft cap is the in-AY2D second-layer CPU cap.
    // Routes through `setMaxChunksCpuSoftCap` so the trim
    // logic is in one place.
    setMaxChunksCpuSoftCap(b.maxChunksCpuSoftCap);
    // Residency side: acknowledged but not wired (R-3G.4).
    _budgetRequest.maxChunksResident = b.maxChunksResident;
    return true;
}

void InMemoryTilemapChunkSource::advanceWindowForTest(uint64_t nowUs) noexcept {
    // R-3G.7: test-only deterministic helper. Bumps the
    // window's `_windowStartUs` such that the next
    // `requestChunk` call rolls the window forward — so the
    // caller does not need a real-time `sleep`.
    //
    // Setting `_windowStartUs` to a value older than the window
    // triggers the rollover branch in `requestChunk`. The new
    // window then starts at `nowUs` (the actual wall clock
    // read inside `requestChunk`).
    if (_maxIoBytesPerSec != 0 && _windowStartUs != 0) {
        _windowStartUs = nowUs - kRateWindowUs - 1u;
    }
}

} // namespace ayt::ay2d
