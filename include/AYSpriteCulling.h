#pragma once
// AYSpriteCulling.h — Phase 3F sprite scene builder public surface.
//
// design.md §17.1 + §17.3: free function `buildSpriteScene` lives
// in `src/AYSpriteCulling.cpp`. The header surfaces only the
// declaration + the helper `WorldAabb` (which is in
// `AYWorldAabb.h` to keep header-only inclusion on the camera
// side). The function signature is locked (R-3F.3): it walks
// `sprites`, AABB- and layer-mask-culls against `camera`,
// stable-sorts the survivors by `packedSortKey`, and emits
// `SpriteDrawCmd` into `out`.
//
// `out` is **cleared** before the build (so repeated calls
// don't accumulate stale entries). It is the caller's
// responsibility to `reserve()` — the helper does NOT
// re-reserve to avoid coupling the allocator to the helper.

#include <cstdint>
#include <vector>

#include "AYOrthographicCamera.h"
#include "AYSprite.h"
#include "AYSpriteDrawCmd.h"

namespace ayt::ay2d {

// design.md §17.3 algorithm:
//   1. WorldAabb(camera) → rect.
//   2. Pre-cull (AABB + layerMask), collect survivor indices.
//   3. std::stable_sort by Sprite::packedSortKey().
//   4. Emit one SpriteDrawCmd per survivor.
//
// All inputs/outputs are in-AY2D types. The future
// `RenderSystem2D` cross-module PR to AYRenderer (§4.2.1)
// translates `SpriteDrawCmd` into `DrawItem::payload`.
void buildSpriteScene(const std::vector<Sprite>&  sprites,
                      const OrthographicCamera&   camera,
                      std::vector<SpriteDrawCmd>& out) noexcept;

} // namespace ayt::ay2d
