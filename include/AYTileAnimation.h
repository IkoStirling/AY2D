#pragma once
// AYTileAnimation.h — Phase 3B per-tile animation table.
//
// design.md §7.2: (tileId -> [frameTileId, durationMs]*) lives on
// Tilemap. Frame change is batched per system tick. The table is
// stored as a single flat vector indexed by tileId for O(1) lookup
// (no hash map — Phase 3B caps "animated tiles per tilemap" at the
// 65 535 distinct-tile-id range of TileIdPackMode::Narrow16, and
// even a small sparse table is faster linearly scanned than hashed
// in a 65 k-vector).
//
// Animation state (frame index + ms remainder accumulator) lives on
// Tilemap::animationState. The tick is a free function — the ECS
// system wrapper (TilemapAnimationTickSystem @ priority 460) lands
// with the AYEntity cross-module PR in Phase 3+.

#include <cstdint>
#include <vector>

namespace ayt::ay2d {

struct TileFrame {
    uint32_t frameTileId = 0;   // tile id to display for this frame
    uint32_t durationMs  = 0;   // hold duration in integer ms (design.md §7.2)
};

// Sparse table: indexed by `sourceTileId`. Empty inner vector ==
// "no animation" (the tile id stays static at the source value).
//
// The outer vector's size is the largest sourceTileId + 1 across
// all registered animations; that is the hot-path index bound.
// A flat std::vector<std::vector<TileFrame>> is simpler + faster
// than a hash map for the access pattern (one O(1) indexed lookup
// per tile per frame, no allocation in the hot path).
using TileAnimationTable = std::vector<std::vector<TileFrame>>;

// Per-tile mutable animation state. Frame indices advance on every
// tick; elapsedMs accumulates the integer-ms remainder so a frame
// duration that does not divide the tick period evenly carries the
// excess into the next tick (design.md §7.2 + §9.4 locks — no float
// drift across long play sessions).
struct TileAnimationState {
    std::vector<uint32_t> currentFrameIdx;  // index into the inner vec; 0 when no anim
    std::vector<uint32_t> elapsedMs;        // ms into the current frame
};

} // namespace ayt::ay2d