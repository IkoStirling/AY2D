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

# Library target.
#
# Phase 0: INTERFACE library — no source files, no object files. INTERFACE
# is the right "empty target" type for a Phase 0 stub because (a) it
# requires no SOURCES list (vcpkg's strict-mode wrapper rejects empty
# STATIC add_library), (b) it exposes target_include_directories +
# target_link_libraries as INTERFACE properties so downstream consumers
# can `target_link_libraries(... AY2D)` correctly, and (c) it creates no
# .lib / .a artifact that would otherwise sit empty in the tree.
#
# Phase 1+ flips this to `add_library(${MODULE_NAME} STATIC ${SRC_FILES})`
# the moment real `.cpp` files exist. The flipped form mirrors every other
# AYRuntime/* entry (static archive). This single line is the only behavior
# delta between Phase 0 and Phase 1+ in this file.
#
# Why INTERFACE today instead of declaring an inline `.cpp` that does
# nothing: an empty-or-no-op TU is forbidden by design — the module is
# explicitly "no code yet", and including even an empty translation unit
# invites the "where's the actual implementation?" question in review.
if(NOT SRC_FILES)
    add_library(${MODULE_NAME} INTERFACE)
else()
    add_library(${MODULE_NAME} STATIC ${SRC_FILES})
endif()

target_include_directories(${MODULE_NAME} INTERFACE
    # Public headers live flat under `include/` (matches sibling
    # convention: AYPhysics/AYRenderer put public headers directly in
    # `include/` with no subdirectory). Consumers write
    # `#include "AYWorld2D.h"` (no namespace prefix in the include path)
    # or use the umbrella `#include <AY2D.h>`.
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Phase 0 has no real link surface. The INTERFACE deps list below is empty by
# design. Phase 1+ adds per-design.md §2.3 allowed-dependencies list:
#   AYMath, AYIO, AYResource (interface only), AYEntity (interface only),
#   AYRenderer (PUBLIC headers only; never src/detail/*), AYShader
#   (PUBLIC headers only), AYGameLoop, AYTime. Never AYUI. Never bgfx.
target_link_libraries(${MODULE_NAME} INTERFACE
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

message(STATUS "AY2D v0.1 (Phase 0 docs-only) — see ${CMAKE_CURRENT_SOURCE_DIR}/design.md")
