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
# (bgfx find_package + CMAKE_PREFIX_PATH into AYShader/thirdParty/
# bgfx-install + shaderc hints + AY_SHADER_*_HINT defs + user32 gdi32).
# Missing any shaderc hint means runtime shader-compile failure that is
# much harder to diagnose than a build error — do not diverge.

set(AY_SHADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYShader")
set(AY_RESOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../AYResource")

list(APPEND CMAKE_PREFIX_PATH
    "${AY_SHADER_ROOT}/thirdParty/bgfx-install/debug"
    "${AY_SHADER_ROOT}/thirdParty/bgfx-install/release"
)
find_package(bgfx CONFIG REQUIRED)

if(NOT DEFINED AY_SHADER_SHADERC_PATH)
    if(WIN32)
        set(_ays_shaderc_name "shaderc.exe")
    else()
        set(_ays_shaderc_name "shaderc")
    endif()
    set(AY_SHADER_SHADERC_PATH
        "${AY_SHADER_ROOT}/thirdParty/bgfx-install/debug/bin/${_ays_shaderc_name}")
endif()

if(NOT DEFINED AY_BGFX_SHADER_INCLUDE_DIR)
    set(AY_BGFX_SHADER_INCLUDE_DIR
        "${AY_SHADER_ROOT}/thirdParty/bgfx-install/debug/include/bgfx")
endif()

get_filename_component(AY_SHADER_SHADERC_PATH "${AY_SHADER_SHADERC_PATH}" ABSOLUTE)
get_filename_component(AY_BGFX_SHADER_INCLUDE_DIR "${AY_BGFX_SHADER_INCLUDE_DIR}" ABSOLUTE)

if(NOT DEFINED AY_SHADER_BGFX_COMMON_DIR)
    set(_ays_bgfx_search
        "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/bgfx/examples/common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../thirdparty/bgfx/examples/common"
    )
    set(AY_SHADER_BGFX_COMMON_DIR "")
    foreach(_dir IN LISTS _ays_bgfx_search)
        if(EXISTS "${_dir}/common.sh")
            set(AY_SHADER_BGFX_COMMON_DIR "${_dir}")
            break()
        endif()
    endforeach()
endif()
if(NOT DEFINED AY_SHADER_BGFX_SRC_DIR AND AY_SHADER_BGFX_COMMON_DIR)
    set(AY_SHADER_BGFX_SRC_DIR "${AY_SHADER_BGFX_COMMON_DIR}/../../src")
endif()

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
