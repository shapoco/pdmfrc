# Common setup of the RP2040 / RP2350 examples. Include after pico_sdk_init():
#   include(${CMAKE_CURRENT_SOURCE_DIR}/../common/rp2_common.cmake)
#   pdmfrc_rp2_example(<target>)
set(PDMFRC_RP2_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})
set(PDMFRC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(PDMFRC_EXAMPLES_CORE_DIR ${PDMFRC_ROOT}/examples/core)

# System clock [kHz]. Above 133000 the core voltage is raised to 1.20 V
set(SYS_CLOCK_KHZ 125000 CACHE STRING "System clock [kHz]")

# The library, its inner loops run from RAM
set(PDMFRC_HOT_ATTR "__attribute__((section(\".time_critical.pdmfrc\")))" CACHE STRING "" FORCE)
add_subdirectory(${PDMFRC_ROOT} pdmfrc)

function(pdmfrc_rp2_example target)
    target_sources(${target} PRIVATE
        ${PDMFRC_RP2_COMMON_DIR}/tg12864b.cpp
        ${PDMFRC_RP2_COMMON_DIR}/app.cpp
    )
    pico_generate_pio_header(${target} ${PDMFRC_RP2_COMMON_DIR}/tg12864b_write.pio)
    target_include_directories(${target} PRIVATE ${PDMFRC_RP2_COMMON_DIR})
    target_link_libraries(${target} pdmfrc pico_stdlib hardware_pio hardware_dma hardware_vreg)
    target_compile_definitions(${target} PRIVATE SYS_CLOCK_KHZ=${SYS_CLOCK_KHZ})
    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_add_extra_outputs(${target})
endfunction()
