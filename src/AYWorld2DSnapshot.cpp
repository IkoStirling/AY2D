// AYWorld2DSnapshot.cpp — P3H.2 in-AY2D snapshot builder.
//
// design.md §13.14 (P3H.2 changelog): out-of-line `build()` so
// future additions (e.g. a derived "diff vs previous snapshot"
// helper, or a windowed average for per-frame counters) do not
// require header churn.

#include "AYWorld2DSnapshot.h"

namespace ayt::ay2d {

World2DSnapshot World2DSnapshot::build(const World2D& world) noexcept {
    World2DSnapshot out;
    out.entries.reserve(world.entries.size());
    for (const auto& e : world.entries) {
        out.entries.push_back(TilemapEntryView{
            e.handle,
            e.layer,
            e.sortingKey,
        });
    }
    out.counters = world.counters.snapshot();
    return out;
}

} // namespace ayt::ay2d