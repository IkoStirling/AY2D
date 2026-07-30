cmake_minimum_required(VERSION 3.15)

project(AY2D_Tests
    VERSION 0.1.0
    DESCRIPTION "AY2D unit tests (Phase 1+ scaffold; TU placeholders per design.md §11.2 CI gate)"
)

set(TEST_TARGET AY2D_Tests)

# ---------------------------------------------------------------------------
# Phase 1+ test target layout (mirrors AYPhysics/unittest/CMakeLists.txt):
#
#   * Each Test_<Subject>.cpp is a STAND-ALONE translation unit (one
#     file = one TU). This follows the AYPhysics CLAUDE.md rule
#     "每个 Test_*.cpp 是一个独立 TU (避免 MSVC include-order 问题, 参
#     见 AYAudio 范本)".
#
#   * The AY2D module itself is INTERFACE in Phase 0 (no .lib/.a
#     artifact). Tests link against it as an INTERFACE library; their
#     PUBLIC include dir comes through automatically.
#
#   * AYTest comes from AYFoundation/AYTest/CMakeLists.txt. AY2D depends
#     on AYTest via target_link_libraries(... AYTest) and a small set of
#     public-include paths (see CLAUDE.md "新增模块要求").
# ---------------------------------------------------------------------------

add_executable(${TEST_TARGET}
    main.cpp
    # Phase 1+ stubs — these exercise the placeholder public headers
    # shipped in include/AY2D/*.h (design.md §0 Phase 0 exit: 4 placeholder
    # headers). The tests assert the placeholders compile + instantiate;
    # functional assertions arrive with Phase 2 implementation.
    Test_TileCoord.cpp
    Test_CollisionFlags.cpp
    Test_TileIdPackMode.cpp
    # Phase 2 functional tests — exercise the real .cpp implementations
    # (src/AYTilemap.cpp, src/AYInMemoryTilemapChunkSource.cpp,
    #  include/AYTileSamplerUV.h header-only math). These are the
    # first tests with non-trivial state setup; AYTest's TU isolation
    # means each file gets fresh state.
    Test_Tilemap.cpp
    Test_TileSamplerUV.cpp
    Test_InMemoryTilemapChunkSource.cpp
    # Phase 3 in-AY2D tests — exercise the real-impl promotions
    # (AYWorld2D, AYCamera, ChunkRequestHandle, Ay2DCounters).
    Test_World2D.cpp
    Test_OrthographicCamera.cpp
    Test_ChunkRequestHandle.cpp
    Test_Counters.cpp
    # Phase 3B in-AY2D tests — animation table + Sprite real impl.
    # design.md §13.7. No cross-module PRs.
    Test_TilemapAnimation.cpp
    Test_Sprite.cpp
    # Phase 3C in-AY2D tests — counters real-wired onto mutation
    # paths. design.md §13.8 / §14. Tests use the actual owner
    # objects (World2D / Tilemap / InMemoryTilemapChunkSource) so
    # the deltas are produced by real code, not manual stand-ins.
    Test_CountersWired.cpp
    # Phase 3D in-AY2D tests — batch tile-fill APIs
    # (setTileRange / fillTile / copyTileRange / gridRect).
    # design.md §13.9 / §15. One batch operation = one mutation
    # event; partial clamp is allowed; width-mismatch is no-op.
    Test_TilemapBatch.cpp
    # Phase 3E in-AY2D tests — world↔cell coordinate math
    # (worldToCell / cellToWorld / aabbOverlappingCells /
    # isCellInWorldBounds + world-coord setTile/getTile/setTileRange
    # overloads on Tilemap). design.md §13.10 / §16. Pure math,
    # no cross-module deps, no bgfx. No new AYMath module link
    # needed — AYMath is already linked for AYOrthographicCamera.
    Test_TileMath.cpp
    # Phase 3F in-AY2D tests - sprite scene AABB cull + stable
    # sort + emit. design.md §13.11 / §17. The helper is a pure
    # data-carrier builder; the future RenderSystem2D cross-
    # module PR to AYRenderer (§4.2.1) translates SpriteDrawCmd
    # into DrawItem::payload. No new AYMath / AYRenderer
    # dependency - AYMath is already linked.
    Test_SpriteCulling.cpp
    # Phase 3G in-AY2D tests - chunk-source budget gates
    # (setCapacity / setMaxIoBytesPerSec / setBudget) + the
    # 10th chunk_io_reject counter. design.md §13.12 / §18.
    # LRU-only wire; Distance / TimeWindow deferred to Phase 4
    # streaming PR (R-3G.1). Tests use advanceWindowForTest
    # helper for deterministic rate-gate rollover (R-3G.7).
    Test_ChunkSourceBudget.cpp
    # Phase 5 in-AY2D tests - collision types (Ray2D /
    # RaycastHit2D / ITileCollisionQuery) +
    # TilemapCollisionQueryAdapter + the flagsAtRaw Empty
    # regression. design.md §13.13 / §8.1. No cross-module PR.
    Test_TileCollisionQuery.cpp
    # P3H.2 in-AY2D tests - World2DSnapshot value type +
    # TilemapEntryView (replaces IWorld2DDebug vtable plan).
    # design.md §13.14. TilemapBinding deprecated (covered
    # implicitly via warning-on-use).
    Test_World2DSnapshot.cpp
    # P3G.2a in-AY2D tests - maxChunksCpuSoftCap
    # (in-AY2D CPU soft cap, evict-down-to) +
    # chunk_io_residency_reject counter. design.md §13.15.
    # No new AYMath dep; counter appended to Ay2DCounters.
    Test_CpuSoftCap.cpp
    # P3G.1 partial in-AY2D tests - counter scaffolding
    # (evictions_distance / _time_window / _lru) +
    # non-LRU setBudget log warning. design.md §13.16.
    # R-3G.1 lock intact; full Distance / TimeWindow wire
    # stays deferred to the cross-module Phase 4 PR.
    Test_BudgetPolicyCounters.cpp
    # P3D.2 in-AY2D tests - aabbOfCell centered on cellToWorld
    # per R-3E.5 (regression for the naive corner-port bug).
    # design.md §13.17.
    Test_AabbOfCell.cpp
    # P3H.1 in-AY2D tests - SpriteSheet thin wrapper
    # (AtlasDesc + path). design.md §13.18. Reuses
    # AYTileSamplerUV::tileUV; no new UV derivation logic.
    Test_SpriteSheet.cpp
    # P3H.3 in-AY2D tests - foreachTilemapView read-only
    # visitor (TilemapEntryView; no Entry& exposure, no
    # resource field). design.md §13.19. The visitor is
    # header-inline on World2D.
    Test_ForeachTilemapView.cpp
    # P3I.1 in-AY2D tests - blockedTileIds + flagsAtRaw
    # data-side wire (§13.20 / §13.PF C6-R1 amendment).
    # Consumer side (AYPhysics isBlocked) stays cross-module
    # per §4.2.1. No new deps.
    Test_TilemapBlockedTileIds.cpp
    # P3I.2 in-AY2D tests - removeTilemap chunk-source purge
    # (§13.21 + §18.4 one-source-per-tilemap). LRU-coherent
    # counter bumps via evictDownTo(0); pending cancel does
    # NOT inflate evictions_lru (L-3I-4). swapTilemap stays
    # no-purge (§13.15).
    Test_World2DRemoveTilemapPurge.cpp
)

target_link_libraries(${TEST_TARGET} PRIVATE
    AY2D
    AYTest
    # Phase 3 added AYMath: AYOrthographicCamera now carries
    # `ayt::math::Float4x4` view/projection matrices (design.md §3).
    # The library is the dependency; we don't redeclare the type.
    AYMath
    # No AYResource / AYEntity link yet — those land with the
    # cross-module PRs (design.md §4.2.1).
)

target_include_directories(${TEST_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(${TEST_TARGET} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(${TEST_TARGET} PRIVATE /W4 /wd4200)
else()
    target_compile_options(${TEST_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
