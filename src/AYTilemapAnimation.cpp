// AYTilemapAnimation.cpp — Phase 3B tick + resolve impl.
//
// design.md §7.2 + §9.4: integer-ms remainder accumulator. The
// inner loop is the batched tick — one walk over animationTable,
// one walk per non-empty entry. 1000 animated tiles is ~1000 inner
// steps, well under the §11 Phase 3 budget of 0.6 ms on the
// reference desktop (F-7).
//
// The hot path (no animations, or no time delta) is two early-out
// branches before the loop. The slow path (real animation with
// looping) walks the table once per call.

#include "AYTilemap.h"

namespace ayt::ay2d {

namespace {

// Helper: lazily size `currentFrameIdx` and `elapsedMs` to the
// animationTable extent. Sizing is allocation-free in steady state
// (the table extent only grows when a new higher tileId animation
// is registered, and only on that one tick).
void ensureStateSize(TileAnimationState& s, size_t n) noexcept {
    if (s.currentFrameIdx.size() < n) s.currentFrameIdx.resize(n, 0u);
    if (s.elapsedMs.size()       < n) s.elapsedMs.resize(n, 0u);
}

} // namespace

void tickTilemapAnimation(Tilemap& t, int64_t nowUs) noexcept {
    // First tick: stash the baseline, do not advance. This avoids a
    // huge initial jump when a stale caller hands in a far-future
    // `nowUs` on the very first call.
    if (!t.hasBeenTicked) {
        t.lastTickUs    = nowUs;
        t.hasBeenTicked = true;
        // Lazily size the state vectors so subsequent ticks can
        // index them safely even if the table is empty.
        ensureStateSize(t.animationState, t.animationTable.size());
        return;
    }

    int64_t deltaUs = nowUs - t.lastTickUs;
    if (deltaUs < 0) deltaUs = 0;            // R-7 spirit: never run backwards
    t.lastTickUs = nowUs;
    if (deltaUs == 0) return;

    // Integer-ms (design.md §7.2 lock — no float drift).
    const uint32_t deltaMs = static_cast<uint32_t>(deltaUs / 1000);
    if (deltaMs == 0) return;

    const size_t n = t.animationTable.size();
    if (n == 0) return;                     // no animations registered
    ensureStateSize(t.animationState, n);

    for (size_t i = 0; i < n; ++i) {
        const auto& frames = t.animationTable[i];
        if (frames.empty()) continue;        // no anim -> tile id stays static

        uint32_t& elapsed  = t.animationState.elapsedMs[i];
        uint32_t& frameIdx = t.animationState.currentFrameIdx[i];
        elapsed += deltaMs;

        // Walk frames while elapsed exceeds frame duration. Loops
        // by mod (frame index wraps). Stops when elapsed < duration
        // of the new frame (the common case after one advance) OR
        // when a frame has durationMs == 0 (zero-duration frame is
        // a no-op — break so we don't infinite-loop).
        while (true) {
            const TileFrame& f = frames[frameIdx % frames.size()];
            if (f.durationMs == 0 || elapsed < f.durationMs) break;
            elapsed -= f.durationMs;
            frameIdx = (frameIdx + 1u) % static_cast<uint32_t>(frames.size());
        }
    }
}

uint32_t resolveAnimatedTileId(const Tilemap& t, uint32_t sourceTileId) noexcept {
    if (sourceTileId >= t.animationTable.size()) return sourceTileId;
    const auto& frames = t.animationTable[sourceTileId];
    if (frames.empty()) return sourceTileId;
    const size_t idx = t.animationState.currentFrameIdx[sourceTileId] % frames.size();
    return frames[idx].frameTileId;
}

} // namespace ayt::ay2d