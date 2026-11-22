# qog_platform: everything Quake needs from the board, and nothing else.
# Engine code should include only these headers, never the BSP directly,
# so the BSP glue stays small and separable (licensing: see docs/).
add_library(qog_platform STATIC
    qog_video.c
    qog_input.c
    qog_data.c)
target_include_directories(qog_platform PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(qog_platform PUBLIC
    fwog_display_bsp
    pico_multicore
    hardware_dma
    hardware_spi)
target_compile_options(qog_platform PRIVATE -Wall -Wextra)

# Fails the build if a display image would grow into the game data region.
function(qog_check_app_size target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_app_size.py
                $<TARGET_FILE_DIR:${target}>/${target}.bin
                ${CMAKE_SOURCE_DIR}/platform/qog_flashmap.h
        VERBATIM)
endfunction()
