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

TEST_SUITE_END
