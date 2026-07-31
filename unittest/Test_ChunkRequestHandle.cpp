// Test_ChunkRequestHandle.cpp — Phase 3 (index, generation) tests.
//
// design.md §6.2 + Phase 3 follow-up: ChunkRequestHandle packs
// 24-bit index + 8-bit generation into a single uint32_t. This
// gives basic ABA safety without bumping the design or the
// ChunkRequestHandle surface (still a single uint32_t on the wire,
// still constructible from the same get-coord handle API).
//
// Coverage:
//   * Default-constructed handle is invalid (index = 0).
//   * (index, generation) constructor packs the bits correctly.
//   * index() / generation() round-trip.
//   * Equality compares index AND generation (ABA guard).
//   * Constants: kMaxIndex = 0x00FFFFFF, kInvalidId = 0.

#include <cstdint>

#include "AYChunkRequestHandle.h"
#include "AYTest.h"

using ayt::ay2d::ChunkRequestHandle;

TEST_SUITE(ChunkRequestHandleSuite)

    TEST_CASE(DefaultIsInvalid) {
        ChunkRequestHandle h;
        CHECK_FALSE(h.isValid());
        CHECK_INT_EQ(h.index(), 0u);
        CHECK_INT_EQ(h.generation(), 0u);
    }

    TEST_CASE(HandConstructedValid) {
        ChunkRequestHandle h{1u, 1u};
        CHECK(h.isValid());
        CHECK_INT_EQ(h.index(), 1u);
        CHECK_INT_EQ(h.generation(), 1u);
    }

    TEST_CASE(PackIndexIsolation) {
        // Index lives in bits 0..23. Generation lives in bits 24..31.
        ChunkRequestHandle h{0x00ABCDEFu, 0u};
        CHECK_INT_EQ(h.index(), 0x00ABCDEFu);
        CHECK_INT_EQ(h.generation(), 0u);
    }

    TEST_CASE(PackGenerationIsolation) {
        ChunkRequestHandle h{1u, 0x5Au};
        CHECK_INT_EQ(h.index(), 1u);
        CHECK_INT_EQ(h.generation(), 0x5Au);
    }

    TEST_CASE(IndexMaskClipsAbove24Bits) {
        // The constructor masks index to 24 bits, so passing
        // a value with the upper 8 bits set trims them.
        ChunkRequestHandle h{0xFF000001u, 0u};
        CHECK_INT_EQ(h.index(), 0x00000001u);
    }

    TEST_CASE(GenerationMaskClipsAbove8Bits) {
        ChunkRequestHandle h{1u, 0x100u};
        // 0x100 & 0xFF = 0.
        CHECK_INT_EQ(h.generation(), 0u);
    }

    TEST_CASE(EqualityRequiresBothIndexAndGeneration) {
        // Two handles with the same index but different generation
        // are NOT equal. This is the ABA guard.
        ChunkRequestHandle a{42u, 1u};
        ChunkRequestHandle b{42u, 2u};
        CHECK(a != b);
        ChunkRequestHandle c{42u, 1u};
        CHECK(a == c);
    }

    TEST_CASE(KInvalidIdIsZero) {
        CHECK_INT_EQ(ChunkRequestHandle::kInvalidId, 0u);
    }

    TEST_CASE(KMaxIndexIs24Bits) {
        CHECK_INT_EQ(ChunkRequestHandle::kMaxIndex, 0x00FFFFFFu);
    }

    TEST_CASE(PackAndUnpackRoundTrip) {
        const uint32_t idx = 0x00123456u;
        const uint32_t gen = 0xABu;
        const uint32_t packed = ChunkRequestHandle::pack(idx, gen);
        ChunkRequestHandle h{idx, gen};
        CHECK_INT_EQ(packed, h.id());
        // Verify the index portion is idx.
        CHECK_INT_EQ(packed & 0x00FFFFFFu, idx);
        // Verify the generation portion is gen.
        CHECK_INT_EQ((packed >> 24) & 0xFFu, gen);
    }

    // ----- P3J.7 / A-11 wrap-around coverage (design.md §13.31) -----
    //
    // Four cases lock wrap-around at the 8-bit generation boundary
    // (255 -> 256 wraps to 0 via the mask) and the 24-bit index
    // boundary (0xFFFFFF -> 0x1000000 wraps to 0 == kInvalidId).

    TEST_CASE(Generation_FF_Plus_One_Wraps_ToZero) {
        ChunkRequestHandle maxGen{1u, 0xFFu};
        CHECK_INT_EQ(maxGen.generation(), 0xFFu);

        // 0x100 & 0xFF (kGenerationMask) == 0: the 9th bit is
        // stripped by the mask. The wrap-around behavior is
        // implicit in the mask discipline.
        ChunkRequestHandle wrapped{1u, 0x100u};
        CHECK_INT_EQ(wrapped.generation(), 0u);
    }

    TEST_CASE(Index_FFFFFF_Plus_One_Wraps_ToInvalid) {
        // Pre-wrap: index = 0xFFFFFF, isValid == true.
        ChunkRequestHandle pre{0xFFFFFFu, 1u};
        CHECK_INT_EQ(pre.index(), 0xFFFFFFu);
        CHECK(pre.isValid());

        // Post-wrap: 0x1000000 & 0x00FFFFFF == 0 -> isValid == false.
        // The chunk source must guard against this on the next
        // requestChunk call (design.md §6.2 forward-lock).
        ChunkRequestHandle post{0x1000000u, 1u};
        CHECK_INT_EQ(post.index(), 0u);
        CHECK_FALSE(post.isValid());
    }

    TEST_CASE(WrapAround_Equality_StillHoldsForSameIdAndGen) {
        // Same index (1), different generation (0xFF vs 0):
        // even though the generation wrapped, the packed ids
        // differ. Equality is on the full packed id, not on
        // the index alone.
        ChunkRequestHandle preWrap {1u, 0xFFu};
        ChunkRequestHandle postWrap{1u, 0u};
        CHECK(preWrap != postWrap);
        CHECK(preWrap.index() == postWrap.index());
        CHECK(preWrap.generation() != postWrap.generation());
    }

    TEST_CASE(Pack_BoundaryValues_RoundTrip) {
        // (0, 0) -> 0
        CHECK_INT_EQ(ChunkRequestHandle::pack(0u, 0u), 0u);
        // (0xFFFFFF, 0xFF) -> 0xFFFFFFFF (all bits set)
        CHECK_INT_EQ(ChunkRequestHandle::pack(0xFFFFFFu, 0xFFu), 0xFFFFFFFFu);
        // (1, 0) -> 1
        CHECK_INT_EQ(ChunkRequestHandle::pack(1u, 0u), 1u);
        // (1, 1) -> 0x01000001
        CHECK_INT_EQ(ChunkRequestHandle::pack(1u, 1u), 0x01000001u);
    }

TEST_SUITE_END
