# QuakeOG E1M1 fly-through pair (Agent N). The display half runs id's renderer
# on the real E1M1 from the board's data image, with a camera moving along a
# path and no game logic (port/common/qog_fly.c), and prints fps over USB.
# The main half is the same minimal companion as apps/qogbench.
#
#   cmake --build build --target qogfly_main
#
# Not in the default build: until the engine's RAM cuts land, the display
# half may not fit in RAM, and the link then says by how much.
set(QOG_ROOT ${CMAKE_SOURCE_DIR})
set(QOGFLY_HUNK_KB 97 CACHE STRING "qogfly hunk in KB (E1M1 peak 98,112 B on the PC with its extra commands, 95 KB fails)")
set(QOGFLY_STACK_KB 94 CACHE STRING "qogfly stack in KB, in main RAM (R_EdgeDrawing alone is 84,768 B now, the render chain about 90 KB)")

add_executable(qogfly_display EXCLUDE_FROM_ALL
    display/main.c
    display/fly_wrap.c
    ${QOG_ROOT}/port/common/qog_fly.c
    ${QOG_ROOT}/port/rp2040/sys_rp2040.c
    ${QOG_ROOT}/port/rp2040/vid_rp2040.c
    ${QOG_ROOT}/port/rp2040/snd_rp2040.c)
target_compile_definitions(qogfly_display PRIVATE
    QOG_SYS_NO_MAIN
    QOG_HUNK_KB=${QOGFLY_HUNK_KB}
    QOG_FLY_STACK_KB=${QOGFLY_STACK_KB})
target_compile_options(qogfly_display PRIVATE -fstack-usage)
target_link_libraries(qogfly_display qog_engine qog_platform fwog_display_bsp)
# qog_engine links pico_stdlib PUBLIC, so the SDK's sources are compiled into
# the library and again into this app, and id's headers define a few globals
# in every file that includes them (d_local.h sadjust, bbextents, ...), which
# GCC 10 and later reject. All the copies are the same zero data or code, and
# the app's own objects come first, so the first definition is the right one.
target_link_options(qogfly_display PRIVATE -Wl,--allow-multiple-definition -Wl,--cref)
# Engine calls that would pull in the console, sound, menus and server,
# replaced by what they do in this host (display/fly_wrap.c).
foreach(_fn Con_Printf Con_SafePrintf Con_DPrintf Host_Error S_ExtraUpdate qog_mem_register qog_aim_register)
    target_link_options(qogfly_display PRIVATE -Wl,--wrap=${_fn})
endforeach()
fwog_display_app(qogfly_display
    VERSION 001
    DESCRIPTION "QuakeOG E1M1 fly-through: id's renderer on the real map from flash, fps over USB")
qog_check_app_size(qogfly_display)

add_executable(qogfly_main EXCLUDE_FROM_ALL main/main.c)
target_link_libraries(qogfly_main fwog_main_bsp)
fwog_embed_display_image(qogfly_main)
fwog_main_app(qogfly_main
    VERSION 001
    DESCRIPTION "QuakeOG fly-through companion: boots the display CPU and kicks the watchdog. Does no game work.")
