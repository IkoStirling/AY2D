// Test_Sprite.cpp — Phase 3B Sprite real-impl tests.
//
// design.md §3 + §7.3 + §7.4: Sprite is a single-image / atlas
// sub-rect draw intent. The struct carries worldMatrix (Float3x3),
// sourceRect (4 UV floats), color (4 RGBA floats), flip bits
// (SpriteFlip enum), layer (0..31), sortingKey (0..0x00FFFFFF).
//
// Coverage (8 cases):
//   * default identity / packSortKey layer-in-high-byte /
//     packSortKey layer mask / flip bits compose / flip independent
//     of layer+sort / color default white / sourceRect default full
//     atlas / worldMatrix default identity.
//
// Float access: Float3x3 uses `m[i]` flat storage (NOT `row[i].col`
// like Float4x4). Diagonal identity entries are m[0]/m[4]/m[8]; the
// off-diagonal entries are m[1..3], m[5..7]. The `WorldMatrix*`
// tests use m[i] access — `ay-2d.md §踩坑` entry 13.

#include <cmath>
#include <cstdint>

#include <algorithm>
#include <vector>

#include "AY2D/Sprite.h"
#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(SpriteSuite)

    TEST_CASE(DefaultSpriteIdentity) {
        Sprite s;
        // worldMatrix = identity (covered by WorldMatrixDefaultIdentity test below)
        // sourceRect = full atlas
        CHECK_INT_EQ(static_cast<int>(s.sourceRectU0), 0);
        CHECK_INT_EQ(static_cast<int>(s.sourceRectV0), 0);
        CHECK_INT_EQ(static_cast<int>(s.sourceRectU1), 1);
        CHECK_INT_EQ(static_cast<int>(s.sourceRectV1), 1);
        // color = white opaque
        CHECK_INT_EQ(static_cast<int>(s.colorR), 1);
        CHECK_INT_EQ(static_cast<int>(s.colorG), 1);
        CHECK_INT_EQ(static_cast<int>(s.colorB), 1);
        CHECK_INT_EQ(static_cast<int>(s.colorA), 1);
        // flip = None
        CHECK(s.flip == SpriteFlip::None);
        // layer = 0, sortingKey = 0
        CHECK_INT_EQ(static_cast<int>(s.layer), 0);
        CHECK_INT_EQ(static_cast<int>(s.sortingKey), 0u);
    }

    TEST_CASE(PackSortKeyLayerInHighByte) {
        Sprite s;
        s.layer      = 5;
        s.sortingKey = 0x00ABCDEFu;
        // layer << 24 | sortingKey & 0x00FFFFFF
        const uint32_t packed = s.packedSortKey();
        CHECK_INT_EQ(static_cast<int>(packed),
                     static_cast<int>(0x05000000u | 0x00ABCDEFu));
    }

    TEST_CASE(PackSortKeyLayerMask) {
        Sprite s;
        s.layer      = 0xFFu;                  // out-of-range for 5-bit layer mask
        s.sortingKey = 0x00123456u;
        const uint32_t packed = s.packedSortKey();
        // layer is masked to 0x1F (5 bits) -> 0x1F000000
        CHECK_INT_EQ(static_cast<int>(packed),
                     static_cast<int>(0x1F000000u | 0x00123456u));
        // Sorting-key mask test: high bits in sortingKey are dropped.
        s.sortingKey = 0xFF123456u;            // high byte = 0xFF
        CHECK_INT_EQ(static_cast<int>(s.packedSortKey()),
                     static_cast<int>(0x1F000000u | 0x00123456u));
    }

    TEST_CASE(FlipBitsCompose) {
        // Horizontal | Vertical == Both
        const SpriteFlip f = SpriteFlip::Horizontal | SpriteFlip::Vertical;
        CHECK(f == SpriteFlip::Both);
        CHECK(hasFlip(f, SpriteFlip::Horizontal));
        CHECK(hasFlip(f, SpriteFlip::Vertical));

        // Horizontal & Vertical == None
        const SpriteFlip none = SpriteFlip::Horizontal & SpriteFlip::Vertical;
        CHECK(none == SpriteFlip::None);

        // |= / &= mutate
        SpriteFlip a = SpriteFlip::None;
        a |= SpriteFlip::Horizontal;
        CHECK(a == SpriteFlip::Horizontal);
        a &= SpriteFlip::Both;
        CHECK(a == SpriteFlip::Horizontal);
    }

    TEST_CASE(FlipIndependent) {
        // Flip bits do not consume layer / sortingKey slots.
        Sprite s;
        s.flip       = SpriteFlip::Both;
        s.layer      = 7;
        s.sortingKey = 0x0042u;
        const uint32_t packed = s.packedSortKey();
        CHECK_INT_EQ(static_cast<int>(packed),
                     static_cast<int>(0x07000000u | 0x0042u));
        CHECK(s.flip == SpriteFlip::Both);
    }

    TEST_CASE(ColorComponentsDefaultToWhite) {
        Sprite s;
        CHECK_INT_EQ(static_cast<int>(s.colorR), 1);
        CHECK_INT_EQ(static_cast<int>(s.colorG), 1);
        CHECK_INT_EQ(static_cast<int>(s.colorB), 1);
        CHECK_INT_EQ(static_cast<int>(s.colorA), 1);

        // Mutate + read-back
        s.colorR = 0.25f;
        s.colorG = 0.5f;
        s.colorB = 0.75f;
        s.colorA = 0.0f;
        CHECK(std::fabs(s.colorR - 0.25f) < 1e-6f);
        CHECK(std::fabs(s.colorG - 0.50f) < 1e-6f);
        CHECK(std::fabs(s.colorB - 0.75f) < 1e-6f);
        CHECK(std::fabs(s.colorA - 0.00f) < 1e-6f);
    }

    TEST_CASE(SourceRectDefaultFullAtlas) {
        Sprite s;
        CHECK_INT_EQ(static_cast<int>(s.sourceRectU0), 0);
        CHECK_INT_EQ(static_cast<int>(s.sourceRectV0), 0);
        CHECK_INT_EQ(static_cast<int>(s.sourceRectU1), 1);
        CHECK_INT_EQ(static_cast<int>(s.sourceRectV1), 1);

        // Sub-rect assignment + read-back
        s.sourceRectU0 = 0.1f;
        s.sourceRectV0 = 0.2f;
        s.sourceRectU1 = 0.6f;
        s.sourceRectV1 = 0.8f;
        CHECK(std::fabs(s.sourceRectU0 - 0.1f) < 1e-6f);
        CHECK(std::fabs(s.sourceRectV0 - 0.2f) < 1e-6f);
        CHECK(std::fabs(s.sourceRectU1 - 0.6f) < 1e-6f);
        CHECK(std::fabs(s.sourceRectV1 - 0.8f) < 1e-6f);
    }

    TEST_CASE(WorldMatrixDefaultIdentity) {
        // Float3x3 storage: m[0..8] flat, row-major.
        //   m[0]=m00  m[1]=m01  m[2]=m02
        //   m[3]=m10  m[4]=m11  m[5]=m12
        //   m[6]=m20  m[7]=m21  m[8]=m22
        // Default constructor produces identity (verified via the
        // AYMath source: Float3x3() == Float3x3::identity()).
        Sprite s;
        // Diagonal == 1
        CHECK(std::fabs(s.worldMatrix.m[0] - 1.0f) < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[4] - 1.0f) < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[8] - 1.0f) < 1e-6f);
        // Off-diagonal == 0
        CHECK(std::fabs(s.worldMatrix.m[1])         < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[2])         < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[3])         < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[5])         < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[6])         < 1e-6f);
        CHECK(std::fabs(s.worldMatrix.m[7])         < 1e-6f);
    }

    // ----- P3J.8 / A-8 batch-state coverage (design.md §13.32) -----
    //
    // Six cases lock Sprite behavior when many sprites are managed
    // together in a std::vector<Sprite>. No new helper is added
    // (this is "verify what we have", mirroring P3I.3's A-2
    // reshape to test-only).

    TEST_CASE(BatchSort_PackedSortKeyAscending) {
        std::vector<Sprite> v;
        v.reserve(4);
        Sprite a; a.layer = 2; v.push_back(a);
        Sprite b; b.layer = 0; v.push_back(b);
        Sprite c; c.layer = 3; v.push_back(c);
        Sprite d; d.layer = 1; v.push_back(d);
        std::stable_sort(v.begin(), v.end(),
                         [](const Sprite& x, const Sprite& y) {
                             return x.packedSortKey() < y.packedSortKey();
                         });
        CHECK_INT_EQ(static_cast<int>(v[0].layer), 0);
        CHECK_INT_EQ(static_cast<int>(v[1].layer), 1);
        CHECK_INT_EQ(static_cast<int>(v[2].layer), 2);
        CHECK_INT_EQ(static_cast<int>(v[3].layer), 3);
    }

    TEST_CASE(BatchSort_SameLayer_SortBySortingKey) {
        std::vector<Sprite> v;
        v.reserve(4);
        Sprite a; a.layer = 5; a.sortingKey = 0;  v.push_back(a);
        Sprite b; b.layer = 5; b.sortingKey = 30; v.push_back(b);
        Sprite c; c.layer = 5; c.sortingKey = 10; v.push_back(c);
        Sprite d; d.layer = 5; d.sortingKey = 20; v.push_back(d);
        std::stable_sort(v.begin(), v.end(),
                         [](const Sprite& x, const Sprite& y) {
                             return x.packedSortKey() < y.packedSortKey();
                         });
        CHECK_INT_EQ(static_cast<int>(v[0].sortingKey), 0);
        CHECK_INT_EQ(static_cast<int>(v[1].sortingKey), 10);
        CHECK_INT_EQ(static_cast<int>(v[2].sortingKey), 20);
        CHECK_INT_EQ(static_cast<int>(v[3].sortingKey), 30);
    }

    TEST_CASE(BatchSort_SameLayerAndKey_StableKeepsInputOrder) {
        std::vector<Sprite> v;
        v.reserve(4);
        // All same layer + same sortingKey. Use m[6] (translation X)
        // as the input-order marker (per §踩坑 #25).
        Sprite a; a.layer = 5; a.sortingKey = 100; a.worldMatrix.m[6] = 10.0f; v.push_back(a);
        Sprite b; b.layer = 5; b.sortingKey = 100; b.worldMatrix.m[6] = 20.0f; v.push_back(b);
        Sprite c; c.layer = 5; c.sortingKey = 100; c.worldMatrix.m[6] = 30.0f; v.push_back(c);
        Sprite d; d.layer = 5; d.sortingKey = 100; d.worldMatrix.m[6] = 40.0f; v.push_back(d);
        std::stable_sort(v.begin(), v.end(),
                         [](const Sprite& x, const Sprite& y) {
                             return x.packedSortKey() < y.packedSortKey();
                         });
        CHECK(std::fabs(v[0].worldMatrix.m[6] - 10.0f) < 1e-6f);
        CHECK(std::fabs(v[1].worldMatrix.m[6] - 20.0f) < 1e-6f);
        CHECK(std::fabs(v[2].worldMatrix.m[6] - 30.0f) < 1e-6f);
        CHECK(std::fabs(v[3].worldMatrix.m[6] - 40.0f) < 1e-6f);
    }

    TEST_CASE(BatchFlip_ComposeIndependentAcrossEntries) {
        std::vector<Sprite> v;
        v.reserve(4);
        Sprite a; a.flip = SpriteFlip::None;       v.push_back(a);
        Sprite b; b.flip = SpriteFlip::Horizontal; v.push_back(b);
        Sprite c; c.flip = SpriteFlip::Vertical;   v.push_back(c);
        Sprite d; d.flip = SpriteFlip::Both;       v.push_back(d);
        CHECK(v[0].flip == SpriteFlip::None);
        CHECK(v[1].flip == SpriteFlip::Horizontal);
        CHECK(v[2].flip == SpriteFlip::Vertical);
        CHECK(v[3].flip == SpriteFlip::Both);
    }

    TEST_CASE(BatchLayer_5BitMask_PackedSortKey) {
        // layer = 0xFF overflows into packedSortKey's high byte.
        // The 5-bit mask (& 0x1Fu) clips layer to 0..31 for the
        // packed value; the field itself reads back 0xFFu (no clamp).
        Sprite s;
        s.layer      = 0xFFu;
        s.sortingKey = 0x00ABCDEFu;
        CHECK_INT_EQ(static_cast<int>(s.layer), 0xFF);
        // packedSortKey = (0xFF & 0x1F) << 24 | (0x00ABCDEF & 0x00FFFFFF)
        //               = 0x1F000000 | 0x00ABCDEF
        //               = 0x1FABCDEF
        CHECK_INT_EQ(static_cast<int>(s.packedSortKey()), 0x1FABCDEF);
    }

    TEST_CASE(BatchColor_DefaultWhite_AcrossEntries) {
        std::vector<Sprite> v;
        v.reserve(4);
        for (int i = 0; i < 4; ++i) v.emplace_back();
        for (const auto& s : v) {
            CHECK(std::fabs(s.colorR - 1.0f) < 1e-6f);
            CHECK(std::fabs(s.colorG - 1.0f) < 1e-6f);
            CHECK(std::fabs(s.colorB - 1.0f) < 1e-6f);
            CHECK(std::fabs(s.colorA - 1.0f) < 1e-6f);
        }
    }

TEST_SUITE_END