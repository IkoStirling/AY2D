// Test_AabbOfCell.cpp — P3D.2 aabbOfCell tests.
//
// design.md §13.17 + §16 R-3E.5 (cell-center convention).
// Regression for the naive corner-port bug: a `cellToWorld`
// returns the cell CENTER, not the corner, so a naive
// `min=cellToWorld(c); max=cellToWorld(c+{1,1})` is off by
// half a cell. The centered composition via
// `FRectangle::fromCenterExtent` is the right answer.
//
// All assertions are pure CPU math, no bgfx, no cross-module
// deps.

#include "AY2D/Tilemap.h"

#include "AYTest.h"
#include "AY2D/TileMath.h"

using namespace ayt::ay2d;

TEST_SUITE(AabbOfCellSuite)

    TEST_CASE(AabbOfCellZeroCentered) {
        // §13.17 case 1: aabbOfCell({0,0}) for a 16x16
        // tilemap (tileWidth=tileHeight=16, default origin
        // (0,0)) is centered on cell (0,0)'s center = (8, 8)
        // with extent (8, 8) -> min (0, 0), max (16, 16).
        Tilemap m;
        m.tileWidth  = 16u;
        m.tileHeight = 16u;
        const ayt::math::FRectangle aabb = m.aabbOfCell(TileCoord{0, 0});
        const float eps = 1e-5f;
        // Cell center at (8, 8); extent (8, 8) -> min (0, 0), max (16, 16).
        CHECK_FLOAT_EQ(aabb.minX, 0.0f, eps);
        CHECK_FLOAT_EQ(aabb.minY, 0.0f, eps);
        CHECK_FLOAT_EQ(aabb.maxX, 16.0f, eps);
        CHECK_FLOAT_EQ(aabb.maxY, 16.0f, eps);
        CHECK_FLOAT_EQ(aabb.width(),  16.0f, eps);
        CHECK_FLOAT_EQ(aabb.height(), 16.0f, eps);
    }

    TEST_CASE(AabbOfCellNonZeroMatchesCornerPortMinusHalfCell) {
        // §13.17 case 2 (regression): aabbOfCell({2,3}) for
        // a 32x16 tilemap centered on cell (2,3)'s center
        // = (2*32+16, 3*16+8) = (80, 56) with extent (16, 8)
        // -> min (64, 48), max (96, 64). The naive corner-
        // port would have produced min (80, 56) and max
        // (112, 72) (using cellToWorld(c) and cellToWorld(
        // c+{1,1})); our centered answer is shifted by
        // (-16, -8) = -half-cell on each axis. Tests
        // confirm the centered form.
        Tilemap m;
        m.tileWidth  = 32u;
        m.tileHeight = 16u;
        const ayt::math::FRectangle aabb = m.aabbOfCell(TileCoord{2, 3});
        const float eps = 1e-5f;
        // Center: (2*32 + 32/2, 3*16 + 16/2) = (80, 56).
        // Extent: (32/2, 16/2) = (16, 8).
        // min: (80-16, 56-8) = (64, 48). max: (80+16, 56+8) = (96, 64).
        CHECK_FLOAT_EQ(aabb.minX, 64.0f, eps);
        CHECK_FLOAT_EQ(aabb.minY, 48.0f, eps);
        CHECK_FLOAT_EQ(aabb.maxX, 96.0f, eps);
        CHECK_FLOAT_EQ(aabb.maxY, 64.0f, eps);
        CHECK_FLOAT_EQ(aabb.width(),  32.0f, eps);
        CHECK_FLOAT_EQ(aabb.height(), 16.0f, eps);
        // FRectangle::center() must round-trip to the same
        // center the cell-center helper produces.
        const ayt::math::FVector2 c = aabb.center();
        CHECK_FLOAT_EQ(c.x, 80.0f, eps);
        CHECK_FLOAT_EQ(c.y, 56.0f, eps);
    }

    TEST_CASE(AabbOfCellWidthHeightMatchTileDimensions) {
        // §13.17 case 3: regardless of cell coord, the AABB
        // width / height matches tileWidth / tileHeight.
        // This is what makes the helper suitable for a
        // future phase's AABB-vs-grid sweep (R-3E.3/4 + the
        // §11.4 P3D.2 roadmap).
        Tilemap m;
        m.tileWidth  = 24u;
        m.tileHeight = 48u;
        const float eps = 1e-5f;
        for (int32_t cx = -2; cx <= 5; ++cx) {
            for (int32_t cy = -2; cy <= 5; ++cy) {
                const ayt::math::FRectangle aabb =
                    m.aabbOfCell(TileCoord{cx, cy});
                CHECK_FLOAT_EQ(aabb.width(),  24.0f, eps);
                CHECK_FLOAT_EQ(aabb.height(), 48.0f, eps);
            }
        }
    }

TEST_SUITE_END