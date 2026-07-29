#pragma once
// AYTileLoadState.h — load-state enum exposed to ECS inspectors.
//
// design.md §11 Phase 2 / F-18: TilemapComponent::loadState is a
// typed enum that ECS inspectors + an Editor Inspector use to display
// the current load progress. The enum is intentionally narrow:
//   * Unloaded — no resource request has been issued.
//   * Loading — request is in flight (chunk IO is async).
//   * Loaded  — resource handle resolved successfully.
//   * Failed  — see Failed() on Tilemap for the reason category.
//
// Nothing here knows about bgfx, AYResource, or ECS. It's a pure
// data enum consumed by ECS components + Editor UI.

#include <cstdint>

namespace ayt::ay2d {

enum class TileLoadState : uint8_t {
    Unloaded = 0,
    Loading  = 1,
    Loaded   = 2,
    Failed   = 3,
};

} // namespace ayt::ay2d
