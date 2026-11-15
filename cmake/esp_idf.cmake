# PDM-FRC as an ESP-IDF component, included by the CMakeLists.txt of the
# repository under ESP-IDF. The options come from Kconfig (idf.py menuconfig).
set(PDMFRC_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

idf_component_register(
    SRCS
        "${PDMFRC_ROOT}/src/pdmfrc.cpp"
        "${PDMFRC_ROOT}/src/push.cpp"
        "${PDMFRC_ROOT}/src/lut.cpp"
    INCLUDE_DIRS "${PDMFRC_ROOT}/include"
)

if(CONFIG_PDMFRC_POS_8BIT)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC PDMFRC_POS_8BIT)
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    PDMFRC_LUT565_R_BITS=${CONFIG_PDMFRC_LUT565_R_BITS}
    PDMFRC_LUT565_G_BITS=${CONFIG_PDMFRC_LUT565_G_BITS}
    PDMFRC_LUT565_B_BITS=${CONFIG_PDMFRC_LUT565_B_BITS}
)
foreach(fmt GRAY8 RGB565 RGB565_SWAPPED)
    if(CONFIG_PDMFRC_FORMAT_${fmt})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE PDMFRC_FORMAT_${fmt}=1)
    else()
        target_compile_definitions(${COMPONENT_LIB} PRIVATE PDMFRC_FORMAT_${fmt}=0)
    endif()
endforeach()
if(CONFIG_PDMFRC_HOT_IN_IRAM)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        "PDMFRC_HOT_ATTR=__attribute__((section(\".iram1.pdmfrc\")))")
endif()
