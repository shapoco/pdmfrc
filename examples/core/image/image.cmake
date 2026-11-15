# Adds the image example (core part) to target, generating image_data.hpp
#   include(<pdmfrc>/examples/core/image/image.cmake)
#   pdmfrc_example_image(<target>)
set(PDMFRC_EXAMPLE_IMAGE_DIR ${CMAKE_CURRENT_LIST_DIR})
# The image of the screen size of the display (assets/images/<display>.png)
if(NOT DEFINED PDMFRC_DISPLAY)
    set(PDMFRC_DISPLAY tg12864b)
endif()
set(PDMFRC_EXAMPLE_IMAGE_PNG ${CMAKE_CURRENT_LIST_DIR}/../../../assets/images/${PDMFRC_DISPLAY}.png
    CACHE FILEPATH "Image of the image example")

function(pdmfrc_example_image target)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(tools ${PDMFRC_EXAMPLE_IMAGE_DIR}/../../../tools)
    set(gen_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set(out ${gen_dir}/image_data.hpp)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${Python3_EXECUTABLE} ${tools}/img2cpp.py ${PDMFRC_EXAMPLE_IMAGE_PNG} ${out}
                --format rgb565_swapped --name IMAGE
        DEPENDS ${PDMFRC_EXAMPLE_IMAGE_PNG} ${tools}/img2cpp.py ${tools}/colorlib.py
        COMMENT "Converting image to image_data.hpp"
    )
    target_sources(${target} PRIVATE ${PDMFRC_EXAMPLE_IMAGE_DIR}/image.cpp ${out})
    target_include_directories(${target} PRIVATE ${PDMFRC_EXAMPLE_IMAGE_DIR} ${gen_dir})
endfunction()
