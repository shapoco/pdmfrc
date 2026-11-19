# Common setup of the RP2040 / RP2350 examples. Include after pico_sdk_init():
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../common/rp2_common.cmake)
#   pdmfrc_rp2_example(<target>)
set(PDMFRC_RP2_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
set(PDMFRC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(PDMFRC_EXAMPLES_CORE_DIR ${PDMFRC_ROOT}/examples/core)

# System clock [kHz]. Above 133000 the core voltage is raised to 1.20 V
set(SYS_CLOCK_KHZ 125000 CACHE STRING "System clock [kHz]")

# Display (common/display_<driver>.cpp). To add a display: add its name here,
# its driver below (and its sources in pdmfrc_rp2_example()) and its size in
# display.hpp
set(PDMFRC_DISPLAYS tg12864b sg12864aslb aqm1248a nokia5110)
set(PDMFRC_DISPLAY tg12864b CACHE STRING "Display: ${PDMFRC_DISPLAYS}")
set_property(CACHE PDMFRC_DISPLAY PROPERTY STRINGS ${PDMFRC_DISPLAYS})
if(NOT PDMFRC_DISPLAY IN_LIST PDMFRC_DISPLAYS)
    message(FATAL_ERROR "Unknown PDMFRC_DISPLAY: ${PDMFRC_DISPLAY} (${PDMFRC_DISPLAYS})")
endif()
message(STATUS "PDMFRC_DISPLAY: ${PDMFRC_DISPLAY}")
# The SG12864ASLB is KS0108 compatible like the TG12864B and shares its driver
if(PDMFRC_DISPLAY STREQUAL "sg12864aslb")
    set(PDMFRC_DISPLAY_DRIVER tg12864b)
else()
    set(PDMFRC_DISPLAY_DRIVER ${PDMFRC_DISPLAY})
endif()

# Stops the configuration unless the display is one of the arguments
# (the examples that need a palette or a screen size)
function(pdmfrc_rp2_require_display)
    if(NOT PDMFRC_DISPLAY IN_LIST ARGN)
        message(FATAL_ERROR "This example supports PDMFRC_DISPLAY=${ARGN} only (not ${PDMFRC_DISPLAY})")
    endif()
endfunction()

# The library, its inner loops run from RAM
set(PDMFRC_HOT_ATTR "__attribute__((section(\".time_critical.pdmfrc\")))" CACHE STRING "" FORCE)
add_subdirectory(${PDMFRC_ROOT} pdmfrc)

function(pdmfrc_rp2_example target)
    target_sources(${target} PRIVATE
        ${PDMFRC_RP2_COMMON_DIR}/app.cpp
        ${PDMFRC_RP2_COMMON_DIR}/display_${PDMFRC_DISPLAY_DRIVER}.cpp
    )
    if(PDMFRC_DISPLAY_DRIVER STREQUAL "tg12864b")
        target_sources(${target} PRIVATE ${PDMFRC_RP2_COMMON_DIR}/tg12864b.cpp)
        pico_generate_pio_header(${target} ${PDMFRC_RP2_COMMON_DIR}/tg12864b_write.pio)
    elseif(PDMFRC_DISPLAY_DRIVER STREQUAL "aqm1248a")
        target_sources(${target} PRIVATE ${PDMFRC_RP2_COMMON_DIR}/aqm1248a.cpp)
        pico_generate_pio_header(${target} ${PDMFRC_RP2_COMMON_DIR}/aqm1248a.pio)
    elseif(PDMFRC_DISPLAY_DRIVER STREQUAL "nokia5110")
        # The PCD8544 uses the PIO program of the AQM1248A (the same 4-line SPI)
        target_sources(${target} PRIVATE ${PDMFRC_RP2_COMMON_DIR}/pcd8544.cpp)
        pico_generate_pio_header(${target} ${PDMFRC_RP2_COMMON_DIR}/aqm1248a.pio)
    endif()
    string(TOUPPER ${PDMFRC_DISPLAY} display_upper)
    target_compile_definitions(${target} PRIVATE PDMFRC_DISPLAY_${display_upper}=1)
    target_include_directories(${target} PRIVATE ${PDMFRC_RP2_COMMON_DIR})
    target_link_libraries(${target} pdmfrc pico_stdlib hardware_pio hardware_dma hardware_vreg)
    target_compile_definitions(${target} PRIVATE SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ})
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_add_extra_outputs(${target})
endfunction()
