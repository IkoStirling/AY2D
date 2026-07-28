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

# Phase 0 ships no source files. Phase 1+ populates SRC_FILES from
# `src/*.cpp` here. Empty list is intentional and documents the Phase 0 exit.
set(SRC_FILES
    # src/AYWorld2D.cpp
    # src/AYTilemap.cpp
    # src/AYSprite.cpp
    # src/AYOrthographicCamera.cpp
)

# Library target. STATIC matches every other AYRuntime/* entry. The target is
# declared with empty sources so subsequent `target_link_libraries` /
# `target_include_directories` are valid CMake — without this, a Phase 1 PR
# would be forced to edit the parent CMakeLists at the same time, which the
# Phase 0 rules forbid (design.md §11.1).
add_library(${MODULE_NAME} STATIC ${SRC_FILES})

target_include_directories(${MODULE_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Phase 0 has no real link surface. The PUBLIC deps list below is empty by
# design. Phase 1+ adds per-design.md §2.3 allowed-dependencies list:
#   AYMath, AYIO, AYResource (interface only), AYEntity (interface only),
#   AYRenderer (PUBLIC headers only; never src/detail/*), AYShader
#   (PUBLIC headers only), AYGameLoop, AYTime. Never AYUI. Never bgfx.
target_link_libraries(${MODULE_NAME} PUBLIC
    # AYMath
    # AYIO
    # AYShader (public headers only)
    # ...
)

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
set(AY2D_PUBLIC_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/AY2D")

add_custom_target(ay2d_check_no_bgfx_in_public_headers
    COMMAND ${CMAKE_COMMAND}
        -DAYTGT_PUBLIC_HEADER_DIR="${AY2D_PUBLIC_HEADER_DIR}"
        -DAYTGT_MODULE_NAME="${MODULE_NAME}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNoBgfxInPublicHeaders.cmake"
    COMMENT "AY2D public-header bgfx/bx leak guard (audit F-5)"
    VERBATIM
)

# Mark the guard as part of the module's "all" target so a plain
# `cmake --build .` inside AY2D/ also runs the leak check.
add_dependencies(${MODULE_NAME} ay2d_check_no_bgfx_in_public_headers)

# ----------------------------------------------------------------------------
# Phase 0 note: enable_testing() + add_subdirectory(unittest) is intentionally
# omitted. The unittest/ directory will land in Phase 1+ alongside the first
# real Test_*.cpp. Wiring it now would create an empty target set, which
# upstream root CI testing might still pick up depending on how it probes
# subdirectories.
# ----------------------------------------------------------------------------

message(STATUS "AY2D v0.1 (Phase 0 docs-only) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
