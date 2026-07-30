cmake_minimum_required(VERSION 3.20)

project(AY2D
    VERSION 0.1.0
    DESCRIPTION "AY2D — 2D world / tilemap / sprite / orthographic camera subsystem (Phase 0 docs-only)"
)

# ----------------------------------------------------------------------------
# Module-level guard: AY2D is Phase 0 docs-only as of v0.1 (2026-07-27).
# The target list below is reserved for Phase 1+ (see design.md §11).
# Building this project standalone is NOT supported until root
# `add_subdirectory(AYRuntime/AY2D)` lands behind the `AY_ENABLE_AY2D`
# CMake option in d:/Projects/CMakeLists.txt.
#
# What is wired here in Phase 0:
#   1. The library target (`AY2D`) with NO source files — `add_library(... STATIC)`,
#      empty SOURCES, so subsequent `target_*` calls do not silently disappear.
#   2. The public-header bgfx-leak guard (design.md §11.2 / audit F-5).
#   3. Stub target for the bgfx-leak check script.
#
# What is NOT wired in Phase 0:
#   - `add_subdirectory(unittest)` (no `unittest/` directory yet).
#   - Cross-module link lines (see design.md §4.2.1 cross-module PR ownership).
#   - The `RenderPassSlot::Forward2DOpaque` slot (Phase 1+ cross-module PR).
#   - The `DrawItem::payload` extension (Phase 2+ cross-module PR).
# ----------------------------------------------------------------------------

set(MODULE_NAME AY2D)

# Phase 2 ships CPU-side .cpp files. Each .cpp that knows about
# the chunk-source bridge, LRU cache math, or any future Phoskia
# wiring lives here. Header-only types (TileCoord, ChunkData,
# AtlasDesc, ITilemapChunkSource, TileSamplerUV) are NOT compiled;
# they are picked up via the AY2D INTERFACE include path by every
# consumer that does `target_link_libraries(... AY2D)`.
set(SRC_FILES
    src/AYTilemap.cpp                    # loadChunkFromSource + chunk-width-mismatch guard
    src/AYInMemoryTilemapChunkSource.cpp # LRU cache + put/get/cancel/evict
    src/AYWorld2D.cpp                    # addTilemap / removeTilemap / swapTilemap + resourceEpoch
    src/AYWorld2DSnapshot.cpp            # P3H.2: World2DSnapshot::build (value-type)
    src/AYTilemapAnimation.cpp           # Phase 3B: tick + resolveAnimatedTileId
    src/AYTilemapBatch.cpp               # Phase 3D: setTileRange / fillTile / copyTileRange + gridRect
    src/AYSpriteCulling.cpp               # Phase 3F: buildSpriteScene (AABB + layer + stable_sort)
    src/AYTileCollision.cpp              # Phase 5: Ray2D::pointAt out-of-line
    src/AYTilemapCollisionAdapter.cpp    # Phase 5: ITileCollisionQuery concrete adapter
)

# Phase 2 is the first time the library type flips to STATIC. The
# `if(NOT SRC_FILES)` branch kept Phase 0 / Phase 1+ stub work
# working without bundling a no-op translation unit; with real .cpp
# files in SRC_FILES the STATIC form mirrors every other AYRuntime/*
# entry (static archive). The library type flip is the visible
# signal that we have left the docs-only exit and entered Phase 2
# finite-tilemap MVP (design.md §11 Phase 1 exit -> Phase 2 entry).
add_library(${MODULE_NAME} STATIC ${SRC_FILES})

target_include_directories(${MODULE_NAME} PUBLIC
    # Public headers live flat under `include/` (matches sibling
    # convention: AYPhysics/AYRenderer put public headers directly in
    # `include/` with no subdirectory). Consumers write
    # `#include "AYWorld2D.h"` (no namespace prefix in the include path)
    # or use the umbrella `#include <AY2D.h>`.
    #
    # PUBLIC (not INTERFACE) is required: when the target type flips
    # from INTERFACE to STATIC at Phase 2 entry (this commit),
    # PUBLIC makes the include path available to (a) the AY2D target's
    # own source files when they `#include "AY<X>.h"`, AND (b) any
    # consumer (unittest, future AYEntity / AYResource consumers).
    # INTERFACE alone would (1) make STATIC targets forget self-include,
    # since an INTERFACE-only target has no compilable sources of its
    # own. Locked in design.md §2.2 / §4 boundary rules.
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Phase 2 keeps the AYFoundation PUBLIC dep list minimal:
# AY2D's pure-CPU implementation is intentionally self-contained
# (std::* containers only). Phase 3A added `AYMath` for
# `OrthographicCamera` (`ayt::math::Float4x4` view/proj matrices,
# design.md §3); Phase 3E reuses the same `AYMath` dep for the
# new `ayt::math::FVector2` surface in `AYTileMath.h` / world-
# coord `setTile` overloads.
#
# Phase 3+ (cross-module PRs, §4.2.1) adds:
#   * AYResource public headers (for .aytilemap loader wiring)
#   * AYRenderer public headers (for DrawItem::payload pattern)
#   * AYEntity public headers (for AYTileMapComponent)
# Phase 5+ adds AYPhysics for ITileCollisionQuery impls.
# Stay within design.md §2.3 allowed deps. Never AYUI. Never bgfx.
#
# The PUBLIC keyword on `AYMath` is what carries both:
#   * the link-time symbol reference (so future consumers can use
#     `Float3x3` / `FVector2` through the AY2D umbrella), AND
#   * the AYMath PUBLIC include path (`aymath/MathTypes.h`) so that
#     AY2D's own .cpp files can include the math header.
#
# P3G.1 partial (slice 4): also PUBLIC-link `AYLog` so the
# non-LRU `setBudget` warning can fire via `ayt::log::warn`.
# AYLog is in the §2.3 allowed-deps list (P3D sibling AYResource
# already uses it for "Invalid source path" + "Skeleton v1
# header truncated" warnings; mirrors the engine-wide pattern).
target_link_libraries(${MODULE_NAME} PUBLIC AYMath AYLog)

# The static library is now consumed by both production callers AND
# AY2D_Tests. The guard target is wired here so a `cmake --build .`
# inside AY2D always runs the bgfx-leak check. (This used to be a
# "Phase 1+ deferred" comment; the INTERFACE->STATIC flip re-enables
# the wire-up since add_dependencies on a STATIC target is valid.)
add_dependencies(${MODULE_NAME} ay2d_check_no_bgfx_in_public_headers)

# ----------------------------------------------------------------------------
# Phase 1 guard target: ay2d_check_no_bgfx_in_public_headers
# Audit finding F-5: the engine currently has NO automated "no bgfx in
# public headers" enforcement. The existing Test_PublicHeaderSurface in
# AYRenderer is compile-only and does not grep or walk the include tree.
#
# This target produces a non-zero exit code if ANY header under
# include/AY2D/**/*.h (or .hpp) references `<bgfx/bgfx.h>`, `<bgfx/*.h>`,
# or `<bx/*.h>`, OR if a public header's transitive clang -M dependency
# tree contains any of those paths.
#
# Wired unconditionally so the CI can opt in via
# `cmake --build . --target ay2d_check_no_bgfx_in_public_headers`
# even before root CMakeLists mounts AY2D.
#
# Phase 0 headerless state: include/AY2D/ does not yet exist, so the guard
# trivially passes. Phase 1 populates include/AY2D/ and the guard becomes
# load-bearing.
# ----------------------------------------------------------------------------
set(AY2D_PUBLIC_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_custom_target(ay2d_check_no_bgfx_in_public_headers
    COMMAND ${CMAKE_COMMAND}
        -DAYTGT_PUBLIC_HEADER_DIR="${AY2D_PUBLIC_HEADER_DIR}"
        -DAYTGT_MODULE_NAME="${MODULE_NAME}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoBgfxInPublicHeaders.cmake"
    COMMENT "AY2D public-header bgfx/bx leak guard (audit F-5)"
    VERBATIM
)

# Phase 0 note: `add_dependencies` on an INTERFACE library is not allowed
# (INTERFACE libraries have no build step to attach to). When the module
# flips to `STATIC ${SRC_FILES}` in Phase 1+, the guard target is wired
# via `add_dependencies(${MODULE_NAME} ay2d_check_no_bgfx_in_public_headers)`
# here. For now, callers invoke the guard by its own target name:
#   `cmake --build . --target ay2d_check_no_bgfx_in_public_headers`
# which is exactly how a CI step would invoke it anyway.
#
# Stub note kept here so the Phase 1 PR does not need to think about this.

# ----------------------------------------------------------------------------
# AYTest integration (Phase 1+)
#
# AY2D follows the AYPhysics sibling pattern (see
# AYRuntime/AYPhysics/unittest/CMakeLists.txt and AY2D CLAUDE.md "新增模块
# 要求"):

#   * `enable_testing()` so CTest can find our test executables.
#   * `add_subdirectory(unittest)` gated on the directory existing, so a
#     sub-tree without any Test_*.cpp is silently skipped (it would
#     otherwise generate a "directory has no CMakeLists.txt" CMake
#     warning).
#   * Each Test_*.cpp is a stand-alone TU linked into a single
#     AY2D_Tests executable that pulls in AYTest + AY2D (and only the
#     dependencies a given test actually needs).
# ----------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unittest/CMakeLists.txt")
    enable_testing()
    add_subdirectory(unittest)
else()
    # Phase 0 stub state — `unittest/` does not yet exist. Phase 1+ adds the
    # directory alongside the first real Test_*.cpp. Until then, do not
    # call enable_testing() at the module scope (root already enabled it
    # globally via its `enable_testing()` line before any
    # add_subdirectory(... AYRuntime/AY2D ...)).
    message(STATUS
        "AY2D — unittest/ not present yet; "
        "skipping enable_testing/add_subdirectory for Phase 0.")
endif()

message(STATUS "AY2D v0.1.14 (P3G.1 partial counter scaffolding + log warning) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
