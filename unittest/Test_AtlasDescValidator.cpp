// Test_AtlasDescValidator.cpp - P3J.3 / A-10 isValidAtlasDesc coverage.
//
// design.md §13.27. The AtlasDesc validator shipped Phase 2 with
// 6 reject conditions + 1 happy path. The existing Test_TileSamplerUV
// suite locks 2 of the rejects plus the happy path; P3J.3 promotes
// the remaining 4 rejects + boundary conditions (gutter boundary,
// non-divisible atlas/tile ratio, default filter) to a dedicated
// suite.

#include "AYAtlasDesc.h"

#include "AYTest.h"

using namespace ayt::ay2d;

TEST_SUITE(AtlasDescValidatorSuite)

    TEST_CASE(AtlasDesc_Default_IsInvalid_AllFieldsZero) {
        // Default-constructed AtlasDesc has all zero fields.
        AtlasDesc d{};
        CHECK_FALSE(isValidAtlasDesc(d));
    }

    TEST_CASE(AtlasDesc_ZeroAtlasDimension_IsInvalid) {
        AtlasDesc d{};
        d.atlasWidthTexels  = 64u;
        d.atlasHeightTexels = 0u;     // zero height
        d.tileWidthTexels   = 16u;
        d.tileHeightTexels  = 16u;
        d.tilesPerRow       = 4u;
        d.tilesPerColumn    = 4u;
        CHECK_FALSE(isValidAtlasDesc(d));

        d.atlasWidthTexels  = 0u;     // zero width
        d.atlasHeightTexels = 64u;
        CHECK_FALSE(isValidAtlasDesc(d));
    }

    TEST_CASE(AtlasDesc_ZeroTileDimension_IsInvalid) {
        AtlasDesc d{};
        d.atlasWidthTexels  = 64u;
        d.atlasHeightTexels = 64u;
        d.tileWidthTexels   = 0u;     // zero tile width
        d.tileHeightTexels  = 16u;
        d.tilesPerRow       = 4u;
        d.tilesPerColumn    = 4u;
        CHECK_FALSE(isValidAtlasDesc(d));

        d.tileWidthTexels   = 16u;
        d.tileHeightTexels  = 0u;     // zero tile height
        CHECK_FALSE(isValidAtlasDesc(d));
    }

    TEST_CASE(AtlasDesc_ZeroTilesPerRowColumn_IsInvalid) {
        AtlasDesc d{};
        d.atlasWidthTexels  = 64u;
        d.atlasHeightTexels = 64u;
        d.tileWidthTexels   = 16u;
        d.tileHeightTexels  = 16u;
        d.tilesPerRow       = 0u;     // zero tilesPerRow
        d.tilesPerColumn    = 4u;
        CHECK_FALSE(isValidAtlasDesc(d));

        d.tilesPerRow       = 4u;
        d.tilesPerColumn    = 0u;     // zero tilesPerColumn
        CHECK_FALSE(isValidAtlasDesc(d));
    }

    TEST_CASE(AtlasDesc_NonDivisibleAtlasTileRatio_IsInvalid) {
        // atlasWidth=100, tileWidth=7, tilesPerRow=14: 7*14=98 != 100.
        AtlasDesc d{};
        d.atlasWidthTexels   = 100u;
        d.atlasHeightTexels  = 100u;
        d.tileWidthTexels    = 7u;
        d.tileHeightTexels   = 7u;
        d.tilesPerRow        = 14u;     // 7*14 = 98, not 100
        d.tilesPerColumn     = 14u;
        d.gutter             = 0u;      // zero gutter passes L-8 (0 < 7/2)
        CHECK_FALSE(isValidAtlasDesc(d));

        // Same shape but tilesPerRow = 14 (still 7*14 = 98 != 100).
        // Validates exact-match discipline (no tolerance).
        d.tilesPerRow = 14u;
        CHECK_FALSE(isValidAtlasDesc(d));
    }

    TEST_CASE(AtlasDesc_GutterTooLarge_IsInvalid) {
        // tile 16x16. L-8 boundary: gutter < min/2 = 8.
        AtlasDesc d{};
        d.atlasWidthTexels   = 64u;
        d.atlasHeightTexels  = 64u;
        d.tileWidthTexels    = 16u;
        d.tileHeightTexels   = 16u;
        d.tilesPerRow        = 4u;
        d.tilesPerColumn     = 4u;

        // gutter == 8 (= min/2): rejected (boundary).
        d.gutter = 8u;
        CHECK_FALSE(isValidAtlasDesc(d));

        // gutter == 7 (< min/2): valid (largest valid gutter).
        d.gutter = 7u;
        CHECK(isValidAtlasDesc(d));
    }

    TEST_CASE(AtlasDesc_HappyPath_BilinearDefault_ReturnsTrue) {
        AtlasDesc d{};
        d.atlasWidthTexels   = 64u;
        d.atlasHeightTexels  = 64u;
        d.tileWidthTexels    = 16u;
        d.tileHeightTexels   = 16u;
        d.tilesPerRow        = 4u;
        d.tilesPerColumn     = 4u;
        d.gutter             = 1u;
        CHECK(isValidAtlasDesc(d));
        // Sanity reads on locked defaults.
        CHECK(d.filter == TileFilter::Bilinear);
        CHECK(d.gutter == 1u);
    }

    TEST_CASE(AtlasDesc_FilterAndWrapDefaultsMatch_Lock) {
        AtlasDesc d{};
        // §5.5 default: Bilinear + Clamp + Clamp.
        CHECK(d.filter == TileFilter::Bilinear);
        CHECK(d.wrapU  == TileWrap::Clamp);
        CHECK(d.wrapV  == TileWrap::Clamp);
        // Enum underlying-type discipline (uint8_t; design.md §5.5).
        static_assert(sizeof(TileFilter) == 1u,
                      "TileFilter must be 1B (uint8_t).");
        static_assert(sizeof(TileWrap) == 1u,
                      "TileWrap must be 1B (uint8_t).");
    }

TEST_SUITE_END