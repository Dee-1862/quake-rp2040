# Quake on the FREE-WILi OG display CPU.
#
# Not part of the default build yet: the engine still needs ~1.8 MB of RAM
# (docs/fit-plan.md), so quake_display cannot link into 264 KB. Build the
# pieces to measure them:
#
#   cmake --build build --target qog_engine      # engine for Cortex-M0+, with stack reports
#   cmake --build build --target quake_display   # full link: reports how far RAM overflows
#   python tools/arm_report.py                   # code size, static RAM, biggest stack frames

set(QOG_ROOT ${CMAKE_SOURCE_DIR})

# Same engine configuration as the PC build (pc/CMakeLists.txt).
set(QOG_ENGINE_DEFS
    QOG_SINGLE_PLAYER
    QOG_EDICT_MARGIN=128
    QOG_MAX_MOD_KNOWN=192
    QOG_MAX_PARTICLES=512
    QOG_CON_TEXTSIZE=4096
    QOG_KEY_HISTORY=8
    QOG_MAX_VIEWSIZE=100
    QOG_NUMSTACKEDGES=2000
    QOG_NUMSTACKSURFACES=800
    QOG_MAXSPANS=1000
    QOG_MAXWIDTH=320
    QOG_MAXHEIGHT=200
    QOG_MAXALIASVERTS=328
    QOG_MAX_BTOFPOLYS=0
    QOG_WARP_INPLACE=1)
# BSP structures built at map load (engine/model.c), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_PLANES_EXACT
    QOG_MARKSURF16
    QOG_SURF_PACKED
    QOG_EDGECACHE16
    QOG_LEAF_PACKED
    QOG_SURF_INDEX
    QOG_FRAME16)
# Buffers, message sizes and client tables sized from measured peaks.
list(APPEND QOG_ENGINE_DEFS
    QOG_LOOP_SERVER_RX=2048
    QOG_MAX_MSGLEN=6500
    QOG_SIGNON_SIZE=6500
    QOG_LOOP_CLIENT_RX=7532
    QOG_LOCALSTACK_FROM_PROGS
    QOG_MAX_CACHED_PICS=56
    QOG_CACHEPIC_NAME=24
    QOG_STATICS_ON_HUNK
    QOG_ZONE_KB=24
    QOG_CONST_LOCALMODELS
    QOG_SURF_T_PACKED
    QOG_MAX_BMODEL_VERTS=128
    QOG_MAX_BMODEL_EDGES=256)
# Renderer stack frames and render tables, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_EDGE_T_PACKED
    QOG_ESPAN_T_PACKED
    QOG_SURF_T_SMALL
    QOG_FINALVERT_PACKED
    QOG_ALIAS_NO_AUXVERTS)
# Baked maps (engine/model.c), loader only. The writer is the PC's qog_bake.
list(APPEND QOG_ENGINE_DEFS
    QOG_BAKE)
# Server entities (edicts) and QuakeC memory, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_EDICT_SPARSE
    QOG_BASELINE_POOL
    QOG_AOT_GLOBALS)
# Client memory and 2D drawing (Agent G), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_CONBACK_FLASH
    QOG_ENTITY_PACKED
    QOG_PARTICLE_PACKED
    QOG_PARTICLE_AVEL8
    QOG_CACHEPIC_FLASH)
# Model table, server tables and board wiring (Agent I), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_MOD_FLASH
    QOG_SV_TABLES
    QOG_SV_MAX_MODELS=208
    QOG_SV_MAX_SOUNDS=176
    QOG_CONST_NOVIS
    QOG_PVS_LEAFS=4096)
# Video memory and renderer tables (Agent H), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_CONST_RTABLES
    QOG_NO_ROWTABLES
    QOG_SKY_DIRECT
    QOG_HUD_IN_ZBUFFER
    QOG_SPANS_IN_EDGES)
# Flash budget for the board's data image (Agent J), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_BAKE_STUBS
    QOG_DEMOS_OPTIONAL)
# Server, client and system memory (Agent M), same as pc/CMakeLists.txt except
# the last two, which are board only (smaller than the PC test scripts need).
list(APPEND QOG_ENGINE_DEFS
    QOG_HUNK_PACKED
    QOG_TRANSLATE_ONDRAW
    QOG_CBUF_INPLACE
    QOG_LOOP_INPLACE
    QOG_STATIC_TRIM
    QOG_EDICT_REUSE
    QOG_CMDBUF_SIZE=4096            # board only, command buffer, measured peak 2158 B (PC keeps 8192 for test scripts)
    QOG_ZONE_BOARD_KB=16)           # board only, zone, peak 7920 B, 12 KB fragmented (PC keeps 24 KB)

file(GLOB QOG_ENGINE_SRC ${QOG_ROOT}/engine/*.c)
list(REMOVE_ITEM QOG_ENGINE_SRC ${QOG_ROOT}/engine/snd_null.c)

# QuakeC runs in id's interpreter, read in place from flash. The compiled
# path (tools/qc2c.py) is opt-in and needs registered data (docs/lessons.md L39).
set(QOG_PROGS_AOT ${CMAKE_CURRENT_BINARY_DIR}/progs_aot.c)
# QuakeOG (Agent I): -DQOG_AOT=ON compiles QuakeC to C, as the PC build does,
# from the progs.dat in QOG_AOT_PAK. It must be the progs.dat the board's
# data image holds (tools/mkboard.py copies data/id1/pak0.pak's unchanged).
# Generated in the build folder from the user's own data, never committed.
option(QOG_AOT "compile QuakeC to C from QOG_AOT_PAK (registered/retail data only)" OFF)
set(QOG_AOT_PAK ${QOG_ROOT}/data/id1/pak0.pak CACHE FILEPATH "pak holding progs.dat for QOG_AOT")
if(QOG_AOT)
    if(NOT EXISTS ${QOG_AOT_PAK})
        message(FATAL_ERROR "QOG_AOT: ${QOG_AOT_PAK} not found (set QOG_AOT_PAK)")
    endif()
    add_custom_command(OUTPUT ${QOG_PROGS_AOT}
        COMMAND ${Python3_EXECUTABLE} ${QOG_ROOT}/tools/qc2c.py --pak ${QOG_AOT_PAK} -o ${QOG_PROGS_AOT}
        DEPENDS ${QOG_ROOT}/tools/qc2c.py ${QOG_AOT_PAK} ${QOG_ROOT}/engine/progs.h ${QOG_ROOT}/engine/progdefs.q1
        COMMENT "Compiling QuakeC to C (board)")
else()
add_custom_command(OUTPUT ${QOG_PROGS_AOT}
    COMMAND ${Python3_EXECUTABLE} ${QOG_ROOT}/tools/qc2c.py --stub -o ${QOG_PROGS_AOT}
    DEPENDS ${QOG_ROOT}/tools/qc2c.py)
endif()

add_library(qog_engine STATIC EXCLUDE_FROM_ALL
    ${QOG_ENGINE_SRC} ${QOG_PROGS_AOT}
    ${QOG_ROOT}/port/common/qog_mem.c
    ${QOG_ROOT}/port/common/qog_aim.c
    ${QOG_ROOT}/port/common/qog_rand.c)
target_include_directories(qog_engine PUBLIC
    ${QOG_ROOT}/engine ${QOG_ROOT}/port/common ${QOG_ROOT}/platform)
target_compile_definitions(qog_engine PUBLIC ${QOG_ENGINE_DEFS})
# id's 1996 code is noisy; -fstack-usage writes a .su file per object so the
# biggest stack frames can be found before they overflow the board's stack.
target_compile_options(qog_engine PRIVATE -w -Wno-implicit-function-declaration -fstack-usage)
# One random sequence on every port (port/common/qog_rand.c).
set_source_files_properties(${QOG_ENGINE_SRC} ${QOG_PROGS_AOT}
    PROPERTIES COMPILE_DEFINITIONS "rand=qog_rand")
target_link_libraries(qog_engine PUBLIC pico_stdlib)

add_executable(quake_display EXCLUDE_FROM_ALL
    ${QOG_ROOT}/port/rp2040/sys_rp2040.c
    ${QOG_ROOT}/port/rp2040/vid_rp2040.c
    ${QOG_ROOT}/port/rp2040/snd_rp2040.c)
target_compile_options(quake_display PRIVATE -fstack-usage)
# QuakeOG (Agent I): hunk size in KB (port/rp2040/sys_rp2040.c). 64 is a
# placeholder; E1M1 needs about 393 KB interpreted, 355 KB with QOG_AOT.
set(QOG_HUNK_KB 64 CACHE STRING "board hunk size in KB")
target_compile_definitions(quake_display PRIVATE QOG_HUNK_KB=${QOG_HUNK_KB})
target_link_libraries(quake_display qog_engine qog_platform fwog_display_bsp)
fwog_display_app(quake_display
    VERSION 001
    DESCRIPTION "QuakeOG: Quake on the display RP2040 (does not fit RAM yet; built for measurement)")
