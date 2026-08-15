// Test_SpriteDrawCmdLayout.cpp - P3J.2 / A-3 layout static_assert.
//
// design.md §13.26. The SpriteDrawCmd POD (include/AY2D/SpriteDrawCmd.h,
// shipped Phase 3F v0.1.9) had a manual layout comment claiming
// 88 B; P3J.2 discovered the actual size is 112 B because MSVC's
// FVector2 / FVector4 are 16-B-aligned SIMD types (see 踩坑 #33 /
// P3H.1's SpriteSheet std::string story for the parallel). The
// layout comment was corrected in the header; this test promotes
// the corrected layout to a hard lock.

#include "AY2D/SpriteDrawCmd.h"

#include <cstddef>
#include <type_traits>

#include "AYTest.h"

#include "AYMath/MathTypes.h"

using namespace ayt::ay2d;

TEST_SUITE(SpriteDrawCmdLayoutSuite)

    TEST_CASE(SpriteDrawCmd_StaticAssert_SizeIs112Bytes) {
        static_assert(sizeof(SpriteDrawCmd) == 112u,
                      "SpriteDrawCmd must be 112 bytes "
                      "(design.md §13.26, post-correction).");
        static_assert(alignof(SpriteDrawCmd) == 16u,
                      "SpriteDrawCmd alignment is 16 "
                      "(driven by FVector2 / FVector4 SIMD align).");
        static_assert(std::is_trivially_copyable<SpriteDrawCmd>::value,
                      "SpriteDrawCmd must be trivially copyable for "
                      "std::vector memcpy-on-realloc (§17.3).");
        static_assert(std::is_standard_layout<SpriteDrawCmd>::value,
                      "SpriteDrawCmd must be standard-layout for the "
                      "future cross-module DrawItem::payload translator.");
        // Trigger the test runner to count this case as passing.
        CHECK_TRUE(true);
    }

    TEST_CASE(SpriteDrawCmd_OffsetOf_PackedSortKey_IsZero) {
        CHECK_INT_EQ(static_cast<size_t>(offsetof(SpriteDrawCmd, packedSortKey)),
                     size_t{0});
    }

    TEST_CASE(SpriteDrawCmd_OffsetOf_WorldMatrix_IsFour) {
        CHECK_INT_EQ(static_cast<size_t>(offsetof(SpriteDrawCmd, worldMatrix)),
                     size_t{4});
    }

    TEST_CASE(SpriteDrawCmd_OffsetOf_SourceRectMin_Is48) {
        // 4 (packedSortKey) + 36 (9 floats) + 8-byte pad-to-16 = 48.
        CHECK_INT_EQ(static_cast<size_t>(offsetof(SpriteDrawCmd, sourceRectMin)),
                     size_t{48});
    }

    TEST_CASE(SpriteDrawCmd_OffsetOf_ColorRGBA_Is80) {
        // 48 + 16 + 16 = 80.
        CHECK_INT_EQ(static_cast<size_t>(offsetof(SpriteDrawCmd, colorRGBA)),
                     size_t{80});
    }

    TEST_CASE(SpriteDrawCmd_OffsetOf_LayerMaskSnapshot_Is100) {
        // 96 (colorRGBA + flip byte) + 4 (pad + u32) = 100.
        CHECK_INT_EQ(static_cast<size_t>(offsetof(SpriteDrawCmd, layerMaskSnapshot)),
                     size_t{100});
    }

    TEST_CASE(SpriteDrawCmd_DefaultConstructed_AllDefaultsMatch) {
        SpriteDrawCmd c{};
        CHECK_INT_EQ(c.packedSortKey, 0u);
        CHECK_TRUE(c.sourceRectMin.x == 0.0f && c.sourceRectMin.y == 0.0f);
        CHECK_TRUE(c.sourceRectMax.x == 1.0f && c.sourceRectMax.y == 1.0f);
        CHECK_TRUE(c.colorRGBA.x == 1.0f && c.colorRGBA.y == 1.0f
                   && c.colorRGBA.z == 1.0f && c.colorRGBA.w == 1.0f);
        CHECK_INT_EQ(c.flip, uint8_t{0});
        CHECK_INT_EQ(c.layerMaskSnapshot, 0u);
        CHECK_TRUE(c.worldMatrix.m[0] == 1.0f && c.worldMatrix.m[4] == 1.0f
                   && c.worldMatrix.m[8] == 1.0f);
    }

TEST_SUITE_END