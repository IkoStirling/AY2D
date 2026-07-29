#pragma once
// AYChunkRequestHandle.h — opaque async token for ITilemapChunkSource
// requests. Phase 3 promotes the underlying representation to a
// packed 24-bit index + 8-bit generation, giving basic ABA safety
// without bumping the design (the on-the-wire shape is still a
// single uint32_t, matching the BodyHandle / ColliderHandle pattern
// in AYPhysics).
//
// design.md §6.2: ChunkRequestHandle is forward-declared in the
// public header so Phase 0 stub code could reference it. Phase 2
// promoted it to a first-class opaque token. Phase 3 splits the
// token into (index, generation) so an outstanding handle
// invalidated by a sequence of (request → cancel → request)
// doesn't accidentally match the new request's id.
//
// Layout (32 bits total):
//   bits  0..23  → index (24 bits, max 16 777 215 outstanding requests)
//   bits 24..31  → generation (8 bits, wraps at 256 — adequate for
//                  InMemoryTilemapChunkSource's per-resource use)
// index == 0 ⇒ "invalid" (kInvalidId). The generation of an invalid
// handle is also 0.
//
// Lifetime: holds no resources; safe to copy and discard freely.
// The matching cancelChunk / tryGetChunk / isResident calls on
// ITilemapChunkSource all take a ChunkRequestHandle by value.

#include <cstdint>

namespace ayt::ay2d {

class ChunkRequestHandle {
public:
    // 0 = invalid; any index == 0 is treated as a non-handle.
    static constexpr uint32_t kInvalidId = 0;

    // Bit layout constants.
    static constexpr uint32_t kIndexBits     = 24;
    static constexpr uint32_t kIndexMask     = (1u << kIndexBits) - 1u;        // 0x00FFFFFF
    static constexpr uint32_t kGenerationBits = 8;
    static constexpr uint32_t kGenerationMask = (1u << kGenerationBits) - 1u;  // 0xFF
    static constexpr uint32_t kGenerationShift = kIndexBits;                   // 24
    static constexpr uint32_t kMaxIndex = kIndexMask;                          // 16 777 215

    constexpr ChunkRequestHandle() noexcept = default;

    // Construct from an already-packed 32-bit id. Used by tests
    // and by the source's internal bookkeeping; the public
    // requestChunk API does NOT take this form.
    explicit constexpr ChunkRequestHandle(uint32_t packedId) noexcept
        : id_(packedId) {}

    // Internal constructor used by the source. Not exposed to the
    // public header so the (index, generation) pairing stays
    // private to the chunk source. Friends declare themselves; see
    // AYInMemoryTilemapChunkSource::requestChunk for the canonical
    // call site.
    constexpr ChunkRequestHandle(uint32_t index, uint32_t generation) noexcept
        : id_(pack(index, generation)) {}

    // Pack / unpack helpers (constexpr, no .cpp needed).
    static constexpr uint32_t pack(uint32_t index, uint32_t generation) noexcept {
        return ((generation & kGenerationMask) << kGenerationShift)
             | (index & kIndexMask);
    }

    [[nodiscard]] constexpr uint32_t index() const noexcept {
        return id_ & kIndexMask;
    }

    [[nodiscard]] constexpr uint32_t generation() const noexcept {
        return (id_ >> kGenerationShift) & kGenerationMask;
    }

    [[nodiscard]] constexpr uint32_t id() const noexcept { return id_; }

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return index() != kInvalidId;
    }

    // Equality compares BOTH the index and the generation, so an
    // outstanding handle invalidated by a sequence of (request →
    // cancel → request) does NOT match the new request's handle.
    // This is the basic ABA guard mentioned in the file comment.
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
