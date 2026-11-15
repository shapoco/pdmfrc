cmake_minimum_required(VERSION 3.13)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/pico_sdk_import.cmake)

project(pdmfrc_gfx3d C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# 3D rendering needs the overclock (cmake -DSYS_CLOCK_KHZ=... to change)
set(SYS_CLOCK_KHZ 250000 CACHE STRING "System clock [kHz]")
# Only the input format used here (saves RAM: the inner loops run from it)
set(PDMFRC_FORMAT_GRAY8 0 CACHE STRING "")
set(PDMFRC_FORMAT_RGB565 0 CACHE STRING "")
set(PDMFRC_FORMAT_RGB565_SWAPPED 1 CACHE STRING "")
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/rp2_common.cmake)
# Needs the palette, and the screen size of the TG12864B
pdmfrc_rp2_require_display(tg12864b)

# ShapoGFX (v1.12.0 or later), RGB565_SWAPPED only, settings for a core without an FPU
if(NOT DEFINED ENV{SHAPOGFX_PATH})
    message(FATAL_ERROR "Environment variable SHAPOGFX_PATH is not set (path to ShapoGFX)")
endif()
set(SHAPOGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SHAPOGFX_FORMAT_GRAY1 0 CACHE STRING "")
set(SHAPOGFX_FORMAT_RGB444 0 CACHE STRING "")
set(SHAPOGFX_FORMAT_ARGB4444 0 CACHE STRING "")
set(SHAPOGFX_FORMAT_RGB565_SWAPPED 1 CACHE STRING "")
set(SHAPOGFX_FORMAT_RGB565 0 CACHE STRING "")
set(SHAPOGFX2D_RIG 0 CACHE STRING "")
set(SHAPOGFX2D_TRANSFORM 0 CACHE STRING "")
set(SHAPOGFX2D_BLEND 0 CACHE STRING "")
set(SHAPOGFX2D_COLOR_KEY 0 CACHE STRING "")
set(SHAPOGFX2D_ANTIALIAS 0 CACHE STRING "")
set(SHAPOGFX3D_FIXED_POINT 1 CACHE STRING "")
set(SHAPOGFX3D_BLEND 0 CACHE STRING "")
set(SHAPOGFX3D_LINES 0 CACHE STRING "")
set(SHAPOGFX3D_POINTS 0 CACHE STRING "")
add_subdirectory($ENV{SHAPOGFX_PATH} shapogfx)
target_compile_options(shapogfx PRIVATE -ffunction-sections -fdata-sections)
# The rasterizer runs from RAM and uses the SIO interpolators
target_compile_definitions(shapogfx PRIVATE
    "SHAPOGFX3D_HOT_ATTR=__attribute__((section(\".time_critical.gfx3d\")))"
    SHAPOGFX3D_HOT_INSTANTIATE=1)
target_link_libraries(shapogfx PRIVATE hardware_interp_headers)

add_executable(gfx3d
    main.cpp
    ${PDMFRC_EXAMPLES_CORE_DIR}/gfx3d/scene.cpp
    ${PDMFRC_EXAMPLES_CORE_DIR}/gfx3d/title.cpp
)
target_include_directories(gfx3d PRIVATE ${PDMFRC_EXAMPLES_CORE_DIR}/gfx3d)
target_compile_definitions(gfx3d PRIVATE
    "SCENE_HOT_ATTR=__attribute__((noinline, section(\".time_critical.scene\")))")
target_link_libraries(gfx3d shapogfx pico_multicore hardware_interp)
pdmfrc_rp2_example(gfx3d)
