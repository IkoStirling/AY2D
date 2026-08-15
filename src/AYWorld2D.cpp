// AYWorld2D.cpp — Phase 3 real impl for World2D.
//
// design.md §3 + §3.4: addTilemap / removeTilemap / swapTilemap
// bump resourceEpoch. The implementation is intentionally simple
// (linear scan over `entries`) because Phase 3 caps the registry
// at a few hundred entries; Phase 4 streaming replaces this with a
// hash map keyed on TilemapResourceHandle (cross-module PR per
// design.md §4.2.1).
//
// Lifetime: this file owns the `entries` vector. IAYTilemap* is
// non-owning — the actual IResource lifetime is owned by
// AYResource's L2 cache (design.md §9.3). The World2D only stores
// raw pointers as a Phase 3 placeholder; the cross-module PR
// replaces these with TilemapResourceHandle (strong-ref LRU handle)
// once AYResource ships the IAYTilemap interface.

#include "AY2D/World2D.h"
#include "AY2D/TilemapChunkSource.h"  // P3I.2: need the full type to call purgeChunks()

#include <algorithm>

namespace ayt::ay2d {

TilemapHandle World2D::addTilemap(uint32_t layer, uint32_t sortingKey) noexcept {
    // Legacy 2-arg overload: delegate with `nullptr`. The 3-arg
    // overload below is the canonical add path (P3I.2 / §13.21).
    return addTilemap(layer, sortingKey, nullptr);
}

TilemapHandle World2D::addTilemap(uint32_t layer,
                                  uint32_t sortingKey,
                                  ITilemapChunkSource* chunkSource) noexcept {
    Entry e;
    e.handle.id         = _nextTilemapId++;
    // Phase 3: each add advances a monotonic generation so a
    // removed-then-re-added handle at the same id position has a
    // different generation. The (id, generation) pair is the
    // ABA-safe handle.
    e.handle.generation = _nextTilemapGeneration++;
    e.layer             = layer;
    e.sortingKey        = sortingKey;
    e.resource          = nullptr;
    // P3I.2 / §13.21: bind the chunk source pointer (non-owning).
    // `nullptr` means "no source bound" — the entry is still
    // valid; `removeTilemap` just skips the purge step in that
    // case.
    e.chunkSource       = chunkSource;
    entries.push_back(std::move(e));
    bumpEpoch();
    // Phase 3C (§14.2): in-world gauge bump on successful add.
    counters.tilemaps_in_world.fetch_add(1u, std::memory_order_relaxed);
    return entries.back().handle;
}

bool World2D::removeEntryByHandle(TilemapHandle handle) {
    if (!handle.id) return false;
    auto it = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& e) {
            return e.handle.id == handle.id
                && e.handle.generation == handle.generation;
        });
    if (it == entries.end()) return false;
    // Bump the generation so any stale handle seen by a stale
    // caller fails the (id, generation) check (basic ABA guard —
    // the id is reusable after removal, but the generation is
    // bumped on removal to invalidate outstanding copies).
    //
    // The bumped generation record is *not* retained in the
    // vector — the live entry is removed. We just bump the local
    // copy so callers that kept a copy see "removed" on lookup.
    it->handle.generation = handle.generation + 1;
    entries.erase(it);
    return true;
}

bool World2D::removeTilemap(TilemapHandle handle) noexcept {
    // P3I.2 / §13.21 L-3I-5 strict ordering:
    //   1. find the entry pointer (entry still alive)
    //   2. if it has a bound chunk source, call purgeChunks()
    //      BEFORE erasing the entry
    //   3. erase via removeEntryByHandle (which also bumps the
    //      generation so any stale handle fails ABA check)
    //   4. bumpEpoch (resourceEpoch is bumped only by the
    //      remove, not by the purge — purge is internal to
    //      removeTilemap; §3.4 lock)
    //   5. saturating decrement tilemaps_in_world
    //
    // The previous (pre-P3I.2) body did just (3)+(4)+(5); chunk
    // eviction did not happen at all. The new ordering preserves
    // (3)+(4)+(5) verbatim and adds (1)+(2).
    Entry* e = findEntryByHandle(handle);
    if (!e) return false;
    if (e->chunkSource) e->chunkSource->purgeChunks();
    if (removeEntryByHandle(handle)) {
        bumpEpoch();
        // Phase 3C (§14.2): decrement gauge with saturating guard
        // (R-3C.1). On a fresh world or after an unmatched add (which
        // never bumps), saturate to 0 instead of underflowing the
        // uint32_t.
        const uint32_t prev = counters.tilemaps_in_world.load(std::memory_order_relaxed);
        if (prev > 0u) {
            counters.tilemaps_in_world.store(prev - 1u, std::memory_order_relaxed);
        }
        return true;
    }
    return false;
}

bool World2D::swapTilemap(TilemapHandle handle,
                          uint32_t newLayer,
                          uint32_t newSortingKey) noexcept {
    if (!handle.id) return false;
    auto it = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& e) {
            return e.handle.id == handle.id
                && e.handle.generation == handle.generation;
        });
    if (it == entries.end()) return false;
    it->layer      = newLayer;
    it->sortingKey = newSortingKey;
    bumpEpoch();
    // Phase 3C (§14.2): swap is in-place — no count delta (only
    // resourceEpoch bumps, per the §3.4 lock).
    return true;
}

const World2D::Entry* World2D::find(TilemapHandle handle) const noexcept {
    if (!handle.id) return nullptr;
    auto it = std::find_if(entries.cbegin(), entries.cend(),
        [&](const Entry& e) {
            return e.handle.id == handle.id
                && e.handle.generation == handle.generation;
        });
    return it == entries.cend() ? nullptr : &(*it);
}

World2D::Entry* World2D::findEntryByHandle(TilemapHandle handle) noexcept {
    // P3I.2 / §13.21: non-const, mutable-pointer variant used by
    // `removeTilemap` to read `chunkSource` BEFORE erasing the
    // entry. Mirrors the lookup logic in `find()` / `removeEntryByHandle`
    // (exact id + generation match).
    if (!handle.id) return nullptr;
    auto it = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& e) {
            return e.handle.id == handle.id
                && e.handle.generation == handle.generation;
        });
    return it == entries.end() ? nullptr : &(*it);
}

World2D::Entry* World2D::find(TilemapHandle handle) noexcept {
    if (!handle.id) return nullptr;
    auto it = std::find_if(entries.begin(), entries.end(),
        [&](const Entry& e) {
            return e.handle.id == handle.id
                && e.handle.generation == handle.generation;
        });
    return it == entries.end() ? nullptr : &(*it);
}

} // namespace ayt::ay2d
