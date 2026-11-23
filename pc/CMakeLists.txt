# PC development build of the QuakeOG engine (Windows, MSVC, 32-bit).
#
#   cmake -S pc -B build-pc -G "Visual Studio 17 2022" -A Win32
#   cmake --build build-pc --config Release
#   build-pc\Release\quakeog_pc.exe            (game data in data\id1\)
#
# 32-bit on purpose: the RP2040 has 32-bit pointers, so struct sizes and the
# RAM meter's numbers carry over. Separate from the firmware build, which
# uses the ARM toolchain from the repo root.
cmake_minimum_required(VERSION 3.21)
project(quakeog_pc C)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure with -A Win32: the engine port assumes 32-bit pointers.")
endif()

# Engine: id's files with the real sound mixer (port/pc/snd_pc.c is the output).
file(GLOB ENGINE_SRC ${ROOT}/engine/*.c)
list(REMOVE_ITEM ENGINE_SRC ${ROOT}/engine/snd_null.c)

# QuakeC compiled to C from the player's own progs.dat (never committed).
# Opt-in: translating the data is a derivative work, which the shareware
# licence forbids (docs/lessons.md L39). Turn it on only with registered or
# retail data. Off, qc2c writes an empty table and id's interpreter runs.
option(QOG_AOT "compile QuakeC to C from data/id1 (registered/retail data only)" OFF)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(QOG_PAK ${ROOT}/data/id1/pak0.pak)
set(PROGS_AOT ${CMAKE_BINARY_DIR}/progs_aot.c)
if(QOG_AOT)
    set(_pak_dep "")
    if(EXISTS ${QOG_PAK})
        set(_pak_dep ${QOG_PAK})
    endif()
    add_custom_command(OUTPUT ${PROGS_AOT}
        COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/qc2c.py --pak ${QOG_PAK} -o ${PROGS_AOT}
        DEPENDS ${ROOT}/tools/qc2c.py ${_pak_dep}
        COMMENT "Compiling QuakeC to C")
else()
    add_custom_command(OUTPUT ${PROGS_AOT}
        COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/qc2c.py --stub -o ${PROGS_AOT}
        DEPENDS ${ROOT}/tools/qc2c.py
        COMMENT "QuakeC: interpreter (QOG_AOT is off)")
endif()

add_executable(quakeog_pc
    ${ENGINE_SRC}
    ${PROGS_AOT}
    ${ROOT}/port/pc/sys_pc.c
    ${ROOT}/port/pc/vid_pc.c
    ${ROOT}/port/pc/snd_pc.c
    ${ROOT}/port/pc/manifest_pc.c
    ${ROOT}/port/pc/tour_pc.c
    ${ROOT}/port/common/qog_mem.c
    ${ROOT}/port/common/qog_aim.c
    ${ROOT}/port/common/qog_rand.c
    ${ROOT}/platform/qog_input.c)
target_include_directories(quakeog_pc PRIVATE
    ${ROOT}/engine ${ROOT}/port/common ${ROOT}/platform)
target_compile_definitions(quakeog_pc PRIVATE
    HOST_TEST                       # platform/ pure code only, no Pico SDK
    _CRT_SECURE_NO_WARNINGS
    QOG_DEFAULT_BASEDIR="${ROOT}/data"
    QOG_SHOT_DIR="${CMAKE_BINARY_DIR}/shots"
    QOG_SINGLE_PLAYER               # one client slot, loopback sockets only
    QOG_EDICT_MARGIN=128            # edicts per map: entity blocks + clients + this
    QOG_MAX_MOD_KNOWN=192           # model table: one level's models, 160 max in shareware
    QOG_MAX_PARTICLES=512           # id's floor; visual trade-off, see docs/changes.md
    QOG_CON_TEXTSIZE=4096           # console scrollback, about 100 lines (id 16 KB, also on the stack)
    QOG_KEY_HISTORY=8               # console command history lines (id 32)
    QOG_MAX_VIEWSIZE=100            # 3D view stays above the status bar; z-buffer 320x152
    QOG_NUMSTACKEDGES=2000          # render tables on the stack, full-game tour peak about 1730 edges on e3m6
    QOG_NUMSTACKSURFACES=800        #   and 626 surfaces (id 2400 and 800)
    QOG_MAXSPANS=1000               # span buffer, overflow only flushes early (id 3000)
    QOG_MAXWIDTH=320                # screen size limit for row and column tables (id 1280x1024)
    QOG_MAXHEIGHT=200
    QOG_MAXALIASVERTS=328           # alias model vertex limit, largest full-game model boss.mdl 325 (id 2000)
    QOG_MAX_BTOFPOLYS=0             # unused back to front debug list compiled out (id 5000)
    QOG_WARP_INPLACE=1)             # underwater warp in the frame buffer, same output (id 64 KB stack buffer)
# BSP structures built at map load, each flag explained where engine/model.h uses it.
target_compile_definitions(quakeog_pc PRIVATE
    QOG_PLANES_EXACT                # planes allocated once, not twice (id's unused second half)
    QOG_MARKSURF16                  # mark surfaces as the file's 16-bit numbers, in place
    QOG_SURF_PACKED                 # surface flags and edge count share a word
    QOG_EDGECACHE16                 # 16-bit edge cache entries, frame window cleared on wrap
    QOG_LEAF_PACKED                 # leaf mark surface index and count as the file's 16 bits
    QOG_SURF_INDEX                  # surface plane and texinfo as the file's 16-bit numbers
    QOG_FRAME16)                    # 16-bit visframe and dlightframe stamps, window refilled on wrap
# Buffers, message sizes and client tables sized from measured peaks.
target_compile_definitions(quakeog_pc PRIVATE
    QOG_LOOP_SERVER_RX=2048         # loopback buffer to the server, measured peak 40 B (id 8 KB)
    QOG_MAX_MSGLEN=6500             # reliable message, measured peak 3838 B, MG24Quake retail 6500 (id 8000)
    QOG_SIGNON_SIZE=6500            # signon buffer, measured peak 3836 B (id 8192)
    QOG_LOOP_CLIENT_RX=7532         # loopback to the client, one message and one datagram (id 8 KB)
    QOG_LOCALSTACK_FROM_PROGS       # QuakeC locals stack sized per progs, 2 KB for shareware (id 8 KB static)
    QOG_MAX_CACHED_PICS=56          # menu pics, 51 possible names in the engine (id 128)
    QOG_CACHEPIC_NAME=24            # menu pic names, longest 16 characters (id 64)
    QOG_STATICS_ON_HUNK             # static entities and efrags from the hunk per level, peaks 44 and 197 (id 33 KB static)
    QOG_ZONE_KB=24                  # zone, measured peak 7.4 KB plus an 8 KB command buffer copy (id 48 KB)
    QOG_CONST_LOCALMODELS           # inline model names as a constant table in flash (id 1.25 KB RAM)
    QOG_SURF_T_PACKED               # edge-drawing surface entries 52 bytes, not id's assembly-padded 64
    QOG_MAX_BMODEL_VERTS=128        # one clipped brush entity polygon, measured peak 22 (id 500)
    QOG_MAX_BMODEL_EDGES=256)       #   and 74 edges (id 1000)
# Renderer stack frames and render tables (Agent D)
target_compile_definitions(quakeog_pc PRIVATE
    QOG_EDGE_T_PACKED               # edge_t 24 bytes with 16-bit links and owner (id 32)
    QOG_ESPAN_T_PACKED              # espan_t 8 bytes, 16-bit coordinates and link (id 16)
    QOG_SURF_T_SMALL                # surf_t 40 bytes with 16-bit links (52 packed, id 64)
    QOG_FINALVERT_PACKED            # finalvert_t 28 bytes, no assembly padding (id 32)
    QOG_ALIAS_NO_AUXVERTS)          # no per-vertex view-space array, the clipper recomputes it
# Same random sequence on every port (port/common/qog_rand.c). Engine and
# generated files only: the port's own qog_rand.c must not be renamed.
set_source_files_properties(${ENGINE_SRC} ${PROGS_AOT}
    PROPERTIES COMPILE_DEFINITIONS "rand=qog_rand")
# id's surface cache (600 KB at 320x200) or per-pixel lighting (engine/d_scanlit.c).
option(QOG_SURFCACHE "use id's surface cache instead of per-pixel lighting" OFF)
if(QOG_SURFCACHE)
    target_compile_definitions(quakeog_pc PRIVATE QOG_SURFCACHE)
endif()
# id's 1996 code is noisy at higher warning levels; keep ours strict later.
target_compile_options(quakeog_pc PRIVATE /W1 /MP)
target_link_libraries(quakeog_pc PRIVATE user32 gdi32 winmm)
# Fixed load address: with the fixed hunk and flash addresses in sys_pc.c,
# every run has the same memory layout, so qog_statehash is repeatable.
target_link_options(quakeog_pc PRIVATE /DYNAMICBASE:NO)
set_target_properties(quakeog_pc PROPERTIES WIN32_EXECUTABLE OFF)
# Baked maps (engine/model.c): read-only BSP structures from id1/qogbake.bin in flash.
target_compile_definitions(quakeog_pc PRIVATE
    QOG_BAKE                        # map loads use the baked image when it matches the packs
    QOG_BAKE_WRITER)                # the qog_bake console command that writes it (PC only)
# Server entities (edicts) and QuakeC memory.
target_compile_definitions(quakeog_pc PRIVATE
    QOG_EDICT_STATS                 # edict use peaks in qog_mem (PC measurement only)
    QOG_EDICT_SPARSE                # edicts keep 38 core field words, the rest as pairs in an arena
    QOG_BASELINE_POOL               # entity baselines only for edicts that get one at spawn
    QOG_AOT_GLOBALS)                # compiled QuakeC: constants in code, temporaries in C locals, packed RAM globals
# qc2c reads the core edict fields from the engine headers (QOG_EDICT_SPARSE)
add_custom_command(OUTPUT ${PROGS_AOT} APPEND
    DEPENDS ${ROOT}/engine/progs.h ${ROOT}/engine/progdefs.q1)
# Client memory and 2D drawing (Agent G)
target_compile_definitions(quakeog_pc PRIVATE
    QOG_CONBACK_FLASH               # console background read in place from the mapped pack (id 62.6 KB cache copy)
    QOG_ENTITY_PACKED               # client entity_t 104 bytes, network origins and angles kept as sent (id 184)
    QOG_PARTICLE_PACKED             # particle_t 36 bytes, byte colour and type, 16-bit list link (id 44)
    QOG_PARTICLE_AVEL8              # entity particle rates as bytes, same floats on use (id 1.9 KB static)
    QOG_CACHEPIC_FLASH)             # menu and intermission pics read in place from the mapped pack (id cache copies)
# Model table, server tables and board wiring (Agent I)
target_compile_definitions(quakeog_pc PRIVATE
    QOG_MOD_FLASH                   # baked model_t used in place in flash, the table holds names and pointers
    QOG_SV_TABLES                   # server precache lists sized below, no map model name copy
    QOG_SV_MAX_MODELS=208           # precache list sizes, full-game peaks 188 models (e3m4) and 154 sounds
    QOG_SV_MAX_SOUNDS=176           #   (id 256, the protocol's byte index)
    QOG_CONST_NOVIS                 # the all-visible PVS row is a constant in flash
    QOG_PVS_LEAFS=4096)             # server PVS rows, largest full-game map 2156 leafs (id 8192)
# Video memory and renderer tables (Agent H). The QOG_ flags that change the
# picture (docs/changes.md) are not in this list.
target_compile_definitions(quakeog_pc PRIVATE
    QOG_CONST_RTABLES               # turbulence, normal and edge tables as constants in flash
    QOG_NO_ROWTABLES                # screen, z and skin row offsets computed, not tabled (3.5 KB)
    QOG_SKY_DIRECT                  # sky composed per drawn pixel, no 16 KB composite
    QOG_HUD_IN_ZBUFFER              # status bar rows of the frame share the z-buffer (15 KB)
    QOG_SPANS_IN_EDGES)             # spans in the free end of the edge table, no 8 KB span buffer
# Flash budget for the board's data image (Agent J)
target_compile_definitions(quakeog_pc PRIVATE
    QOG_BAKE_STUBS                  # qog_bake -bakestubs copies maps into the image, a stub stands for each in the pak
    QOG_DEMOS_OPTIONAL)             # the demo loop plays only the demos present, the board's data has none
# Server, client and system memory (Agent M)
target_compile_definitions(quakeog_pc PRIVATE
    QOG_HUNK_PACKED                 # entity strings and command entries share hunk blocks (id 32 B block each)
    QOG_TRANSLATE_ONDRAW            # player colour table built on the stack when drawn (id 16 KB per player on the hunk)
    QOG_CBUF_INPLACE                # scripts inserted into the command buffer in place, not copied through the zone
    QOG_LOOP_INPLACE                # net_message read in place in the loopback buffers (id 6.5 KB buffer)
    QOG_STATIC_TRIM                 # key tables as bytes, loopback-only network tables, 1 KB spawnparms (exact)
    QOG_EDICT_REUSE                 # all edicts in use, reuse the longest-free one before id's Sys_Error
    QOG_PALETTE_FLASH)              # gfx/palette.lmp read in place from the mapped pack (id 800 B hunk copy)
