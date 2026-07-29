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
