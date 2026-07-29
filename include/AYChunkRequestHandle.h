#pragma once
// AYChunkRequestHandle.h — opaque async token for ITilemapChunkSource
// requests. Phase 2 lives inside AY2D; header-only data class (no .cpp).
//
// design.md §6.2: ChunkRequestHandle is forward-declared in the public
// header so Phase 0 stub code referenced it; Phase 2 promotes it to a
// first-class opaque token with a generation-bit check. The 32-bit id
// pack is the same layout as BodyHandle / ColliderHandle in AYPhysics
// (packed index + generation in a single uint32_t). For Phase 2 we
// keep the layout deliberately small: chunk IO is per-resource and
// the LRU eviction in TilemapBudget caps resident count to a few
// thousand chunks, so 32 bits are ample.
//
// Lifetime: holds no resources; safe to copy and discard freely. The
// matching cancelChunk / tryGetChunk / isResident calls on
// ITilemapChunkSource all take a ChunkRequestHandle by value.

#include <cstdint>

namespace ayt::ay2d {

class ChunkRequestHandle {
public:
    static constexpr uint32_t kInvalidId = 0;

    constexpr ChunkRequestHandle() noexcept = default;
    explicit constexpr ChunkRequestHandle(uint32_t id) noexcept : id_(id) {}

    [[nodiscard]] constexpr uint32_t id() const noexcept { return id_; }
    [[nodiscard]] constexpr bool     isValid() const noexcept { return id_ != kInvalidId; }

    [[nodiscard]] constexpr bool operator==(const ChunkRequestHandle& other) const noexcept {
        return id_ == other.id_;
    }
    [[nodiscard]] constexpr bool operator!=(const ChunkRequestHandle& other) const noexcept {
        return id_ != other.id_;
    }

private:
    uint32_t id_ = kInvalidId;
};

} // namespace ayt::ay2d
