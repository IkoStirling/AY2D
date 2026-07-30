#pragma once
// AY2DCounters.h — Phase 3 profiling counters for AY2D.
//
// design.md §10.1.1 (F-8): AY2D ships six `ay2d_<metric>_<unit>`
// counters as instance fields on the owning object (not TU-static
// globals). The naming convention is module-local (snake_case; the
// engine has no global profiler) — see F-8 transparency note.
//
// Six counters, mirroring the table in design.md §10.1.1:
//
//   | ay2d_chunk_io_us         | uint64_t | microseconds  |
//   | ay2d_chunk_io_bytes     | uint64_t | bytes         |
//   | ay2d_chunk_resident_count| uint32_t | count         |
//   | ay2d_atlas_bytes        | uint64_t | bytes         |
//   | ay2d_draw2d_items       | uint32_t | count         |
//   | ay2d_draw2d_pass_us     | uint64_t | microseconds  |
//
// Phase 3C (§14) extends the struct with three tile-dimension
// metrics that are in-AY2D scope (no cross-module PR):
//
//   | ay2d_tiles_mutated      | uint64_t | count         |
//   | ay2d_tiles_resident     | uint64_t | count         |
//   | ay2d_tilemaps_in_world  | uint32_t | count         |
//   | ay2d_chunk_io_reject    | uint64_t | count         |  (Phase 3G, §18.1)
//
// See design.md §14.2 for the wiring contract (which mutation
// path bumps which field).
//
// All counters are `std::atomic` so increments are thread-safe
// across the Presenter / streaming system / chunk source threads.
// Memory ordering: relaxed (counters are telemetry, not sync
// points). A snapshot is a relaxed load of each field — there is
// no cross-field consistency guarantee; that is fine for telemetry.
//
// The struct is POD-equivalent (no virtual methods, no custom
// constructor) so it can live inside `World2D` / `Tilemap` /
// `Draw2DPass` without friending. Consumers / tests access via
// the `counters()` getter on the owning object.

#include <atomic>
#include <cstdint>

namespace ayt::ay2d {

struct Ay2DCounters {
    // Total wall time spent on chunk IO (cumulative).
    // `requestChunk → tryGetChunk ready` interval.
    std::atomic<uint64_t> chunk_io_us         = 0;

    // Total decoded bytes delivered to the cache.
    std::atomic<uint64_t> chunk_io_bytes     = 0;

    // Current number of chunks resident in the LRU cache.
    // (Gauge, not counter — sampled on demand.)
    std::atomic<uint32_t> chunk_resident_count = 0;

    // Sum of all .ayatlas textures currently resident in L3.
    // (Gauge — Phase 3 keeps it as zero; populated by the
    // RenderResourceManager once that lands via cross-module PR.)
    std::atomic<uint64_t> atlas_bytes        = 0;

    // Per-frame `Draw2DItem` count submitted by RenderSystem2D.
    // (Per-frame gauge — reset each frame by the system.)
    std::atomic<uint32_t> draw2d_items       = 0;

    // Wall time of Draw2DPass::execute (per-frame).
    std::atomic<uint64_t> draw2d_pass_us     = 0;

    // Phase 3C (§14): tile-dimension counters (in-AY2D scope).

    // Cumulative count of successful tile mutations (setTile writes
    // that landed in the storage vector, resizeGrid calls,
    // clear() calls, loadChunkFromSource success deliveries).
    // Failed / out-of-range operations do NOT bump this counter —
    // see design.md §14.2 no-double-counting invariant.
    std::atomic<uint64_t> tiles_mutated       = 0;

    // Gauge: total number of tile slots currently resident in the
    // `Tilemap::tileIds{16,32}` vector. Zero on construction; the
    // `setTile` first-write lazy-fill bumps this to `expected =
    // cols * rows`. `resizeGrid` / `clear` reset it back to 0.
    // `loadChunkFromSource` success sets it to `tileIds{16,32}.size()`.
    std::atomic<uint64_t> tiles_resident      = 0;

    // Gauge: number of tilemaps currently registered in a World2D.
    // Lifetime owner is `World2D::counters`. `addTilemap` bumps
    // by 1; `removeTilemap` matching decrements by 1 (saturating
    // at 0; see §14.5 R-3C.1).
    std::atomic<uint32_t> tilemaps_in_world   = 0;

    // Phase 3G (§18.1): cumulative count of `requestChunk`
    // rejections caused by the rate gate
    // (`maxIoBytesPerSec` exceeded). Cumulative — reset ONLY
    // by `resetAll`, NOT by `resetPerFrame` (R-3G.2). The rate
    // gate is the in-AY2D budget enforcer for the P3G
    // `TilemapBudget` shape; Phase 4 streaming PR extends it.
    std::atomic<uint64_t> chunk_io_reject     = 0;

    // P3G.2a (§13.15): cumulative count of soft-cap eviction
    // failures. The soft cap is the second-layer in-AY2D CPU
    // cap (`maxChunksCpuSoftCap`); when `setBudget` lowers the
    // soft cap below the cache size, the source trims the
    // cache down to the soft cap. If the trim is blocked by an
    // outstanding pin / handle, the counter increments. Same
    // discipline as `chunk_io_reject`: cumulative, reset ONLY
    // by `resetAll`, NOT by `resetPerFrame`.
    std::atomic<uint64_t> chunk_io_residency_reject = 0;

    // Cheap non-atomic snapshot helper. Returns a copy of the
    // current values; the snapshot is not internally consistent
    // across fields (each field is a relaxed load) but is
    // adequate for telemetry.
    struct Snapshot {
        uint64_t chunk_io_us;
        uint64_t chunk_io_bytes;
        uint32_t chunk_resident_count;
        uint64_t atlas_bytes;
        uint32_t draw2d_items;
        uint64_t draw2d_pass_us;
        // Phase 3C (§14): tile-dimension metrics.
        uint64_t tiles_mutated;
        uint64_t tiles_resident;
        uint32_t tilemaps_in_world;
        // Phase 3G (§18.1): rate-gate rejection counter.
        uint64_t chunk_io_reject;
        // P3G.2a (§13.15): soft-cap eviction failure counter.
        uint64_t chunk_io_residency_reject;
    };

    [[nodiscard]] Snapshot snapshot() const noexcept {
        return Snapshot{
            chunk_io_us.load(std::memory_order_relaxed),
            chunk_io_bytes.load(std::memory_order_relaxed),
            chunk_resident_count.load(std::memory_order_relaxed),
            atlas_bytes.load(std::memory_order_relaxed),
            draw2d_items.load(std::memory_order_relaxed),
            draw2d_pass_us.load(std::memory_order_relaxed),
            // Phase 3C.
            tiles_mutated.load(std::memory_order_relaxed),
            tiles_resident.load(std::memory_order_relaxed),
            tilemaps_in_world.load(std::memory_order_relaxed),
            // Phase 3G.
            chunk_io_reject.load(std::memory_order_relaxed),
            // P3G.2a.
            chunk_io_residency_reject.load(std::memory_order_relaxed),
        };
    }

    // Reset every counter to zero. Used by tests + by the
    // per-frame `draw2d_items` / `draw2d_pass_us` resets.
    void resetAll() noexcept {
        chunk_io_us.store(0, std::memory_order_relaxed);
        chunk_io_bytes.store(0, std::memory_order_relaxed);
        chunk_resident_count.store(0, std::memory_order_relaxed);
        atlas_bytes.store(0, std::memory_order_relaxed);
        draw2d_items.store(0, std::memory_order_relaxed);
        draw2d_pass_us.store(0, std::memory_order_relaxed);
        // Phase 3C.
        tiles_mutated.store(0, std::memory_order_relaxed);
        tiles_resident.store(0, std::memory_order_relaxed);
        tilemaps_in_world.store(0, std::memory_order_relaxed);
        // Phase 3G (R-3G.2: chunk_io_reject is cumulative, NOT
        // per-frame — reset by resetAll only).
        chunk_io_reject.store(0, std::memory_order_relaxed);
        // P3G.2a: chunk_io_residency_reject is cumulative
        // (R-3G.2 discipline extended to soft-cap eviction
        // failures); reset by resetAll only.
        chunk_io_residency_reject.store(0, std::memory_order_relaxed);
    }

    // Reset the per-frame fields only (chunk_io_* and
    // chunk_resident_count / atlas_bytes / tiles_mutated /
    // tiles_resident / tilemaps_in_world are cumulative).
    void resetPerFrame() noexcept {
        draw2d_items.store(0, std::memory_order_relaxed);
        draw2d_pass_us.store(0, std::memory_order_relaxed);
    }
};

} // namespace ayt::ay2d
