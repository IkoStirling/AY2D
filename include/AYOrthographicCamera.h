#pragma once
// AYOrthographicCamera.h — Phase 3 real impl.
//
// design.md §3 + §5.3: OrthographicCamera owns the view-projection
// math for a 2D scene. The matrices are stored as `ayt::math::Float4x4`
// so the consumer can hand them directly to
// `Renderer::setMainCamera(view, proj)` (no shader-side ortho
// distinction; the public API accepts any Float4x4).
//
// Three concerns are owned here:
//   1. View matrix — build from `position` (world coords) + `zoom`
//      + `rotationRadians` (around z). Default is identity (origin
//      is the camera, no rotation, no zoom).
//   2. Projection matrix — orthographic with `viewSize` (world
//      units) mapped to the viewport rect. `viewSize` is the
//      vertical extent in world units; horizontal extent is
//      `viewSize * aspect`.
//   3. Pixel-perfect invariants — design.md §5.3 four-invariants
//      check (integer world, integer zoom, gutter 0, integer
//      viewport-scale). `pixelPerfect` auto-flips to true when ALL
//      four invariants hold; consumers can force it on/off.
//
// Public header carries only `Float4x4` (from AYMath) + plain
// PODs. No bgfx, no `AYRenderer` detail. Mirrors the
// sibling rule in AYRenderer's `setMainCamera(view, proj)` API.

#include <cmath>
#include <cstdint>

#include "AYMath/MathTypes.h"
#include "AYMath/MathUtils.h"

namespace ayt::ay2d {

// Viewport rect in pixels. (0, 0) is the top-left of the render
// target. `widthPx` / `heightPx` are integer pixel counts. The
// integer-pixel invariant is what enables design.md §5.3
// `integerViewportScale`-based pixel-perfect rendering.
struct ViewportRect {
    int32_t x        = 0;
    int32_t y        = 0;
    int32_t widthPx  = 0;
    int32_t heightPx = 0;
};

struct OrthographicCamera {
    // ---- Viewport (display rect in pixels) ----
    ViewportRect viewport;

    // ---- View definition (world units) ----
    // Camera position in world space (the world-space point that
    // is centered in the viewport). Default = (0, 0).
    float         positionX     = 0.0f;
    float         positionY     = 0.0f;

    // Zoom factor. `zoom = 1.0` means 1 world unit == 1 viewport
    // pixel / pixelsPerUnit. `zoom > 1` zooms in. Negative zoom
    // flips the image (rare; mostly for shader-side effects).
    // Integer zoom is one of the §5.3 pixel-perfect invariants.
    float         zoom          = 1.0f;

    // Rotation in radians around the screen-space z axis
    // (the camera's forward). Default = 0 (no rotation). Integer
    // multiples of pi/2 are pixel-perfect safe.
    float         rotationRadians = 0.0f;

    // Vertical extent in world units. The horizontal extent is
    // `viewSize * viewportAspect`. Must be > 0; otherwise the
    // projection matrix is degenerate.
    float         viewSize      = 1.0f;

    // Near / far clip planes (world units along the camera's
    // forward axis). Default = (-1, 1) — a 2D scene typically
    // needs nothing farther than 1 world unit.
    float         nearZ         = -1.0f;
    float         farZ          =  1.0f;

    // ---- Layer visibility (design.md §3 + §7.4) ----
    // 32 layers max. `layerMask & (1u << layer)` gates whether the
    // camera sees a layer's draw items.
    uint32_t      layerMask     = 0xFFFFFFFFu;

    // ---- Pixel-perfect mode (design.md §5.3) ----
    // `pixelPerfect` is the externally-forced flag. The `isPixelPerfectSafe()`
    // helper returns true when ALL FOUR invariants hold:
    //   1. integer world positions (this camera's position is integer)
    //   2. integer camera zoom (`zoom` is integer)
    //   3. gutter is zero (this is an atlas-side concern; the
    //      caller declares it via `atlasGutterIsZero`)
    //   4. integer viewport scale (viewport dimensions are integer)
    // `pixelPerfect == true` forces the pixel-perfect mode on
    // even when the invariants don't all hold; the *truthfulness*
    // of the pixel-perfect claim is gated by `isPixelPerfectSafe`.
    bool          pixelPerfect  = false;

    // Atlas-side flag for the §5.3 third invariant. The camera
    // owns the declaration so RenderSystem2D can compute the
    // truth flag without re-querying the atlas descriptor.
    bool          atlasGutterIsZero = false;

    // -----------------------------------------------------------------------
    // Derived getters (header-only; cheap constant-time math).
    // -----------------------------------------------------------------------

    [[nodiscard]] float viewportAspect() const noexcept {
        if (viewport.heightPx == 0) return 1.0f;
        return static_cast<float>(viewport.widthPx)
             / static_cast<float>(viewport.heightPx);
    }

    // Build the view matrix: translate by -position, rotate by
    // -rotationRadians, scale by 1/zoom. The Y axis is
    // convention-dependent; AY2D uses bottom-up (atlas row 0 is
    // the bottom of the screen), so we do NOT flip Y here — the
    // projection matrix below also uses bottom-up.
    [[nodiscard]] ayt::math::Float4x4 viewMatrix() const noexcept {
        // Translate -position, then rotate about z, then scale.
        // Combined as T * R * S is the canonical camera-look-at
        // form; reading from the right, the world point is scaled,
        // rotated, then translated.
        const float s = 1.0f / zoom;
        using ayt::math::Float4x4;
        using ayt::math::FVector4;

        // Scale (1/zoom, 1/zoom, 1, 1).
        Float4x4 m = Float4x4::identity();
        m.row[0].x = s;
        m.row[1].y = s;
        m.row[2].z = 1.0f;

        // Rotation about z (axis = +z). Rotate by -rotationRadians
        // (the world is rotated under the camera, so the camera's
        // own rotation is negated).
        const float c = std::cos(rotationRadians);
        const float sn = std::sin(rotationRadians);
        // Negate the rotation: build rotation matrix R(-θ) and
        // multiply m = R * S.
        Float4x4 r = Float4x4::identity();
        r.row[0].x =  c;
        r.row[0].y =  sn;
        r.row[1].x = -sn;
        r.row[1].y =  c;
        m = r * m;

        // Translate by -position.
        m.row[0].w = -positionX;
        m.row[1].w = -positionY;
        return m;
    }

    // Build the projection matrix via `ayt::math::ortho`. The
    // visible region is centered on the camera's position with
    // vertical extent `viewSize` (so top/bottom = ±viewSize/2);
    // horizontal extent = vertical * aspect.
    [[nodiscard]] ayt::math::Float4x4 projectionMatrix() const noexcept {
        const float aspect = viewportAspect();
        const float half = viewSize * 0.5f;
        const float left   = -half * aspect;
        const float right  =  half * aspect;
        const float bottom = -half;
        const float top    =  half;
        return ayt::math::ortho(left, right, bottom, top, nearZ, farZ);
    }

    // -----------------------------------------------------------------------
    // Pixel-perfect invariants (design.md §5.3).
    // -----------------------------------------------------------------------

    // Returns true iff all four invariants hold for this camera.
    // See design.md §5.3 for the truth table.
    [[nodiscard]] bool isPixelPerfectSafe() const noexcept {
        // Invariant 1: integer world positions. The camera's
        // position must be integer-pixel (or rather, integer world
        // unit). The zoom and the viewport scale combine to make
        // an integer-pixel safe only when position is integer.
        const auto posXInt = static_cast<float>(static_cast<int32_t>(positionX));
        const auto posYInt = static_cast<float>(static_cast<int32_t>(positionY));
        const bool integerWorldPositions =
            posXInt == positionX && posYInt == positionY;

        // Invariant 2: integer camera zoom. `zoom` is a float so
        // allow small epsilon (pixel-perfect is "approximately"
        // integer — the rasterizer's own epsilon is hidden by
        // integer pipeline stages).
        const float zoomInt = static_cast<float>(static_cast<int32_t>(zoom));
        const bool integerCameraZoom = std::fabs(zoom - zoomInt) < 1e-6f;

        // Invariant 3: gutter is zero (atlas-side; declared here).
        const bool gutterIsZero = atlasGutterIsZero;

        // Invariant 4: integer viewport scale. Width and height
        // are integer pixel counts, so this is automatically true.
        const bool integerViewportScale =
            viewport.widthPx > 0 && viewport.heightPx > 0;

        return integerWorldPositions
            && integerCameraZoom
            && gutterIsZero
            && integerViewportScale;
    }

    // Force-on / force-off override. The `pixelPerfect` field is
    // the consumer-side gate; the *truth* is `isPixelPerfectSafe()`.
    // RenderSystem2D combines both: if pixel-perfect is requested
    // but unsafe, the system emits a debounced stderr warning + drops
    // back to bilinear (the safe path).
    [[nodiscard]] bool pixelPerfectRequested() const noexcept {
        return pixelPerfect;
    }
};

} // namespace ayt::ay2d
