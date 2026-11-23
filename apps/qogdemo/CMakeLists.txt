# QuakeOG demo playback pair (Agent O). The display half runs id's client and
# renderer on the recorded demos in the board's data image, with no server,
# QuakeC, console or sound (port/common/qog_demo.c), and prints fps over USB.
# The main half is the same minimal companion as apps/qogfly.
#
#   cmake --build build --target qogdemo_main
#
# Not in the default build, like apps/qogfly.
set(QOG_ROOT ${CMAKE_SOURCE_DIR})
set(QOGDEMO_HUNK_KB 72 CACHE STRING "qogdemo hunk in KB (demo2 peak 74,176 B on the PC with two paks and an 8 KB command buffer, about 65.5 KB on the board)")
set(QOGDEMO_STACK_KB 34 CACHE STRING "qogdemo stack in KB, in main RAM")
set(QOGDEMO_ZONE_KB 4 CACHE STRING "qogdemo zone in KB (demo1 to demo3 peak 3,176 B on the PC)")

add_executable(qogdemo_display EXCLUDE_FROM_ALL
    display/main.c
    display/demo_wrap.c
    display/demo_sv.c
    ${QOG_ROOT}/port/common/qog_demo.c
    ${QOG_ROOT}/port/rp2040/sys_rp2040.c
    ${QOG_ROOT}/port/rp2040/vid_rp2040.c
    ${QOG_ROOT}/port/rp2040/snd_rp2040.c)
target_compile_definitions(qogdemo_display PRIVATE
    QOG_SYS_NO_MAIN
    QOG_HUNK_KB=${QOGDEMO_HUNK_KB}
    QOG_DEMO_STACK_KB=${QOGDEMO_STACK_KB}
    QOG_DEMO_ZONE_KB="${QOGDEMO_ZONE_KB}")
target_compile_options(qogdemo_display PRIVATE -fstack-usage)
# The demo player gets its own engine library, sized for the three E1 demos
# (measured with -qogdemo on the PC). Same sources and flags as qog_engine
# otherwise (apps/quake/CMakeLists.txt, which must come first).
set(QOGDEMO_SURFACES 480 CACHE STRING "qogdemo edge-drawing surfaces (demo1 to demo3 peak 412, the game uses 800)")
get_target_property(_demo_src qog_engine SOURCES)
get_target_property(_demo_defs qog_engine COMPILE_DEFINITIONS)
list(FILTER _demo_src EXCLUDE REGEX "progs_aot\.c$")
set(QOGDEMO_SLOTS 192 CACHE STRING "qogdemo client entity slots (demo1 uses 178 entity numbers, the game keeps one per number up to 600)")
list(TRANSFORM _demo_defs REPLACE "^QOG_NUMSTACKSURFACES=[0-9]+$" "QOG_NUMSTACKSURFACES=${QOGDEMO_SURFACES}")
list(TRANSFORM _demo_defs REPLACE "^QOG_NUMSTACKEDGES=[0-9]+$" "QOG_NUMSTACKEDGES=1800")     # the edge table fits the one-byte z-buffer (demos peak 995)
list(TRANSFORM _demo_defs REPLACE "^QOG_CMDBUF_SIZE=[0-9]+$" "QOG_CMDBUF_SIZE=256")        # demos stuff only "bf", the loop "playdemo"
list(TRANSFORM _demo_defs REPLACE "^QOG_MAX_CACHED_PICS=[0-9]+$" "QOG_MAX_CACHED_PICS=8")  # loading, pause, intermission and finale pics
list(APPEND _demo_defs
    QOG_CL_ENTSLOTS=${QOGDEMO_SLOTS}  # client entities in slots for the numbers in use (exact)
    QOG_MAX_TEMP_ENTITIES=16        # beam segments, none in the E1 demos (id 64)
    QOG_DLIGHT_SURFS=256            # lit surface hash, overflow falls back to id's tree walk (game 1024)
    QOG_ZBUFFER8                    # one-byte z-buffer, 48,640 B less RAM, changes the picture slightly (docs/changes.md)
    QOG_ZBUFFER8_NEAR=7             #   with its own steps nearer than 32 units, so the weapon keeps its depth order
    QOG_EDGECACHE_FRAME=152)        # the world's edge cache in the frame buffer's 3D view rows, the z-buffer has no room for it
set(_demo_aot ${CMAKE_CURRENT_BINARY_DIR}/progs_aot_stub.c)
add_custom_command(OUTPUT ${_demo_aot}
    COMMAND ${Python3_EXECUTABLE} ${QOG_ROOT}/tools/qc2c.py --stub -o ${_demo_aot}
    DEPENDS ${QOG_ROOT}/tools/qc2c.py)
add_library(qog_engine_demo STATIC EXCLUDE_FROM_ALL ${_demo_src} ${_demo_aot})
target_include_directories(qog_engine_demo PUBLIC
    ${QOG_ROOT}/engine ${QOG_ROOT}/port/common ${QOG_ROOT}/platform)
target_compile_definitions(qog_engine_demo PUBLIC ${_demo_defs})
target_compile_options(qog_engine_demo PRIVATE -w -Wno-implicit-function-declaration -fstack-usage)
set_source_files_properties(${_demo_src} ${_demo_aot} PROPERTIES COMPILE_DEFINITIONS "rand=qog_rand")
target_link_libraries(qog_engine_demo PUBLIC pico_stdlib)
target_compile_options(qog_engine_demo PUBLIC -fno-short-enums)   # same layout as the PC and the baked image (apps/quake)
target_link_options(qog_engine_demo INTERFACE -Wl,--no-enum-size-warning)
target_link_libraries(qogdemo_display qog_engine_demo qog_platform fwog_display_bsp)
# As apps/qogfly: the SDK's sources are in the library and the app, and id's
# headers define a few globals in every file that includes them.
target_link_options(qogdemo_display PRIVATE -Wl,--allow-multiple-definition -Wl,--cref)
# The host, server, console, keys, menus, network and sound objects stay out
# of the link: display/demo_wrap.c and demo_sv.c define what the client and
# renderer refer to.
fwog_display_app(qogdemo_display
    VERSION 001
    DESCRIPTION "QuakeOG demo playback: id's client and renderer play demo1 to demo3 from flash, fps over USB")
qog_check_app_size(qogdemo_display)

add_executable(qogdemo_main EXCLUDE_FROM_ALL main/main.c)
target_link_libraries(qogdemo_main fwog_main_bsp)
fwog_embed_display_image(qogdemo_main)
fwog_main_app(qogdemo_main
    VERSION 001
    DESCRIPTION "QuakeOG demo playback companion: boots the display CPU and kicks the watchdog. Does no game work.")
