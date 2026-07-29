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

#include "AYWorld2D.h"

#include <algorithm>

namespace ayt::ay2d {

TilemapHandle World2D::addTilemap(uint32_t layer, uint32_t sortingKey) noexcept {
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
    entries.push_back(std::move(e));
    bumpEpoch();
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
    if (removeEntryByHandle(handle)) {
        bumpEpoch();
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
