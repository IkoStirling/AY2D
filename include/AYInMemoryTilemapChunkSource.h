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
#include "AYTilemapBudget.h"
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

    // Phase 3G (§18.1): runtime budget mutators. The ctor's
    // `capacity` parameter still works (R-3G.7 / backward
    // compat); these setters rewire the same `_capacity` field
    // post-construction.
    //
    // `setCapacity(0)` flips to unlimited (matches the ctor's
    // `capacity=0` default). `setCapacity(k > 0)` caps the
    // cache at `k` chunks; existing over-resident entries are
    // evicted on the next `evictIfNeeded` call.
    void setCapacity(uint32_t capacity) noexcept;

    // P3G.2a (§13.15): set the in-AY2D CPU soft cap. `0` =
    // disabled (no soft cap). Non-zero AND less than
    // `_capacity` triggers an immediate `evictDownTo(s)` call,
    // removing LRU-front entries until `_cache.size() <= s`.
    // Non-zero AND greater-or-equal to `_capacity` is a no-op
    // (the hard cap rules; the soft cap cannot exceed it).
    // Bumps `chunk_io_residency_reject` if the trim is blocked
    // by a pin set (Phase 4 streaming ships the pin set; today
    // the counter stays at 0 because nothing blocks eviction).
    void setMaxChunksCpuSoftCap(uint32_t softCap) noexcept;

    // `setMaxIoBytesPerSec(0)` disables the rate gate (R-3G.3a).
    // Non-zero activates the sliding-window rejection policy on
    // `requestChunk` (§18.2).
    void setMaxIoBytesPerSec(uint64_t bytesPerSec) noexcept;

    // Atomic "apply a budget" (R-3G.4). Returns true iff the
    // policy is LRU; non-LRU policy → no-op, returns false.
    // `maxChunksResident` is acknowledged but the source does
    // not act on it (R-3G.4; Phase 6 PR).
    // P3G.2a (§13.15): `maxChunksCpuSoftCap` is the in-AY2D
    // second-layer CPU cap; when non-zero and below
    // `maxChunksLoaded`, an immediate `evictDownTo` runs.
    [[nodiscard]] bool setBudget(const TilemapBudget& b) noexcept;

    // Read the live budget. The struct shape matches
    // `TilemapBudget` (forward-compat with Phase 4 / Phase 6).
    [[nodiscard]] TilemapBudget budget() const noexcept {
        return TilemapBudget{
            _capacity,                          // maxChunksLoaded
            _maxChunksCpuSoftCap,               // P3G.2a in-AY2D soft cap
            _budgetRequest.maxChunksResident,   // last-requested residency (R-3G.4)
            _budgetRequest.maxIoBytesPerSec,    // current rate gate
            EvictionPolicy::LRU,                // hard-wired in P3G (R-3G.1)
        };
    }

    // Test-only helper. Advances the rate-gate window so the
    // caller can drive deterministic tests (R-3G.7). The helper
    // bumps `_windowStartUs` to (now - kWindowUs - 1us) so the
    // next `requestChunk` call rolls the window forward. Public
    // surface; Phase 4 streaming PR can promote this to a
    // friend-with-test-shim pattern.
    void advanceWindowForTest(uint64_t nowUs) noexcept;

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

    // Phase 3G (§18.1 / §18.2): rate-gate state. Sliding window
    // of 1 second; bucket accumulates the *would-be* chunk
    // byte size per accepted request. Rejection bumps
    // `chunk_io_reject`. `_budgetRequest` echoes the last
    // `setBudget(b)` call (used by `budget()` to report the
    // residency-side value even though it's not wired).
    static constexpr uint64_t kRateWindowUs = 1'000'000ull; // 1 second
    uint64_t  _maxIoBytesPerSec   = 0;   // 0 = disabled (R-3G.3a)
    uint64_t  _windowStartUs      = 0;   // wall-clock at window start
    uint64_t  _consumedInWindow   = 0;   // bytes consumed in current window
    TilemapBudget _budgetRequest;       // last-requested budget shape

    // P3G.2a (§13.15): in-AY2D CPU soft cap. `0` = disabled
    // (no soft cap; LRU hard cap rules alone). Non-zero
    // triggers an `evictDownTo(s)` whenever a `setBudget` /
    // `setMaxChunksCpuSoftCap` call sees the new soft cap
    // below the cache size. Same cumulative/resetAll
    // discipline as `chunk_io_reject` for the rejection
    // counter (`chunk_io_residency_reject`).
    uint32_t  _maxChunksCpuSoftCap = 0;  // 0 = disabled

    // Returns the bytes-per-request for this source based on the
    // chunk's nominal storage width. P3G hard-codes the
    // chunk-of-16x16 nominal size:
    //   Narrow16 => 16*16 * 2B = 512 B. But the §6.2 chunk
    //     nominal is the type system's standard (32 KB / 64 KB)
    //     for the rate gate — we use the type system's nominal.
    [[nodiscard]] static constexpr uint64_t nominalChunkBytes(
        TileIdPackMode mode) noexcept {
        // 16*16 cells * 2B (Narrow16) = 512B; we multiply by 64
        // to use a representative 32 KB nominal. Phase 4 PR may
        // make this configurable per `TilemapBudget`.
        return mode == TileIdPackMode::Narrow16
            ? 32ull * 1024ull
            : 64ull * 1024ull;
    }

    void evictIfNeeded() noexcept;

    // P3G.2a (§13.15): trim the cache down to `target` entries
    // by evicting LRU-front entries. No-op when
    // `_cache.size() <= target`. When the pin set (Phase 4
    // streaming) blocks eviction, bumps
    // `chunk_io_residency_reject` per blocked attempt (today
    // the pin set is empty so the bump stays at 0).
    void evictDownTo(uint32_t target) noexcept;
};

} // namespace ayt::ay2d
