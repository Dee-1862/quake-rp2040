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
set(QOGFLY_HUNK_KB 18 CACHE STRING "qogfly hunk in KB (18 is what ran on the board)")
set(QOGFLY_STACK_KB 34 CACHE STRING "qogfly stack in KB, in main RAM (34 is what ran on the board, R_EdgeDrawing is 36,184 B with QOG_EDGES_IN_ZBUFFER)")

add_executable(qogfly_display EXCLUDE_FROM_ALL
    display/main.c
    display/fly_wrap.c
    ${QOG_ROOT}/port/common/qog_fly.c
    ${QOG_ROOT}/port/common/qog_phase.c
    ${QOG_ROOT}/port/rp2040/sys_rp2040.c
    ${QOG_ROOT}/port/rp2040/vid_rp2040.c
    ${QOG_ROOT}/port/rp2040/snd_rp2040.c)
target_compile_definitions(qogfly_display PRIVATE
    QOG_SYS_NO_MAIN
    QOG_HUNK_KB=${QOGFLY_HUNK_KB}
    QOG_FLY_STACK_KB=${QOGFLY_STACK_KB})
target_compile_options(qogfly_display PRIVATE -fstack-usage)
# The fly-through only ever draws E1M1, whose full level tour peaks at 443
# surfaces, so it gets its own engine library with a smaller surface table
# (the edge-drawing surfaces live on the stack) and no particle block. The
# game's qog_engine keeps the full-game sizes. Same sources and flags
# otherwise (apps/quake/CMakeLists.txt, which must come first).
set(QOGFLY_SURFACES 480 CACHE STRING "qogfly edge-drawing surfaces (E1M1 tour peak 443, the game uses 800)")
get_target_property(_fly_src qog_engine SOURCES)
get_target_property(_fly_defs qog_engine COMPILE_DEFINITIONS)
list(FILTER _fly_src EXCLUDE REGEX "progs_aot\.c$")
list(TRANSFORM _fly_defs REPLACE "^QOG_NUMSTACKSURFACES=[0-9]+$" "QOG_NUMSTACKSURFACES=${QOGFLY_SURFACES}")
set(_fly_aot ${CMAKE_CURRENT_BINARY_DIR}/progs_aot_stub.c)
add_custom_command(OUTPUT ${_fly_aot}
    COMMAND ${Python3_EXECUTABLE} ${QOG_ROOT}/tools/qc2c.py --stub -o ${_fly_aot}
    DEPENDS ${QOG_ROOT}/tools/qc2c.py)
add_library(qog_engine_fly STATIC EXCLUDE_FROM_ALL ${_fly_src} ${_fly_aot})
target_include_directories(qog_engine_fly PUBLIC
    ${QOG_ROOT}/engine ${QOG_ROOT}/port/common ${QOG_ROOT}/platform)
target_compile_definitions(qog_engine_fly PUBLIC ${_fly_defs} QOG_PARTICLES_PRESET)
# Render phase times over USB every 2 s (engine/qog_phase.h, Agent P).
# About 2,000 timer reads and three walks of the span lists per frame.
option(QOGFLY_PHASE "qogfly prints the render phase split" ON)
if(QOGFLY_PHASE)
    target_compile_definitions(qog_engine_fly PUBLIC QOG_PHASE_TIME)
endif()
target_compile_options(qog_engine_fly PRIVATE -w -Wno-implicit-function-declaration -fstack-usage)
set_source_files_properties(${_fly_src} ${_fly_aot} PROPERTIES COMPILE_DEFINITIONS "rand=qog_rand")
target_link_libraries(qog_engine_fly PUBLIC pico_stdlib)
target_compile_options(qog_engine_fly PUBLIC -fno-short-enums)   # same layout as the PC and the baked image (apps/quake)
target_link_options(qog_engine_fly INTERFACE -Wl,--no-enum-size-warning)
target_link_libraries(qogfly_display qog_engine_fly qog_platform fwog_display_bsp)
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
