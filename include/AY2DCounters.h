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
    };

    [[nodiscard]] Snapshot snapshot() const noexcept {
        return Snapshot{
            chunk_io_us.load(std::memory_order_relaxed),
            chunk_io_bytes.load(std::memory_order_relaxed),
            chunk_resident_count.load(std::memory_order_relaxed),
            atlas_bytes.load(std::memory_order_relaxed),
            draw2d_items.load(std::memory_order_relaxed),
            draw2d_pass_us.load(std::memory_order_relaxed),
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
    }

    // Reset the per-frame fields only (chunk_io_* and
    // chunk_resident_count / atlas_bytes are cumulative).
    void resetPerFrame() noexcept {
        draw2d_items.store(0, std::memory_order_relaxed);
        draw2d_pass_us.store(0, std::memory_order_relaxed);
    }
};

} // namespace ayt::ay2d
