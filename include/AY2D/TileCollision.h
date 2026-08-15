#pragma once
// AY2D/TileCollision.h — Phase 5 ship: collision types + interface + adapters.
//
// design.md §8.1: this header is the public Phase 5 surface for
// tile collision queries. It expands the Phase 0 placeholder
// (`CollisionFlags` + bitwise operators) with the three types the
// doc has carried since Phase 0:
//
//   * `Ray2D`         — a 2D ray (origin + unit-length direction +
//                        minimum parametric distance).
//   * `RaycastHit2D`  — result of a raycast query.
//   * `ITileCollisionQuery` — abstract base class consumed by the
//     AYPhysics 2D backend (§4.2.1 cross-module PR) and any
//     user-defined resolver. Ships three virtual methods:
//     `flagsAt` / `isBlocked` / `raycast`.
//
// Concrete adapters (e.g. `TilemapCollisionQueryAdapter` in
// `AY2D/TilemapCollisionAdapter.h`) implement the interface by
// delegating to a `Tilemap` cell. Production 2D physics
// integration is deferred per §8.2 — AY2D ships **no** collision
// resolver (only the interface + a thin adapter that surfaces
// the placeholder `Tilemap::flagsAtRaw`).
//
// §13.PF pre-flight retractions (2026-07-30):
//   * C6 / `Tilemap::flagsAtRaw` MUST return
//     `CollisionFlags::Empty` (1<<6) for no-data cells, NOT
//     `CollisionFlags::None` (0). Body fixed in Phase 5.
//   * C8 / cell type deviation: the shipped interface uses
//     `TileCoord` (consistent with all AY2D code) instead of
//     `IVector2` per the §8.1 doc text. §16.4 permits int<->int
//     equivalence; the deviation is logged in §13.PF.
//   * C6 / default `isBlocked` formula corrected from
//     `flagsAt(c) & mask != Empty` (always true) to
//     `flagsAt(c) != Empty`.

#include <cstdint>

#include "AYMath/MathTypes.h"

#include "AY2D/TileCoord.h"

namespace ayt::ay2d {

// design.md §8.1 bitflags. Empty (1<<6) is the explicit "tile has no
// collision"; None (0) is the default-constructed zero meaning "unset
// / unknown" and MUST NOT be used to mean "empty". The loader is
// responsible for normalizing unknown bits to Empty before exposing
// them via ITileCollisionQuery.
enum class CollisionFlags : uint32_t {
    None      = 0,
    Solid     = 1u << 0,
    OneWay    = 1u << 1,
    Slope_L   = 1u << 2,
    Slope_R   = 1u << 3,
    Hazard    = 1u << 4,
    Ladder    = 1u << 5,
    Empty     = 1u << 6,
    // Reserved bits 7..31 for future tile meta (sound, material, etc.).
};

// Bitwise operators are required because CollisionFlags is used as a
// bitmask in production paths (e.g. `flags & Solid`).
inline constexpr CollisionFlags operator|(CollisionFlags a, CollisionFlags b) noexcept {
    return static_cast<CollisionFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline constexpr CollisionFlags operator&(CollisionFlags a, CollisionFlags b) noexcept {
    return static_cast<CollisionFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline constexpr CollisionFlags operator^(CollisionFlags a, CollisionFlags b) noexcept {
    return static_cast<CollisionFlags>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}
inline constexpr CollisionFlags operator~(CollisionFlags a) noexcept {
    return static_cast<CollisionFlags>(~static_cast<uint32_t>(a));
}
inline CollisionFlags& operator|=(CollisionFlags& a, CollisionFlags b) noexcept { a = a | b; return a; }
inline CollisionFlags& operator&=(CollisionFlags& a, CollisionFlags b) noexcept { a = a & b; return a; }

// ---------------------------------------------------------------------------
// Phase 5 additions (previously placeholder; types defined per §8.1 doc block).
// ---------------------------------------------------------------------------

// 2D ray used for tile queries. Phase 5 keeps the struct local to
// AY2D; when the 2D physics backend ships (§8.3), consumers may
// switch to `ayt::physics::Ray2D` via a deprecation alias.
struct Ray2D {
    ayt::math::FVector2 origin    { 0.0f, 0.0f };
    // Direction is **not** validated as unit-length here; callers
    // are responsible for normalising (per §8.1 doc). The
    // placeholder adapter ignores the magnitude today (always
    // miss); a future axis-aligned tile-grid walker will assert.
    ayt::math::FVector2 direction { 1.0f, 0.0f };
    float               tMin      = 0.0f;

    // Sample the ray at parametric distance `t` (>= tMin). The
    // caller multiplies `direction` by `t` and adds `origin`.
    // Defined in `src/AYTileCollision.cpp` (kept out-of-line so
    // future SIMD / hardware-quad fast paths do not require
    // header churn).
    [[nodiscard]] ayt::math::FVector2 pointAt(float t) const noexcept;
};

// Raycast result. `hit == false` is the default-constructed
// sentinel; the loader is responsible for normalizing unknown
// cells to `Empty` before exposing them via this type.
struct RaycastHit2D {
    bool               hit      = false;
    float              t        = 0.0f;     // along `ray.direction` (>= ray.tMin)
    TileCoord          cell     { 0, 0 };
    CollisionFlags     flags    = CollisionFlags::None;
    ayt::math::FVector2 point   { 0.0f, 0.0f };  // world-space hit position
};

// Tile collision query interface (design.md §8.1). The Phase 5
// exit gate (`design.md §11 Phase 5 row`) requires: "Interface
// compiles; unit tests for cell lookup." A thin adapter
// (`TilemapCollisionQueryAdapter`) is the only in-AY2D concrete
// implementation. Production 2D physics resolver / raycast walker
// lands via cross-module PR (§4.2.1).
class ITileCollisionQuery {
public:
    virtual ~ITileCollisionQuery() = default;

    // Per-cell flag mask. The contract (§8.1, §13.PF): a cell with
    // no per-tile flag data MUST return `CollisionFlags::Empty`
    // (1<<6), not `CollisionFlags::None` (0). `None` is reserved
    // for "unset / unknown" and MUST NOT mean "empty".
    virtual CollisionFlags flagsAt(TileCoord cell) const noexcept = 0;

    // Default impl (R-3G.6 form, post pre-flight fix):
    //   `return flagsAt(c) != CollisionFlags::Empty;`
    // "Blocked" means any flag beyond the Empty bit (the §6.3
    // deterministic-default contract). Override only when the
    // product needs finer granularity (e.g. one-way platforms
    // require a separate consumer-side check).
    virtual bool isBlocked(TileCoord cell) const noexcept {
        return flagsAt(cell) != CollisionFlags::Empty;
    }

    // Raycast through the tile grid. Phase 5 placeholder behaviour
    // (per §8.1 + §11 Phase 5 row exit gate): always miss
    // (`hit=false`). A real axis-aligned tile-grid walker lands
    // with the consumer-side cross-module PR (§4.2.1 to
    // AYPhysics maintainer).
    virtual RaycastHit2D raycast(Ray2D ray, float maxDistance) const noexcept = 0;
};

} // namespace ayt::ay2d