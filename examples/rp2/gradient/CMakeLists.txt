cmake_minimum_required(VERSION 3.13)

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/pico_sdk_import.cmake)

project(pdmfrc_gradient C CXX ASM)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Only the input format used here (saves RAM: the inner loops run from it)
set(PDMFRC_FORMAT_GRAY8 1 CACHE STRING "")
set(PDMFRC_FORMAT_RGB565 0 CACHE STRING "")
set(PDMFRC_FORMAT_RGB565_SWAPPED 0 CACHE STRING "")
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/rp2_common.cmake)

add_executable(gradient
    main.cpp
    ${PDMFRC_EXAMPLES_CORE_DIR}/gradient/gradient.cpp
)
target_include_directories(gradient PRIVATE ${PDMFRC_EXAMPLES_CORE_DIR}/gradient)
pdmfrc_rp2_example(gradient)
