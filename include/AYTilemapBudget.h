#pragma once
// AYTilemapBudget.h — Phase 3G chunk-source budget value-type.
//
// design.md §6.2 + §18.1: the budget struct shape ships in
// code so `InMemoryTilemapChunkSource::setBudget` can take it
// by const-reference and `budget()` can read it back. The
// shape was previously documented only in design.md (Phase
// 2 §6.2). P3G surfaces the value-type so the LRU wire is
// real; `Distance` / `TimeWindow` eviction policies remain
// deferred to Phase 4 streaming (R-3G.1).
//
// Pure value type. No virtual methods, no allocation, no
// dependency on any other AY2D header. Mirrors the
// `Option<T>` shape used elsewhere in the engine where a
// config struct needs to be cheap to copy.

#include <cstdint>

namespace ayt::ay2d {

enum class EvictionPolicy : uint8_t {
    // Default; mirrors `AYResourceCache`'s strong-ref pattern
    // (design.md §6.2 + §6.2.1). InMemoryTilemapChunkSource
    // implements it directly via `evictIfNeeded`.
    LRU         = 0,
    // Chunk farthest from active camera evicted first. NOT
    // IMPLEMENTED in P3G (R-3G.1); the source returns false
    // when `setBudget` is called with this policy. Phase 4
    // streaming PR is the home for the real implementation.
    Distance    = 1,
    // Chunks untouched for N seconds evicted first. NOT
    // IMPLEMENTED in P3G (R-3G.1); same as Distance.
    TimeWindow  = 2,
};

struct TilemapBudget {
    // Hard cap on chunks loaded into memory (R-3G.4 wire; P3G).
    // Wired to `InMemoryTilemapChunkSource::setCapacity`.
    uint32_t       maxChunksLoaded    = 1024;
    // In-AY2D CPU soft cap, evict-down-to semantics
    // (P3G.2a §13.15). `0` = disabled (no soft cap). When
    // non-zero AND less than `maxChunksLoaded`, calls to
    // `setBudget(b)` (or `setMaxChunksCpuSoftCap`) trim the
    // cache down to this size by evicting LRU-front entries.
    // When non-zero AND greater-or-equal to `maxChunksLoaded`,
    // the hard cap rules and the soft cap is a no-op (the
    // hard cap is the upper bound).
    uint32_t       maxChunksCpuSoftCap = 0;
    // GPU residency ceiling. NOT wired in P3G (R-3G.4) — the
    // field is acknowledged by the budget struct for forward-
    // compat with Phase 6 perf hardening, but the InMemory
    // chunk source silently ignores it.
    uint32_t       maxChunksResident  = 2048;
    // Bytes-per-second ceiling on accepted `requestChunk`
    // calls. `0` disables the rate gate (R-3G.3a); non-zero
    // activates the §18.2 sliding-window policy.
    uint32_t       maxIoBytesPerSec   = 64 * 1024 * 1024;  // 64 MB/s
    // Eviction policy. P3G wires LRU only; Distance / TimeWindow
    // are Phase 4 PR (R-3G.1).
    EvictionPolicy eviction           = EvictionPolicy::LRU;
};

} // namespace ayt::ay2d
