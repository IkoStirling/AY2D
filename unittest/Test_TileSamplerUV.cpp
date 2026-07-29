// Test_TileSamplerUV.cpp — Phase 2 sampler-math unit tests.
//
// design.md §5.1 + §5.2 + §5.3. Pure-CPU math, so these tests run
// without any renderer-side setup (no bgfx, no AYRenderer, no ECS).
// The render pass eventually wraps these UVs into vertex streams;
// the unit-test boundary is the math contract.

#include <cmath>
#include <cstdint>

#include "AYAtlasDesc.h"
#include "AYTileSamplerUV.h"

#include "AYTest.h"

using namespace ayt::ay2d;

namespace {

constexpr float kEps = 1e-6f;

bool feq(float a, float b) noexcept {
    return std::fabs(a - b) < kEps;
}

} // namespace

TEST_SUITE(TileSamplerUVSuite)

    TEST_CASE(DefaultDescReturnsZeroUV) {
        AtlasDesc desc;  // all zeros
        const TileUV uv = tileUV(0u, desc);
        CHECK(feq(uv.uMin, 0.0f));
        CHECK(feq(uv.uMax, 0.0f));
        CHECK(feq(uv.vMin, 0.0f));
        CHECK(feq(uv.vMax, 0.0f));
    }

    TEST_CASE(AtlasValidationRuleHalfTileGutter) {
        AtlasDesc d;
        d.atlasWidthTexels  = 64;
        d.atlasHeightTexels = 64;
        d.tileWidthTexels   = 32;
        d.tileHeightTexels  = 32;
        d.tilesPerRow       = 2;
        d.tilesPerColumn    = 2;

        // gutter < min(tileWidth, tileHeight) / 2 = 16
        d.gutter = 1;
        CHECK(isValidAtlasDesc(d));
        d.gutter = 15;
        CHECK(isValidAtlasDesc(d));

        // gutter == 16 violates the < half rule.
        d.gutter = 16;
        CHECK_FALSE(isValidAtlasDesc(d));

        // Misaligned tile count.
        d.gutter = 1;
        d.tilesPerRow = 3;
        CHECK_FALSE(isValidAtlasDesc(d));
    }

    TEST_CASE(HalfTexelOffsetIsApplied) {
        AtlasDesc d;
        d.atlasWidthTexels  = 32;
        d.atlasHeightTexels = 32;
        d.tileWidthTexels   = 32;
        d.tileHeightTexels  = 32;
        d.tilesPerRow       = 1;
        d.tilesPerColumn    = 1;
        d.gutter            = 0;

        const TileUV uv = tileUV(0u, d);

        // Atlas occupies the whole 0..1 UV space; tile id 0 with no
        // gutter sits at the corners of the atlas. Half-texel center
        // adds 0.5/32 = 0.015625 to each side.
        CHECK(feq(uv.uMin, 0.5f / 32.0f));
        CHECK(feq(uv.uMax, 1.0f - 0.5f / 32.0f));
        CHECK(feq(uv.vMin, 0.5f / 32.0f));
        CHECK(feq(uv.vMax, 1.0f - 0.5f / 32.0f));
    }

    TEST_CASE(GutterShrinksTileEdgeUV) {
        AtlasDesc d;
        d.atlasWidthTexels  = 64;
        d.atlasHeightTexels = 64;
        d.tileWidthTexels   = 32;
        d.tileHeightTexels  = 32;
        d.tilesPerRow       = 2;
        d.tilesPerColumn    = 2;
        d.gutter            = 2;

        // Tile id 0 is the bottom-left tile. With gutter=2, each
        // tile edge shrinks by 2/64 = 0.03125 on each side (plus the
        // half-texel center offset 0.5/64 — the upper edge moves
        // -0.5/64 because the texel center sits at the integer
        // coordinate, see design.md §5.1 / L-7).
        const TileUV uv = tileUV(0u, d);

        CHECK(feq(uv.uMin, 2.0f / 64.0f + 0.5f / 64.0f));
        CHECK(feq(uv.uMax, 32.0f / 64.0f - 2.0f / 64.0f - 0.5f / 64.0f));
        // V matches U: row 0 = atlas bottom; both edges follow the
        // gutter + half-texel rule.
        CHECK(feq(uv.vMin, 2.0f / 64.0f + 0.5f / 64.0f));
        CHECK(feq(uv.vMax, 32.0f / 64.0f - 2.0f / 64.0f - 0.5f / 64.0f));
    }

    TEST_CASE(TileIdLaysOutAcrossAtlas) {
        AtlasDesc d;
        d.atlasWidthTexels  = 64;
        d.atlasHeightTexels = 64;
        d.tileWidthTexels   = 32;
        d.tileHeightTexels  = 32;
        d.tilesPerRow       = 2;   // 2 columns
        d.tilesPerColumn    = 2;
        d.gutter            = 0;

        // Tile id 1 is column=1, row=0. Both tile id 0 and tile id 1
        // share row 0 (atlas bottom). V convention is bottom-up:
        // row 0 maps to atlas v ∈ [0, tileHeight/atlasH].
        const TileUV uv1 = tileUV(1u, d);
        // Right edge at 1 - 0.5/64 (half-texel inside the atlas right
        // border). Left edge at tileWidth/atlasW + 0.5/64 (half-texel
        // inside the inner column boundary).
        CHECK(feq(uv1.uMin, 32.0f / 64.0f + 0.5f / 64.0f));
        CHECK(feq(uv1.uMax, 64.0f / 64.0f - 0.5f / 64.0f));
        // V: row 0 stays in atlas bottom half.
        CHECK(feq(uv1.vMin, 0.5f / 64.0f));
        CHECK(feq(uv1.vMax, 32.0f / 64.0f - 0.5f / 64.0f));
    }

    TEST_CASE(PixelPerfectSafeRequiresAllFourInvariants) {
        // design.md §5.3: "pixel art" requires four invariants; if
        // ANY one breaks, artifacts appear.
        PixelPerfectInvariants good{};
        good.integerWorldPositions = true;
        good.integerCameraZoom     = true;
        good.gutterIsZero          = true;
        good.integerViewportScale  = true;
        CHECK(isPixelPerfectSafe(good));

        PixelPerfectInvariants badGutter = good;
        badGutter.gutterIsZero = false;
        CHECK_FALSE(isPixelPerfectSafe(badGutter));

        PixelPerfectInvariants badZoom = good;
        badZoom.integerCameraZoom = false;
        CHECK_FALSE(isPixelPerfectSafe(badZoom));
    }

TEST_SUITE_END
