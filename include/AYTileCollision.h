#pragma once
// AYTileCollision.h — Phase 0 placeholder.
//
// Real definition lands in Phase 5+ alongside ITileCollisionQuery
// (design.md §8.1). Until then this header declares only the
// CollisionFlags bitmask + the operator set that the unit tests
// (`unittest/Test_CollisionFlags.cpp`) consume.
//
// Once §8 implementation lands, additional types — `Ray2D`,
// `RaycastHit2D`, and the `ITileCollisionQuery` interface itself —
// will be added here. Consumers of those types can switch from
// direct includes (used in Phase 0 / 1 stubs) to umbrella `<AY2D.h>`
// immediately, because Phase 0/1 exposes the same `ayt::ay2d::` types.

#include <cstdint>

namespace ayt::ay2d {

// design.md §8.1 bitflags. Empty (1<<6) is the explicit "tile has no
// collision"; None (0) is the default-constructed zero meaning "unset
// / unknown" and MUST NOT be used to mean "empty". The loader is
// responsible for normalizing unknown bits to Empty before exposing
// them via ITileCollisionQuery (Phase 5+).
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

} // namespace ayt::ay2d
