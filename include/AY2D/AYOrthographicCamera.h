#pragma once
// AYOrthographicCamera.h — Phase 0 placeholder (no implementation; design.md §3).
//
// Real definition lands in Phase 1+. Camera matrices are uploaded via
// `Renderer::setMainCamera(view, proj)` (no shader-side ortho camera
// distinction; the public API accepts any Float4x4 — design.md §2.3 L-7).
#include <cstdint>
namespace ayt::ay2d {
struct OrthographicCamera {
    uint32_t layerMask       = 0xFFFFFFFFu;  // 32 layers
    bool     pixelPerfect    = false;       // design.md §5.3 four-invariants
};
} // namespace ayt::ay2d
