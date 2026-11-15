cmake_minimum_required(VERSION 3.13)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/pico_sdk_import.cmake)

project(pdmfrc_image C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Only the input format used here (saves RAM: the inner loops run from it)
set(PDMFRC_FORMAT_GRAY8 0 CACHE STRING "")
set(PDMFRC_FORMAT_RGB565 0 CACHE STRING "")
set(PDMFRC_FORMAT_RGB565_SWAPPED 1 CACHE STRING "")
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/rp2_common.cmake)
# Needs the palette, and the screen size of the TG12864B
pdmfrc_rp2_require_display(tg12864b)
include(${PDMFRC_EXAMPLES_CORE_DIR}/image/image.cmake)

add_executable(image main.cpp)
pdmfrc_example_image(image)
pdmfrc_rp2_example(image)
