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
    QOG_PALETTE_FLASH
    QOG_CMDBUF_SIZE=4096            # board only, command buffer, measured peak 2158 B (PC keeps 8192 for test scripts)
    QOG_ZONE_BOARD_KB=16)           # board only, zone, peak 7920 B, 12 KB fragmented (PC keeps 24 KB)
# Per-level map arrays (Agent L), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_FRAMEBITS
    QOG_DLIGHT_SPARSE
    QOG_LEAF16)
# Renderer temporary memory in the z-buffer (Agent K), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_EDGES_IN_ZBUFFER
    QOG_EDGECACHE_ZBUFFER
    QOG_SND_ONINIT
    QOG_LOOP_ONINIT
    QOG_PACKDIR_CHUNKED)
# Full game on the board (Agent Q), same as pc/CMakeLists.txt except the
# last one, which is board only.
list(APPEND QOG_ENGINE_DEFS
    QOG_MIN_MEMORY=0
    QOG_SND_OFF_NOHUNK
    QOG_LOADGAME_ZTEMP
    QOG_PRECACHE_ZTEMP
    QOG_KEYS_BYTES
    QOG_EDICT_MARGIN_BOARD=64)      # board only, edicts past the map's estimate, fight tour peak 35 (PC keeps 128)
# Demo playback (Agent O), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_DEMO_FLASH
    QOG_ITEMBOX_NOLEAFS)
# Renderer speed, exact (Agent P), same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_LITSPAN_FAST
    QOG_LITSPAN_TAILMUL
    QOG_ZSPANS_FAST
    QOG_LITSPAN_ZFUSE)

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
# The ARM bare-metal ABI makes enums as small as their values (qboolean is
# one byte), while the PC build and the baked flash image use 4-byte enums.
# Every engine struct with an enum must have the same layout on both, so
# the engine and everything that includes its headers uses 4-byte enums.
# No enum crosses into the SDK, so the linker's enum size note is silenced.
target_compile_options(qog_engine PUBLIC -fno-short-enums)
target_link_options(qog_engine INTERFACE -Wl,--no-enum-size-warning)

add_executable(quake_display EXCLUDE_FROM_ALL
    ${QOG_ROOT}/port/rp2040/sys_rp2040.c
    ${QOG_ROOT}/port/rp2040/vid_rp2040.c
    ${QOG_ROOT}/port/rp2040/snd_rp2040.c)
target_compile_options(quake_display PRIVATE -fstack-usage)
# QuakeOG (Agent I): hunk size in KB (port/rp2040/sys_rp2040.c).
# Agent Q: the measured E1M1 need with QOG_AOT and the board-only flags, a
# fight, a save and a load (PC build with the same flags, smallest
# -heapsize that runs it). Play without a load needs 187 KB, interpreted
# QuakeC 38 KB more for its globals.
set(QOG_HUNK_KB 194 CACHE STRING "board hunk size in KB")
target_compile_definitions(quake_display PRIVATE QOG_HUNK_KB=${QOG_HUNK_KB})
target_link_libraries(quake_display qog_engine qog_platform fwog_display_bsp)
# QuakeOG (Agent Q): the honest board configuration. The game's stack is an
# array in main RAM (port/rp2040/sys_rp2040.c), so the link counts it, sized
# from the deepest call chain (tools/stack_chain.py, docs/changes.md). The
# same duplicate definitions as apps/qogfly (pico_stdlib PUBLIC, d_local.h
# globals) are allowed, the first is the right one.
set(QOG_STACK_KB 47 CACHE STRING "board stack array in KB, deepest chain 45,968 B plus interrupts (0: the SDK's scratch stack)")
if(QOG_STACK_KB GREATER 0)
    target_compile_definitions(quake_display PRIVATE QOG_STACK_KB=${QOG_STACK_KB})
endif()
target_link_options(quake_display PRIVATE -Wl,--allow-multiple-definition)
fwog_display_app(quake_display
    VERSION 001
    DESCRIPTION "QuakeOG: Quake on the display RP2040 (does not fit RAM yet; built for measurement)")

# QuakeOG (Agent R): the two-chip split. The server half (SV_*, QuakeC,
# edicts) runs on the MAIN RP2040 as quake_main, the client half (CL_*,
# renderer, status bar, input) on the DISPLAY RP2040 as quake_display. They
# talk over the inter-CPU UART through the link net driver (engine/net_link.c,
# port/common/qog_link.c, port/rp2040/link_rp2040.c). Each half gets its own
# engine library from qog_engine's sources and flags, as apps/qogfly does.
# -DQOG_SPLIT=OFF builds quake_display as the single-chip game again.
#
#   cmake --build build --target quake_main   # both halves: main embeds quake_display
option(QOG_SPLIT "quake_display is the client half and quake_main the server half" ON)
if(QOG_SPLIT)
    get_target_property(_split_src qog_engine SOURCES)
    get_target_property(_split_defs qog_engine COMPILE_DEFINITIONS)
    set(_split_common
        ${QOG_ROOT}/port/common/qog_link.c
        ${QOG_ROOT}/port/common/qog_mem.c
        ${QOG_ROOT}/port/common/qog_rand.c)

    # client half: no server, QuakeC or edicts
    set(_cl_src ${_split_src})
    list(FILTER _cl_src EXCLUDE REGEX "/(sv_[a-z]+|pr_[a-z]+|net_loop|progs_aot|qog_mem|qog_rand)\.c$")
    add_library(qog_engine_cl STATIC EXCLUDE_FROM_ALL ${_cl_src} ${_split_common})
    # QuakeOG (Agent S): the client half's own settings, board only, never in
    # the PC game list (a PC client gets them with -DQOG_SPLIT_CL_EXTRA)
    set(_cl_defs ${_split_defs})
    list(TRANSFORM _cl_defs REPLACE "^QOG_NUMSTACKEDGES=[0-9]+$" "QOG_NUMSTACKEDGES=1800")   # the edge table fits the one-byte z-buffer (full game peak 1,730)
    list(TRANSFORM _cl_defs REPLACE "^QOG_ZONE_BOARD_KB=[0-9]+$" "QOG_ZONE_BOARD_KB=8")       # client zone (was 16), see docs/changes.md
    list(APPEND _cl_defs
        QOG_ZBUFFER8                    # one-byte z-buffer, approved by the user (changes about 0.07% of view pixels)
        QOG_ZBUFFER8_NEAR=7             #   with its own steps nearer than 32 units, as the demo player
        QOG_EDGECACHE_FRAME=152         # the world's edge cache in the frame buffer's 3D view rows (the z-buffer is full)
        QOG_CL_ENTSLOTS=600             # client entities only for the numbers in use (exact), at most id's MAX_EDICTS
        QOG_CL_ENTBLOCK=16              #   from the hunk in blocks of 16 as the level uses them
        QOG_CON_PRINTTEXT               # the renderer's bmodel warning prints without a 4 KB stack buffer (exact)
        QOG_DLIGHT_SURFS=256            # lit surface hash of 256 as the demo player (game 1024), overflow falls back to id's tree walk (exact, slower)
        QOG_VID_NO_8TO16                # the unused 16-bit palette table in flash (vid.h)
        QOG_ZEDGE_STACK_GUARD=1024      # Agent K's edge fallback stops with an error when the stack cannot hold it
        QOG_CVAR_LITERAL                # cvar values stay at their initial strings in flash until changed, not in the zone
        QOG_LINK_SCRATCH                # the link's two 1 KB rings in scratch X below core 1's stack
        QOG_KEYLINES_SCRATCH            # the console's 2 KB command history in scratch Y below the boot stack
        QOG_NEXTDEMO_STR                # CL_NextDemo's command line in 32 bytes, not 1 KB under the loading plaque's render
        QOG_SURF_T_TINY                 # 32-byte edge-drawing surfaces, 16-bit keys and face indices (exact, the stack's biggest table)
        QOG_TRANSLATE_INLINE            # player colours through a 256-byte texel map, not a 16 KB colour map on the stack (exact)
        QOG_LIGHTSTYLE_ZONE             # light style texts in the zone at their length, not 64 x 64 bytes
        QOG_PVS_ROW_SMALL               # the PVS row buffer for QOG_PVS_LEAFS leafs (512 bytes, id 1 KB)
        QOG_KEYNAMES_CONST              # the key name table in flash
        QOG_CL_PRECACHE_SV              # model and sound precache tables of the server half's sizes (208 and 176)
        QOG_TEMPENT_BLOCK=8             # beam segments from the level's hunk in blocks of 8 as used (id's 64 kept), none on a level without beams
        QOG_CMD_ARGV_ARENA=640)         # command tokens in a 640-byte buffer, not the zone (it fragmented a small zone)
    target_compile_definitions(qog_engine_cl PUBLIC ${_cl_defs} QOG_NET_LINK QOG_SPLIT_CLIENT)

    # server half: no client, renderer, console or sound. Its command buffer
    # is never used (the client's is the game's), so it gets a small one.
    set(QOG_SPLIT_SV_ENGINE cmd common crc cvar host host_cmd mathlib model net_link net_main net_none
        net_vcr pr_cmds pr_edict pr_exec sv_main sv_move sv_phys sv_user world zone)
    list(TRANSFORM QOG_SPLIT_SV_ENGINE PREPEND ${QOG_ROOT}/engine/)
    list(TRANSFORM QOG_SPLIT_SV_ENGINE APPEND .c)
    set(_sv_defs ${_split_defs})
    list(REMOVE_ITEM _sv_defs QOG_LOADGAME_ZTEMP QOG_PRECACHE_ZTEMP)
    list(TRANSFORM _sv_defs REPLACE "^QOG_CMDBUF_SIZE=[0-9]+$" "QOG_CMDBUF_SIZE=256")
    list(APPEND _sv_defs QOG_LOADGAME_STRSIZE=8192)   # a save's biggest block 2,485 B (id 32 KB on the hunk)
    list(TRANSFORM _sv_defs REPLACE "^QOG_ZONE_BOARD_KB=[0-9]+$" "QOG_ZONE_BOARD_KB=6")   # server zone peak 1.9 KB
    add_library(qog_engine_sv STATIC EXCLUDE_FROM_ALL ${QOG_SPLIT_SV_ENGINE} ${QOG_PROGS_AOT}
        ${QOG_ROOT}/port/common/qog_split_sv.c ${_split_common})
    target_compile_definitions(qog_engine_sv PUBLIC ${_sv_defs} QOG_NET_LINK QOG_SPLIT_SERVER)

    foreach(_lib qog_engine_cl qog_engine_sv)
        # both ends of the link use the same frame size: 512-byte frames
        target_compile_definitions(${_lib} PUBLIC QOG_LINK_CHUNK=512)
        target_include_directories(${_lib} PUBLIC
            ${QOG_ROOT}/engine ${QOG_ROOT}/port/common ${QOG_ROOT}/platform)
        target_compile_options(${_lib} PRIVATE -w -Wno-implicit-function-declaration -fstack-usage)
        target_compile_options(${_lib} PUBLIC -fno-short-enums)   # same layout as the PC and the baked image
        target_link_options(${_lib} INTERFACE -Wl,--no-enum-size-warning)
        target_link_libraries(${_lib} PUBLIC pico_stdlib)
    endforeach()
    # the server's rings are 2 KB. Agent S: the client's are 1 KB, a frame that
    # does not fit is not acknowledged and comes again (exact, slower only in
    # the bursts of a level change)
    target_compile_definitions(qog_engine_sv PUBLIC QOG_LINK_TXRING=2048 QOG_LINK_RXRING=2048)
    target_compile_definitions(qog_engine_cl PUBLIC QOG_LINK_TXRING=1024 QOG_LINK_RXRING=1024)
    target_link_libraries(qog_engine_cl PUBLIC fwog_display_bsp)
    target_link_libraries(qog_engine_sv PUBLIC fwog_main_bsp)

    # the display app becomes the client half (the same sources, another library)
    target_sources(quake_display PRIVATE ${QOG_ROOT}/port/rp2040/link_rp2040.c)
    get_target_property(_disp_libs quake_display LINK_LIBRARIES)
    list(TRANSFORM _disp_libs REPLACE "^qog_engine$" "qog_engine_cl")
    set_property(TARGET quake_display PROPERTY LINK_LIBRARIES ${_disp_libs})
    set(QOG_CL_HUNK_KB 68 CACHE STRING "client half hunk in KB, smallest -heapsize of quakeog_cl with the board client flags for E1M1 with a fight, a save and a load (e3m5 79, Agent S)")
    set(QOG_CL_STACK_KB 34 CACHE STRING "client half stack array in KB (tools/stack_chain.py from game_main, docs/changes.md)")
    get_target_property(_disp_defs quake_display COMPILE_DEFINITIONS)
    list(TRANSFORM _disp_defs REPLACE "^QOG_HUNK_KB=[0-9]+$" "QOG_HUNK_KB=${QOG_CL_HUNK_KB}")
    list(TRANSFORM _disp_defs REPLACE "^QOG_STACK_KB=[0-9]+$" "QOG_STACK_KB=${QOG_CL_STACK_KB}")
    set_property(TARGET quake_display PROPERTY COMPILE_DEFINITIONS ${_disp_defs})
    # Agent S: the BSP's frame sender without its 4 KB frame buffer (port/rp2040/link_rp2040.c)
    target_compile_definitions(quake_display PRIVATE QOG_LINK_STREAM_SEND QOG_VID_SURFCACHE_MIN)   # and a 64-byte surface cache header (vid_rp2040.c)
    target_link_options(quake_display PRIVATE -Wl,--wrap=fwog_link_uart_send_frame)

    set(QOG_SV_HUNK_KB 172 CACHE STRING "server half hunk in KB, smallest -heapsize of quakeog_sv for e3m5 with a fight, a save and a load (E1M1 130)")
    set(QOG_SV_STACK_KB 24 CACHE STRING "server half stack array in KB, deepest chain from server_main 17,496 B (tools/stack_chain.py)")
    add_executable(quake_main EXCLUDE_FROM_ALL
        ${QOG_ROOT}/port/rp2040/sys_rp2040.c
        ${QOG_ROOT}/port/rp2040/link_rp2040.c)
    target_compile_definitions(quake_main PRIVATE
        QOG_SYS_MAIN QOG_LINK_MAIN QOG_HUNK_KB=${QOG_SV_HUNK_KB} QOG_STACK_KB=${QOG_SV_STACK_KB})
    target_compile_options(quake_main PRIVATE -fstack-usage)
    target_link_libraries(quake_main qog_engine_sv fwog_main_bsp)
    target_link_options(quake_main PRIVATE -Wl,--allow-multiple-definition)
    # the display image main carries: quake_display, or for measuring main
    # while quake_display does not link, any display app (qog_display)
    set(QOG_SPLIT_MAIN_EMBED quake_display CACHE STRING "display app quake_main embeds")
    fwog_embed_display_image(quake_main ${QOG_SPLIT_MAIN_EMBED})
    fwog_main_app(quake_main
        VERSION 001
        DESCRIPTION "QuakeOG: the Quake server half (QuakeC, physics, edicts) on the main RP2040, with the client half on the display")
    add_custom_command(TARGET quake_main POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_app_size.py
                $<TARGET_FILE_DIR:quake_main>/quake_main.bin
                ${CMAKE_SOURCE_DIR}/platform/qog_flashmap.h main
        VERBATIM)
endif()
