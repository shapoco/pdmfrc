cmake_minimum_required(VERSION 3.13)

# Under ESP-IDF this directory is a component (see idf_component.yml)
if(ESP_PLATFORM)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/esp_idf.cmake)
    return()
endif()

project(pdmfrc LANGUAGES CXX)

# Compile-time options (SPEC.md 3.2)
option(PDMFRC_POS_8BIT "Coordinates in uint8_t (screens up to 255x255)" OFF)
set(PDMFRC_LUT565_R_BITS 4 CACHE STRING "Upper bits of R indexing the RGB565 LUT (1..5)")
set(PDMFRC_LUT565_G_BITS 5 CACHE STRING "Upper bits of G indexing the RGB565 LUT (1..6)")
set(PDMFRC_LUT565_B_BITS 4 CACHE STRING "Upper bits of B indexing the RGB565 LUT (1..5)")
# Input formats compiled in: disabling the unused ones saves code (and RAM
# when the inner loops are placed there)
foreach(fmt GRAY8 RGB565 RGB565_SWAPPED)
    set(PDMFRC_FORMAT_${fmt} 1 CACHE STRING "Input format ${fmt} (0/1)")
endforeach()
set(PDMFRC_HOT_ATTR "" CACHE STRING
    "Attribute of the inner loops of push/pop, e.g. __attribute__((section(\".time_critical.pdmfrc\")))")

add_library(pdmfrc STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/pdmfrc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/push.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/lut.cpp
)
target_include_directories(pdmfrc PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(pdmfrc PUBLIC cxx_std_11)

# Changes pos_t, so it is passed on to the users of the library
if(PDMFRC_POS_8BIT)
    target_compile_definitions(pdmfrc PUBLIC PDMFRC_POS_8BIT)
endif()
# Read by src/*.cpp only
target_compile_definitions(pdmfrc PRIVATE
    PDMFRC_LUT565_R_BITS=${PDMFRC_LUT565_R_BITS}
    PDMFRC_LUT565_G_BITS=${PDMFRC_LUT565_G_BITS}
    PDMFRC_LUT565_B_BITS=${PDMFRC_LUT565_B_BITS}
    PDMFRC_FORMAT_GRAY8=${PDMFRC_FORMAT_GRAY8}
    PDMFRC_FORMAT_RGB565=${PDMFRC_FORMAT_RGB565}
    PDMFRC_FORMAT_RGB565_SWAPPED=${PDMFRC_FORMAT_RGB565_SWAPPED}
)
if(NOT PDMFRC_HOT_ATTR STREQUAL "")
    target_compile_definitions(pdmfrc PRIVATE "PDMFRC_HOT_ATTR=${PDMFRC_HOT_ATTR}")
endif()

# Lets the linker drop what is not used (e.g. initLut when only setLut is)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pdmfrc PRIVATE -ffunction-sections -fdata-sections)
endif()
