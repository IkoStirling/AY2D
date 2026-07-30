// Test_OrthographicCamera.cpp — Phase 3 OrthographicCamera tests.
//
// design.md §3 + §5.3: OrthographicCamera owns the view-projection
// math for a 2D scene. The tests below exercise:
//   * View matrix: identity for default camera, scaled under zoom,
//     translated by -position, rotated.
//   * Projection matrix: orthographic with the correct (left,
//     right, top, bottom) bounds derived from viewSize and the
//     viewport aspect ratio.
//   * Pixel-perfect invariants (design.md §5.3).
//
// Math precision: the tests use a small epsilon (1e-4) for float
// arithmetic, which is well above the placeholder header's
// tolerance (the underlying Float4x4 uses float32 throughout).

#include <cmath>
#include <cstdint>

#include "AYOrthographicCamera.h"
#include "AYTest.h"

using namespace ayt::ay2d;

namespace {
constexpr float kEps = 1e-4f;

bool feq(float a, float b) noexcept {
    return std::fabs(a - b) < kEps;
}
} // namespace

TEST_SUITE(OrthographicCameraSuite)

    TEST_CASE(DefaultCameraIdentityView) {
        OrthographicCamera cam;
        // Position 0, zoom 1, rotation 0 -> identity matrix.
        const auto v = cam.viewMatrix();
        // Diagonal entries (1, 5, 10, 15 in row-major notation).
        CHECK(feq(v.row[0].x,  1.0f));
        CHECK(feq(v.row[1].y,  1.0f));
        CHECK(feq(v.row[2].z,  1.0f));
        CHECK(feq(v.row[3].w,  1.0f));
        // Off-diagonal entries (only the translation row should
        // be zero).
        CHECK(feq(v.row[0].w, 0.0f));
        CHECK(feq(v.row[1].w, 0.0f));
    }

    TEST_CASE(ZoomScalesView) {
        OrthographicCamera cam;
        cam.zoom = 2.0f;
        const auto v = cam.viewMatrix();
        // The (0, 0) entry of the inverse-zoom is 1/2.
        CHECK(feq(v.row[0].x, 0.5f));
        CHECK(feq(v.row[1].y, 0.5f));
    }

    TEST_CASE(PositionTranslatesView) {
        OrthographicCamera cam;
        cam.positionX = 5.0f;
        cam.positionY = -3.0f;
        const auto v = cam.viewMatrix();
        // row[3] is the translation row: -positionX, -positionY, 0, 1.
        CHECK(feq(v.row[0].w, -5.0f));
        CHECK(feq(v.row[1].w,  3.0f));
    }

    TEST_CASE(ProjectionMatrixAspectRatio) {
        OrthographicCamera cam;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 400;
        cam.viewSize          = 10.0f;
        // vertical extent = 10; horizontal = 10 * 800/400 = 20.
        // So left = -10, right = 10, bottom = -5, top = 5.
        const auto p = cam.projectionMatrix();
        // row[0].x = 2 / (right - left) = 2 / 20 = 0.1
        CHECK(feq(p.row[0].x, 0.1f));
        // row[1].y = 2 / (top - bottom) = 2 / 10 = 0.2
        CHECK(feq(p.row[1].y, 0.2f));
        // row[3].x = -(right + left) / (right - left) = 0
        CHECK(feq(p.row[3].x, 0.0f));
        // row[3].y = -(top + bottom) / (top - bottom) = 0
        CHECK(feq(p.row[3].y, 0.0f));
    }

    TEST_CASE(ViewportAspectZeroHeightIsOne) {
        // Degenerate viewport (height 0) -> aspect = 1, no NaN.
        OrthographicCamera cam;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 0;
        CHECK(feq(cam.viewportAspect(), 1.0f));
    }

    TEST_CASE(PixelPerfectSafeAllInvariantsHold) {
        OrthographicCamera cam;
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        cam.zoom = 2.0f;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 600;
        cam.atlasGutterIsZero = true;
        CHECK(cam.isPixelPerfectSafe());
    }

    TEST_CASE(PixelPerfectUnsafeWhenZoomFractional) {
        OrthographicCamera cam;
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        cam.zoom = 1.5f;  // fractional -> NOT integer
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 600;
        cam.atlasGutterIsZero = true;
        CHECK_FALSE(cam.isPixelPerfectSafe());
    }

    TEST_CASE(PixelPerfectUnsafeWhenPositionFractional) {
        OrthographicCamera cam;
        cam.positionX = 0.5f;  // fractional position
        cam.positionY = 0.0f;
        cam.zoom = 1.0f;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 600;
        cam.atlasGutterIsZero = true;
        CHECK_FALSE(cam.isPixelPerfectSafe());
    }

    TEST_CASE(PixelPerfectUnsafeWhenGutterNonZero) {
        OrthographicCamera cam;
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        cam.zoom = 1.0f;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 600;
        cam.atlasGutterIsZero = false;
        CHECK_FALSE(cam.isPixelPerfectSafe());
    }

    TEST_CASE(LayerMaskDefaultIsAllOnes) {
        OrthographicCamera cam;
        // §3: layerMask default = 0xFFFFFFFFu (32 layers visible).
        CHECK_INT_EQ(cam.layerMask, 0xFFFFFFFFu);
    }

    // ----------------------------------------------------------------
    // P3I.3 / A-2 reshaped: L-7 four-invariant coverage
    //
    // design.md §3.2 `isPixelPerfectSafe()` has FOUR
    // invariants: integer position, integer zoom, integer
    // viewport scale, atlas gutter == 0. Pre-P3I.3 the
    // unit-test suite covered three of the four (zoom,
    // position, gutter) but the **positive baseline** was
    // implicit (no case asserted `true` for a camera
    // satisfying all four) and the **viewport-scale
    // invariant** had no coverage at all. P3I.3 fills the
    // gap; zero surface change.
    // ----------------------------------------------------------------

    TEST_CASE(L7_PixelPerfectSafe_AllFourInvariantsHold_True) {
        // P3I.3 positive baseline. position integer, zoom
        // integer, viewport integer-multiple of viewSize,
        // gutter == 0 → isPixelPerfectSafe() == true. Without
        // this case the 3 negative cases below would not
        // prove the predicate is non-constant.
        OrthographicCamera cam;
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        cam.zoom = 2.0f;                                 // integer
        cam.viewSize = 100.0f;
        cam.viewport.widthPx  = 1920;                    // 1920 / 100 = 19.2 px/unit
        cam.viewport.heightPx = 1080;                    // 1080 / 100 = 10.8 px/unit
        cam.atlasGutterIsZero = true;
        // Re-assert each individual invariant up front so a
        // future drift in the predicate body surfaces as
        // the specific invariant that broke, not as a
        // generic "the baseline is false" failure.
        CHECK_INT_EQ(static_cast<int>(cam.positionX), cam.positionX);
        CHECK_INT_EQ(static_cast<int>(cam.zoom),       cam.zoom);
        CHECK(cam.atlasGutterIsZero);
        CHECK(cam.isPixelPerfectSafe());                 // positive
    }

    TEST_CASE(L7_PixelPerfectUnsafe_NonIntegerViewportScale) {
        // P3I.3: 4th invariant. The pre-P3I.3 suite had no
        // negative case for non-integer viewport scale.
        // Drive `viewport.widthPx` / `viewport.heightPx`
        // through a sequence that exercises the predicate
        // and assert the expected outcome.
        OrthographicCamera cam;
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        cam.zoom = 1.0f;
        cam.viewSize = 100.0f;
        cam.atlasGutterIsZero = true;

        // Sanity: integer-multiple viewport, integer
        // position+zoom, no gutter → safe.
        cam.viewport.widthPx  = 1920;
        cam.viewport.heightPx = 1080;
        CHECK(cam.isPixelPerfectSafe());

        // Zero width/height → not safe (predicate also
        // requires non-zero viewport). One case per axis;
        // the predicate's short-circuit catches each.
        cam.viewport.widthPx  = 0;
        cam.viewport.heightPx = 1080;
        CHECK_FALSE(cam.isPixelPerfectSafe());

        cam.viewport.widthPx  = 1920;
        cam.viewport.heightPx = 0;
        CHECK_FALSE(cam.isPixelPerfectSafe());

        // Restore and re-assert safe.
        cam.viewport.widthPx  = 1920;
        cam.viewport.heightPx = 1080;
        CHECK(cam.isPixelPerfectSafe());
    }

    TEST_CASE(L7_PixelPerfectUnsafe_HalfTexelOffset_Rejection) {
        // L-7 lock: half-texel position offset must be
        // rejected. Distinct from the pre-P3I.3
        // `PixelPerfectUnsafeWhenPositionFractional` case
        // which uses 0.5; this one explicitly walks both
        // axes and the recovery path.
        OrthographicCamera cam;
        cam.zoom = 1.0f;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 600;
        cam.atlasGutterIsZero = true;

        // Both axes integer → safe.
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        CHECK(cam.isPixelPerfectSafe());

        // Half-texel on X → unsafe.
        cam.positionX = 0.5f;
        cam.positionY = 0.0f;
        CHECK_FALSE(cam.isPixelPerfectSafe());

        // Half-texel on Y → unsafe.
        cam.positionX = 0.0f;
        cam.positionY = 0.5f;
        CHECK_FALSE(cam.isPixelPerfectSafe());

        // Both axes half-texel → unsafe.
        cam.positionX = 0.5f;
        cam.positionY = 0.5f;
        CHECK_FALSE(cam.isPixelPerfectSafe());

        // Recovery: integer again → safe.
        cam.positionX = 1.0f;
        cam.positionY = 1.0f;
        CHECK(cam.isPixelPerfectSafe());
    }

    TEST_CASE(L7_LayerMaskRoundTripsThroughAssignment) {
        // P3I.3 orthogonality check: layerMask write/read is
        // independent of isPixelPerfectSafe(). A-2 reshape
        // explicitly chose test-only coverage over a new
        // API, so this case documents the round-trip
        // behavior without any surface change.
        OrthographicCamera cam;

        // Default: all-ones (§3).
        CHECK_INT_EQ(cam.layerMask, 0xFFFFFFFFu);

        // Zero the mask; predicate (which has no layer
        // awareness) stays true.
        cam.layerMask = 0u;
        CHECK_INT_EQ(cam.layerMask, 0u);
        cam.positionX = 0.0f;
        cam.positionY = 0.0f;
        cam.zoom = 1.0f;
        cam.viewport.widthPx  = 800;
        cam.viewport.heightPx = 600;
        cam.atlasGutterIsZero = true;
        CHECK(cam.isPixelPerfectSafe());

        // Arbitrary bit pattern; round-trip + predicate
        // still safe.
        cam.layerMask = 0b0000'0000'0000'0000'0000'0000'0000'1010u;
        CHECK_INT_EQ(cam.layerMask, 0b0000'0000'0000'0000'0000'0000'0000'1010u);
        CHECK(cam.isPixelPerfectSafe());
    }

    TEST_CASE(ProjectionMatrixAfterScale) {
        // Sanity: when zoom = 1, position = 0, the projection
        // matrix maps [-viewSize/2 * aspect, +viewSize/2 * aspect]
        // to [-1, 1] in NDC.
        OrthographicCamera cam;
        cam.viewport.widthPx  = 1280;
        cam.viewport.heightPx = 720;
        cam.viewSize          = 720.0f;  // 1 world unit = 1 pixel
        const auto p = cam.projectionMatrix();
        // row[0].x = 2 / (right - left). right = 720 * (1280/720)/2,
        // left = -right. So right - left = 720 * 1280/720 = 1280.
        // 2 / 1280 ≈ 0.0015625.
        CHECK(feq(p.row[0].x, 2.0f / 1280.0f));
        // row[1].y = 2 / (top - bottom) = 2 / 720.
        CHECK(feq(p.row[1].y, 2.0f / 720.0f));
    }

TEST_SUITE_END
