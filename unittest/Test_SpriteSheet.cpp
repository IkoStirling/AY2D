// Test_SpriteSheet.cpp — P3H.1 SpriteSheet thin wrapper tests.
//
// design.md §13.18 (P3H.1 changelog) + §13.PF (C4 reshape).
// SpriteSheet is `AtlasDesc + path`; per-cell UV delegates
// to `AYTileSamplerUV::tileUV` (no new UV derivation logic).
//
// All assertions are pure CPU, no bgfx, no cross-module deps.

#include "AY2D/SpriteSheet.h"

#include "AY2D/AtlasDesc.h"
#include "AYTest.h"
#include "AY2D/TileSamplerUV.h"

using namespace ayt::ay2d;

TEST_SUITE(SpriteSheetSuite)

    TEST_CASE(UvRectMatchesTileUVForSampleTileIds) {
        // §13.18 case 1: `SpriteSheet::uvRect(tileId)` returns
        // an FRectangle whose min/max match
        // `AYTileSamplerUV::tileUV(tileId, atlas)`. The two
        // paths MUST agree exactly (same gutter + half-texel
        // math; no divergence).
        SpriteSheet sheet;
        // 32x32 atlas, 8x8 tiles, 4 cols x 4 rows, gutter=1.
        sheet.atlas.atlasWidthTexels  = 32u;
        sheet.atlas.atlasHeightTexels = 32u;
        sheet.atlas.tileWidthTexels   = 8u;
        sheet.atlas.tileHeightTexels  = 8u;
        sheet.atlas.tilesPerRow       = 4u;
        sheet.atlas.tilesPerColumn    = 4u;
        sheet.atlas.gutter            = 1u;
        sheet.texturePath             = "art/sprites/main.ayatlas";

        const float eps = 1e-5f;
        for (uint32_t tileId = 0; tileId < 16u; ++tileId) {
            const TileUV ref = tileUV(tileId, sheet.atlas);
            const ayt::math::FRectangle got = sheet.uvRect(tileId);
            CHECK_FLOAT_EQ(got.minX, ref.uMin, eps);
            CHECK_FLOAT_EQ(got.minY, ref.vMin, eps);
            CHECK_FLOAT_EQ(got.maxX, ref.uMax, eps);
            CHECK_FLOAT_EQ(got.maxY, ref.vMax, eps);
        }
    }

    TEST_CASE(StructFieldLayoutAndCopySemantics) {
        // §13.18 case 2: `SpriteSheet` holds a `std::string`
        // (the texturePath) + an `AtlasDesc` POD. Verify the
        // field layout (no hidden vtable / virtual base) +
        // copy semantics (std::string provides the copy
        // ctor; ECS component data-flow paths rely on this).
        //
        // NOTE: `std::is_trivially_copyable_v<SpriteSheet>` is
        // false (std::string has non-trivial copy), and on MSVC
        // `std::is_standard_layout_v<SpriteSheet>` may also be
        // false due to MSVC's std::string layout. We use a
        // direct field-offset check + copy semantics instead
        // of the type traits — same discipline as
        // Test_World2DSnapshot case 5.

        // Field offsets sanity check: `atlas` is the first
        // field, so its address equals the struct's address.
        SpriteSheet s{};
        CHECK_INT_EQ(reinterpret_cast<uintptr_t>(&s.atlas) -
                     reinterpret_cast<uintptr_t>(&s), 0u);

        // Default-constructed texturePath is empty.
        CHECK_TRUE(s.texturePath.empty());
        // Default-constructed atlas is the all-zero AtlasDesc;
        // `isValidAtlasDesc` returns false (zero dims).
        CHECK_FALSE(isValidAtlasDesc(s.atlas));

        // Copy semantics: SpriteSheet is copy-constructible +
        // copy-assignable via std::string. The original
        // remains untouched after the copy mutates its
        // texturePath (proves deep copy, not alias).
        SpriteSheet copy = s;
        CHECK_TRUE(copy.texturePath.empty());
        copy.texturePath = "re-assigned";
        CHECK_TRUE(copy.texturePath == "re-assigned");
        CHECK_TRUE(s.texturePath.empty());  // original untouched

        // sizeof sanity: SpriteSheet must contain at least the
        // AtlasDesc + std::string minimums. MSVC std::string is
        // typically 32 bytes; AtlasDesc is 5*4 + 1 + 1 + 1 + 1
        // = 24 bytes (with 4-byte enum alignment). Total
        // expected >= 56 bytes; tolerate the upper-bound
        // because MSVC std::string grows with SSO buffer.
        CHECK_TRUE(sizeof(SpriteSheet) >= sizeof(AtlasDesc));
        CHECK_TRUE(sizeof(SpriteSheet) >= sizeof(std::string));
    }

TEST_SUITE_END