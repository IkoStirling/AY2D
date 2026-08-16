# AY2D/demo/CMakeLists.txt — CM-4 (2026-08-11): standalone 2D demo exe.
#
# Two integration paths in one window (key 1/2 to switch):
#   1 = ECS main line  — AYEntity 2D components + 刀 2 systems
#       (TilemapComponent / SpriteComponent / OrthoCameraComponent +
#       OrthoCameraUpdateSystem(405) / TilemapRenderSystem(510) /
#       SpriteRenderSystem(510)); bootstrapModule() wires all five.
#   2 = direct AY2D line — ayt::ay2d::World2D + std::vector<ayt::ay2d::Sprite>
#       -> buildSpriteScene -> SpriteDrawCmd -> DrawItem translation,
#       plus tile draws via the real ayt::ay2d::tileUV / cellToWorld;
#       the demo is the ONLY layer allowed to hold AY2D data and
#       translate it into DrawItem (it links both AY2D and AYRenderer).
#
# CMake skeleton mirrors AYRenderer/demo/CMakeLists.txt line for line
# (centralized AYShader bgfx discovery + shaderc hints + user32/gdi32).
# Missing any shaderc hint means runtime shader-compile failure that is
# much harder to diagnose than a build error — do not diverge.

set(AY_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYShader")
set(AY_RESOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYResource")
include("${AY_SHADER_ROOT}/cmake/AYBgfx.cmake")

add_executable(AY2D_EngineDemo AY2DEngineDemo.cpp)

target_link_libraries(AY2D_EngineDemo PRIVATE
    AY2D
    AYRenderer
    AYEntity
    AYGameLoop
    AYResource
)

target_include_directories(AY2D_EngineDemo PRIVATE
    ${AY_RESOURCE_ROOT}/include/assetsImpl
    ${AY_RESOURCE_ROOT}/interface
    ${AY_RESOURCE_ROOT}/interface/assetsDefs
)

if(WIN32)
    target_link_libraries(AY2D_EngineDemo PRIVATE user32 gdi32)
endif()

target_compile_definitions(AY2D_EngineDemo PRIVATE
    AY_SHADER_SHADERC_HINT="${AY_SHADER_SHADERC_PATH}"
    AY_SHADER_BGFX_COMMON_HINT="${AY_SHADER_BGFX_COMMON_DIR}"
    AY_SHADER_BGFX_SRC_HINT="${AY_SHADER_BGFX_SRC_DIR}"
)
