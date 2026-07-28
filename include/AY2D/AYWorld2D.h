#pragma once
// AYWorld2D.h — Phase 0 placeholder (no implementation; design.md §3).
//
// Real definition lands in Phase 2+. This header exists in Phase 1 ONLY to:
//   1. Validate the public-header bgfx-leak guard (design.md §11.2).
//   2. Reserve the include path so downstream consumers can begin prototype
//      includes against `ayt::ay2d::World2D` without churn.
#include <cstdint>
namespace ayt::ay2d {
struct World2D {
    uint64_t resourceEpoch = 0;  // see design.md §3.4
};
} // namespace ayt::ay2d
