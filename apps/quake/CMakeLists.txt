# Quake on the FREE-WILi OG display CPU.
#
# Not part of the default build yet, the engine still needs ~1.8 MB of RAM
# (docs/plans/fit-plan.md), so quake_display cannot link into 264 KB. Build the
# pieces to measure them.
#
#   cmake --build build --target qog_engine      # engine for Cortex-M0+, with stack reports
#   cmake --build build --target quake_display   # full link, reports how far RAM overflows
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
# Client memory and 2D drawing, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_CONBACK_FLASH
    QOG_ENTITY_PACKED
    QOG_PARTICLE_PACKED
    QOG_PARTICLE_AVEL8
    QOG_CACHEPIC_FLASH)
# Model table, server tables and board wiring, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_MOD_FLASH
    QOG_SV_TABLES
    QOG_SV_MAX_MODELS=208
    QOG_SV_MAX_SOUNDS=176
    QOG_CONST_NOVIS
    QOG_PVS_LEAFS=4096)
# Video memory and renderer tables, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_CONST_RTABLES
    QOG_NO_ROWTABLES
    QOG_SKY_DIRECT
    QOG_HUD_IN_ZBUFFER
    QOG_SPANS_IN_EDGES)
# Flash budget for the board's data image, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_BAKE_STUBS
    QOG_DEMOS_OPTIONAL)
# Server, client and system memory, same as pc/CMakeLists.txt except
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
# Per-level map arrays, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_FRAMEBITS
    QOG_DLIGHT_SPARSE
    QOG_LEAF16)
# Renderer temporary memory in the z-buffer, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_EDGES_IN_ZBUFFER
    QOG_EDGECACHE_ZBUFFER
    QOG_SND_ONINIT
    QOG_LOOP_ONINIT
    QOG_PACKDIR_CHUNKED)
# Full game on the board, same as pc/CMakeLists.txt except the
# last one, which is board only.
list(APPEND QOG_ENGINE_DEFS
    QOG_MIN_MEMORY=0
    QOG_SND_OFF_NOHUNK
    QOG_LOADGAME_ZTEMP
    QOG_PRECACHE_ZTEMP
    QOG_KEYS_BYTES
    QOG_EDICT_MARGIN_BOARD=64)      # board only, edicts past the map's estimate, fight tour peak 35 (PC keeps 128)
# Demo playback, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_DEMO_FLASH
    QOG_ITEMBOX_NOLEAFS)
# Renderer speed, exact, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_LITSPAN_FAST
    QOG_LITSPAN_TAILMUL
    QOG_ZSPANS_FAST
    QOG_LITSPAN_ZFUSE
    QOG_ALIASSPAN_FAST              # earlier work, exact: alias spans with their steps in locals (d_polyse.c)
    QOG_FLOAT_EXACT)                # earlier work, exact: single precision where id's double gives the same float (mathlib.h)
# Episode 1 client RAM, exact, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_ENTITY_TIGHT
    QOG_CON_DEBUGLOG_DIRECT)
# Server stack, exact, same as pc/CMakeLists.txt. SV_PushMove's
# lists for sv.max_edicts entries, not 600 (sv_phys.c)
list(APPEND QOG_ENGINE_DEFS
    QOG_PUSHMOVE_ALLOCA)
# World and brush model front end, exact, same as pc/CMakeLists.txt.
list(APPEND QOG_ENGINE_DEFS
    QOG_BMODEL_ZEROANGLE)           # r_bsp.c R_RotateBmodel: zero angles without double trig or matrix products
# Earlier work, lightmaps and dynamic lights, exact, same as pc/CMakeLists.txt (r_light.c, r_surf.c)
list(APPEND QOG_ENGINE_DEFS
    QOG_DLIGHT_AXIAL
    QOG_DLIGHT_FACING
    QOG_DLIGHT_INT
    QOG_DLIGHT_REACHUP
    QOG_GRAD_EXACTF
    QOG_GRAD_FAST)

list(APPEND QOG_ENGINE_DEFS
    QOG_PART_DIEBITS)               # earlier work, exact: particle kill checks as integer compares (r_part.c)
# Earlier work, brush entities and the edge scan with fewer float calls, exact,
# same as pc/CMakeLists.txt (r_bsp.c, r_edge.c, d_edge.c, r_main.c)
list(APPEND QOG_ENGINE_DEFS
    QOG_LEADZ_FLOAT                 # r_edge.c: two brush surfaces of one key ordered by 1/z in float, id's doubles only near a tie
    QOG_BPOLY_AXIAL                 # r_bsp.c R_RecursiveClipBPoly: sides on axial planes by integer compares of the float bits
    QOG_BMODEL_FACING               # r_bsp.c: brush surface facing tests without double, one subtraction on axial planes
    QOG_BMODEL_BBOXF                # r_main.c R_BmodelCheckBBox: float compares instead of double differences
    QOG_BMODEL_FRUSTUM              # r_main.c R_DrawBEntitiesOnList: view planes of entities that do not turn from the last ones
    QOG_DSURF_NOFRUSTUM             # d_edge.c D_DrawSurfaces: brush surfaces turn no view planes, set back once at the end
    QOG_SCAN_FAST                   # r_edge.c: span generation in one loop with its globals in locals (round 2)
    QOG_BPOLY_SIDES                 # r_bsp.c: brush clip sides inline, a point shared with the edge before keeps its side
    QOG_BMSPLIT_INT                 # r_efrag.c R_SplitEntityOnNode2: axial box tests by integer compares of the float bits
    QOG_LMAP_FUSE                   # r_surf.c R_BuildLightMap: the grid's passes fused, the same cells
    QOG_BMODEL_BBOXINT              # r_main.c R_BmodelCheckBBox: integer plane tests with a margin first (with QOG_WORLDNODE_INT)
    QOG_SUBDIV_LEAF                 # d_polyse.c: subdivision leaves tested inline, no call (round 3)
    QOG_LEFTEDGE_LOCALS             # d_polyse.c D_PolysetScanLeftEdge: the edge's state in locals
    QOG_ALIAS_SMALLTRI)             # d_polyse.c: triangles with no pixel or one drawn without gradients
# Earlier work, one app for every episode's data image. The start level comes
# from the image (qogboot.txt), a level exit or a save of a level the image
# does not hold goes to that level or is not loaded (engine/host_cmd.c)
list(APPEND QOG_ENGINE_DEFS
    QOG_BOOT_ENTRY
    QOG_MISSING_MAPS)

file(GLOB QOG_ENGINE_SRC ${QOG_ROOT}/engine/*.c)
list(REMOVE_ITEM QOG_ENGINE_SRC ${QOG_ROOT}/engine/snd_null.c)

# QuakeC runs in id's interpreter, read in place from flash. The compiled
# path (tools/qc2c.py) is opt-in and needs registered data (docs/lessons.md L39).
set(QOG_PROGS_AOT ${CMAKE_CURRENT_BINARY_DIR}/progs_aot.c)
# QuakeOG, -DQOG_AOT=ON compiles QuakeC to C, as the PC build does,
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
# id's 1996 code is noisy. -fstack-usage writes a .su file per object so the
# biggest stack frames can be found before they overflow the board's stack.
target_compile_options(qog_engine PRIVATE -w -Wno-implicit-function-declaration -fstack-usage)
# One random sequence on every port (port/common/qog_rand.c).
set_source_files_properties(${QOG_ENGINE_SRC} ${QOG_PROGS_AOT}
    PROPERTIES COMPILE_DEFINITIONS "rand=qog_rand")
# The compiled QuakeC reads a global as a float (G) and writes it back as an int
# (I) in one loop, like anglemod's while (v < 0) v += 360. With strict aliasing GCC
# may keep the float in a register, never see the store and spin forever, the
# board's server hung on exactly that loop. The PC compiler does not do it.
set_source_files_properties(${QOG_PROGS_AOT} ${QOG_ENGINE_SRC}
    PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing")
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
# QuakeOG, hunk size in KB (port/rp2040/sys_rp2040.c).
# Earlier work, the measured E1M1 need with QOG_AOT and the board-only flags, a
# fight, a save and a load (PC build with the same flags, smallest
# -heapsize that runs it). Play without a load needs 187 KB, interpreted
# QuakeC 38 KB more for its globals.
set(QOG_HUNK_KB 194 CACHE STRING "board hunk size in KB")
target_compile_definitions(quake_display PRIVATE QOG_HUNK_KB=${QOG_HUNK_KB})
target_link_libraries(quake_display qog_engine qog_platform fwog_display_bsp)
# QuakeOG, the honest board configuration. The game's stack is an
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
# Earlier work, the display app's slot ends at the data region (913,408 B with
# the data at 0x100000), so a display app that outgrows it fails the build
qog_check_app_size(quake_display)

# QuakeOG, the two-chip split. The server half (SV_*, QuakeC,
# edicts) runs on the MAIN RP2040 as quake_main, the client half (CL_*,
# renderer, status bar, input) on the DISPLAY RP2040 as quake_display. They
# talk over the inter-CPU UART through the link net driver (engine/net_link.c,
# port/common/qog_link.c, port/rp2040/link_rp2040.c). Each half gets its own
# engine library from qog_engine's sources and flags, as apps/qogfly does.
# -DQOG_SPLIT=OFF builds quake_display as the single-chip game again.
#
#   cmake --build build --target quake_main   # both halves, main embeds quake_display
option(QOG_SPLIT "quake_display is the client half and quake_main the server half" ON)
if(QOG_SPLIT)
    get_target_property(_split_src qog_engine SOURCES)
    get_target_property(_split_defs qog_engine COMPILE_DEFINITIONS)
    set(_split_common
        ${QOG_ROOT}/port/common/qog_link.c
        ${QOG_ROOT}/port/common/qog_mem.c
        ${QOG_ROOT}/port/common/qog_rand.c)
    # QuakeOG, both halves print where the frame time goes over USB
    # every 2 s (engine/qog_phase.h, port/rp2040/sys_rp2040.c). The client
    # prints "[time cl]" lines, the server "[time sv]". About 2,000 timer
    # reads and three span list walks a frame, 160 bytes of RAM per half.
    option(QOG_BOARD_TIMING "both halves print frame phase times over USB every 2 s" ON)
    if(QOG_BOARD_TIMING)
        list(APPEND _split_common ${QOG_ROOT}/port/common/qog_phase.c)
    endif()

    # client half, no server, QuakeC or edicts
    set(_cl_src ${_split_src})
    list(FILTER _cl_src EXCLUDE REGEX "/(sv_[a-z]+|pr_[a-z]+|net_loop|progs_aot|qog_mem|qog_rand)\.c$")
    add_library(qog_engine_cl STATIC EXCLUDE_FROM_ALL ${_cl_src} ${_split_common})
    # QuakeOG, the client half's own settings, board only, never in
    # the PC game list (a PC client gets them with -DQOG_SPLIT_CL_EXTRA)
    set(_cl_defs ${_split_defs})
    list(TRANSFORM _cl_defs REPLACE "^QOG_NUMSTACKEDGES=[0-9]+$" "QOG_NUMSTACKEDGES=1800")   # the edge table fits the one-byte z-buffer (full game peak 1,730)
    # The edge-drawing surface table lives on the client's stack (32 B each).
    # 800 covers the whole game (e3m6 peak 670). The board's data image holds
    # episode 1 (peak 488, e1m8), and the first board run overflowed the 34 KB
    # stack under R_EdgeDrawing's BSP recursion, so the board client sizes it
    # for the episode it carries. A view past it drops surfaces, never crashes.
    set(QOG_CL_SURFACES 600 CACHE STRING "client half edge-drawing surfaces (episode 1 peak 488, whole game 670)")
    list(TRANSFORM _cl_defs REPLACE "^QOG_NUMSTACKSURFACES=[0-9]+$" "QOG_NUMSTACKSURFACES=${QOG_CL_SURFACES}")
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
        QOG_ZEDGE_STACK_GUARD=1024      # the earlier edge fallback, when the stack cannot hold it the earlier draws those spans without z (r_edge.c)
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
        QOG_CMD_ARGV_ARENA=640          # command tokens in a 640-byte buffer, not the zone (it fragmented a small zone)
        QOG_MSG_STRING_INPLACE          # earlier work: message strings read in place, no 2 KB static copy (exact, common.c)
        QOG_LINK_RX_ZTEMP               # earlier work: the link's 7.5 KB receive buffer in the z-buffer between views, not the hunk (net_link.c)
        QOG_TEMPENT_SMALL               # earlier work: beam segments are only the drawn part of entity_t, 40 bytes not 88 (exact, cl_tent.c)
        QOG_HUNK_HDR8                   # earlier work: 8-byte hunk block headers and 8-byte rounding, not 16 (exact, zone.c)
        QOG_ITEMBOX_PACKED              # earlier work: item box frame bits share packed hunk blocks (exact, model.c)
        QOG_ENTITY_NOEFRAG              # earlier work: efrag and visframe of statics share the client entity's network part (exact, render.h)
        QOG_ENTITY_NOTOPNODE            # earlier work: the drawn bmodel's top node in one variable, entities 76 bytes (exact, render.h)
        QOG_CACHEPIC_NAMEPTR            # earlier work: menu pic names point into the mapped pack's directory, 672 B not 1,792 (exact, draw.c)
        QOG_LIGHTSTYLE_SHORT)           # earlier work: light style values as shorts, 512 B (exact, r_shared.h)
    # QuakeOG, menus for five buttons and no keyboard (engine/menu.c).
    # No Multiplayer or console entry, a Controls page that shows the buttons,
    # a Levels page of the maps in the data image, yes/no answered with gray
    # and green + blue. The network menus are left out, which frees RAM.
    list(APPEND _cl_defs QOG_BOARD_MENU)
    # QuakeOG, sound. id's mixer runs on the server half for the
    # client half's sound calls (engine/snd_link.c, engine/net_link.c), which
    # plays the 8-bit samples it sends back on the I2S amplifier through PIO
    # and DMA (port/rp2040/snd_rp2040.c). -DQOG_SOUND=OFF builds both halves
    # with -nosound as before.
    option(QOG_SOUND "split game sound: mixed on main, played by the display" ON)
    if(QOG_SOUND)
        list(APPEND _cl_defs QOG_SND_REMOTE)
    endif()
    # QuakeOG, speed, board client only (docs/changes.md)
    option(QOG_BOARD_LOCKWAIT "the next frame's input, link and parsing run during the LCD scanout (exact)" ON)
    option(QOG_BOARD_CORE1 "the display chip's second core draws half of the lit spans (exact)" ON)
    option(QOG_BOARD_PIPELINE "the client draws a frame while the server half runs the next one" ON)
    if(QOG_BOARD_LOCKWAIT)
        list(APPEND _cl_defs QOG_VID_LOCKWAIT)     # VID_Update does not wait, VID_LockBuffer does (vid_rp2040.c)
    endif()
    if(QOG_BOARD_CORE1)
        list(APPEND _cl_defs QOG_SPANS_CORE1)      # every second span of a lit surface on core 1 (d_scanlit.c)
    endif()
    if(QOG_BOARD_PIPELINE)
        list(APPEND _cl_defs QOG_LINK_PIPELINE)    # a frame's screen update runs while the server half runs the next frame (host.c)
    endif()
    # QuakeOG, the frame outside the 3D view, board only (docs/changes.md)
    option(QOG_BOARD_LINKOVERLAP "main sends its link frames by DMA, so its frame runs while the display draws (exact)" ON)
    option(QOG_BOARD_PIPEDEMO "demos and timedemo use the pipelined screen update too (exact)" ON)
    option(QOG_BOARD_SCANRACE "the 3D view starts while the last frame goes to the LCD, waiting only for the rows it writes (exact)" ON)
    option(QOG_BOARD_HUDSKIP "the status bar rows go to the LCD only when they change (exact)" ON)
    list(APPEND _cl_defs QOG_LINK_TXRINGSEND)      # link frames written from the ring, no 514-byte frame buffer (qog_link.c)
    set(_ak_sv_defs "")
    if(QOG_BOARD_LINKOVERLAP)
        list(APPEND _cl_defs QOG_LINK_OVERLAP QOG_LINK_LISTEN_GAP=10)   # nothing sent while it draws (net_link.c)
        list(APPEND _ak_sv_defs QOG_LINK_TXASYNC QOG_LINK_LISTEN_GAP=10) # frames by DMA (link_rp2040.c)
    endif()
    if(QOG_BOARD_PIPEDEMO AND QOG_BOARD_PIPELINE)
        list(APPEND _cl_defs QOG_LINK_PIPELINE_DEMO)   # host.c Host_PipelineCan
    endif()
    if(QOG_BOARD_SCANRACE AND QOG_BOARD_LOCKWAIT)
        list(APPEND _cl_defs QOG_VID_SCANRACE)     # vid_rp2040.c, r_main.c, r_draw.c, screen.c
        # QuakeOG, a scanout that sends the status bar rows sends
        # them first, so the next view's z-buffer writes wait for 48 rows
        option(QOG_BOARD_HUDFIRST "the status bar rows go to the LCD before the 3D view rows (exact)" ON)
        if(QOG_BOARD_HUDFIRST)
            list(APPEND _cl_defs QOG_VID_HUDFIRST)  # vid_rp2040.c, platform/qog_video.c
        endif()
        # QuakeOG, the edge scan's spans wait row by row for the
        # last scanout instead of for its end
        option(QOG_BOARD_ROWRACE "R_ScanEdges waits for the scanout row by row (exact)" ON)
        if(QOG_BOARD_ROWRACE)
            list(APPEND _cl_defs QOG_VID_ROWRACE)   # r_main.c, r_edge.c
        endif()
        # QuakeOG, outside Turbo the world's edge cache goes to the end
        # of the z-buffer region, which no scanout reads, while the world is drawn
        option(QOG_BOARD_ECTAIL "the edge cache past the rows a scanout sends (exact)" ON)
        if(QOG_BOARD_ECTAIL)
            list(APPEND _cl_defs QOG_EDGECACHE_TAIL)   # r_main.c, r_draw.c
        endif()
    endif()
    if(QOG_BOARD_HUDSKIP)
        list(APPEND _cl_defs QOG_HUD_SKIP)         # r_main.c, screen.c, vid_rp2040.c
        # QuakeOG (approved by the user), in Turbo (r_fast 2) the status
        # bar rows go to the LCD at most every r_hudcap ms (100), the newest state
        # at the next slot. Exact and Fast are untouched
        option(QOG_BOARD_HUDCAP "with HUDSKIP, Turbo sends the status bar at most every r_hudcap ms" ON)
        if(QOG_BOARD_HUDCAP)
            list(APPEND _cl_defs QOG_HUD_CAP)      # r_main.c R_QogHudCapHold
        endif()
    endif()
    # QuakeOG, world front end speed, exact (same pixels, docs/changes.md)
    option(QOG_BOARD_VERTCACHE "world vertices projected once a frame through a cache in free frame buffer rows (exact)" ON)
    option(QOG_BOARD_WORLDNODE "BSP node culling and side tests in float compares, no double arithmetic (exact)" ON)
    if(QOG_BOARD_VERTCACHE)
        list(APPEND _cl_defs QOG_VERTCACHE)        # r_draw.c R_QogProject
    endif()
    if(QOG_BOARD_WORLDNODE)
        list(APPEND _cl_defs QOG_WORLDNODE_FAST)   # r_bsp.c R_RecursiveWorldNode
        list(APPEND _cl_defs QOG_NODESIDE_BITS)    # earlier work: its side and facing tests by the float's bits, no float compares
    endif()
    option(QOG_BOARD_CLIPDIST "with VERTCACHE, each world vertex's distance to the view planes measured once a frame (exact)" ON)
    if(QOG_BOARD_VERTCACHE AND QOG_BOARD_CLIPDIST)
        list(APPEND _cl_defs QOG_VERTCACHE_CLIPDIST)   # r_draw.c R_QogClipDist
    endif()
    # Flash miss experiments for the lit spans, exact, off until measured on
    # the board, texels or the colour map read through the XIP no-allocate
    # alias, so their misses do not evict code (d_scanlit.c)
    option(QOG_BOARD_TEXNOALLOC "lit span texels through the XIP no-allocate alias (exact, measurement)" OFF)
    option(QOG_BOARD_CMAPNOALLOC "lit span colour map through the XIP no-allocate alias (exact, measurement)" OFF)
    if(QOG_BOARD_TEXNOALLOC)
        list(APPEND _cl_defs QOG_LIT_TEX_NOALLOC)
    endif()
    if(QOG_BOARD_CMAPNOALLOC)
        list(APPEND _cl_defs QOG_LIT_CMAP_NOALLOC)
    endif()
    option(QOG_BOARD_ALIASCORE1 "with CORE1, alias model vertices shared by both cores (exact)" ON)
    if(QOG_BOARD_CORE1 AND QOG_BOARD_ALIASCORE1)
        list(APPEND _cl_defs QOG_ALIAS_CORE1)      # r_alias.c R_QogAliasPrepareRange
    endif()
    # QuakeOG, core 1 draws whole lit world surfaces while core 0
    # scans the edges, and both share each flush's surfaces (exact)
    option(QOG_BOARD_DRAWCORE1 "with CORE1, the world's lit surfaces drawn by both cores during the edge scan (exact)" ON)
    if(QOG_BOARD_CORE1 AND QOG_BOARD_DRAWCORE1)
        list(APPEND _cl_defs QOG_DRAW_CORE1)       # d_edge.c, d_core1.c, r_edge.c
    endif()
    # QuakeOG, one copy of that drawing code for both cores, which reaches
    # each core's values through the core number (exact). OFF compiles it a
    # second time for core 1, with variables of its own
    option(QOG_BOARD_SHAREDDRAW "with DRAWCORE1, one copy of the lit surface drawing for both cores (exact)" ON)
    if(QOG_BOARD_CORE1 AND QOG_BOARD_DRAWCORE1 AND QOG_BOARD_SHAREDDRAW)
        list(APPEND _cl_defs QOG_DRAW_SHARED)      # d_drawctx.h
    endif()
    # QuakeOG, core 1 moves three of every five particles once
    # core 0 has drawn them (exact)
    option(QOG_BOARD_PARTSCORE1 "with DRAWCORE1, particle physics shared by both cores (exact)" ON)
    if(QOG_BOARD_CORE1 AND QOG_BOARD_DRAWCORE1 AND QOG_BOARD_PARTSCORE1)
        list(APPEND _cl_defs QOG_PARTS_CORE1)      # r_part.c
    endif()
    if(QOG_BOARD_TIMING)
        list(APPEND _cl_defs QOG_PHASE_TIME)
    endif()
    # QuakeOG, measurement builds, off by default, with
    # QOG_BOARD_TIMING. XIPSTATS adds "[time cl] xip" (XIP cache accesses and
    # misses per view in each render phase) and "[time cl] work" (the world
    # front end's nodes, faces and edges per view). FPCOUNT adds the "[time
    # cl] fp" lines (float and double helper calls per view and phase, their
    # measured cost) and implies XIPSTATS. FPCOUNT renames the engine
    # library's helper calls to counting stubs, so its times run a little slow.
    option(QOG_BOARD_XIPSTATS "measurement: XIP cache counts per render phase and world work counts" OFF)
    option(QOG_BOARD_FPCOUNT "measurement: float and double helper calls per view and phase (implies XIPSTATS)" OFF)
    if(QOG_BOARD_TIMING AND (QOG_BOARD_XIPSTATS OR QOG_BOARD_FPCOUNT))
        list(APPEND _cl_defs QOG_PHASE_XIP)
        if(QOG_BOARD_FPCOUNT)
            list(APPEND _cl_defs QOG_PHASE_FPCOUNT)
            target_sources(quake_display PRIVATE
                ${QOG_ROOT}/port/rp2040/qog_fpcount.S ${QOG_ROOT}/port/rp2040/qog_fpcount.c)
            add_custom_command(TARGET qog_engine_cl POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} --redefine-syms=${QOG_ROOT}/port/rp2040/qog_fpcount.syms
                        $<TARGET_FILE:qog_engine_cl>
                COMMENT "QOG_BOARD_FPCOUNT: engine helper calls go through the counting stubs")
        endif()
    endif()
    # QuakeOG, code placement for the shared 16 KB XIP cache, exact
    # (the same code at other addresses, no RAM). The hot render functions go
    # to the front of flash text, grouped by core and phase in
    # port/rp2040/ld_hottext/default_text_excludes.incl, so the code that runs
    # together is contiguous (tools/xip_sets.py counts the set overlaps)
    option(QOG_BOARD_HOTTEXT "hot render code grouped at the front of flash by core and phase (exact, layout only)" OFF)
    if(QOG_BOARD_HOTTEXT)
        pico_add_linker_script_override_path(quake_display ${QOG_ROOT}/port/rp2040/ld_hottext
            FILES default_text_excludes.incl)
    endif()
    # QuakeOG, the edge scan (r_edge.c) built with -Os, exact. With the two
    # drawing cores its code is more than the XIP cache holds, and the smaller
    # code leaves fewer lines in conflict (tools/xip_sets.py). Off until a
    # board run shows what it saves
    option(QOG_BOARD_SCANSIZE "r_edge.c built for size, fewer XIP cache conflicts in the edge scan (exact)" OFF)
    if(QOG_BOARD_SCANSIZE)
        set_property(SOURCE ${QOG_ROOT}/engine/r_edge.c APPEND PROPERTY COMPILE_OPTIONS -Os)
    endif()
    # QuakeOG, the Fast render mode, approximate and hard to see,
    # switched at run time by the r_fast cvar (Options, Render Exact / Fast).
    # On by default on the board. r_fast 0 draws exactly as before.
    option(QOG_BOARD_FAST "the r_fast cvar and the Fast render mode, on by default (d_litspan.h)" ON)
    if(QOG_BOARD_FAST)
        list(APPEND _cl_defs QOG_RFAST QOG_RFAST_ON)
    endif()
    # QuakeOG, Turbo, a third render mode (r_fast 2, Options, Render
    # Exact / Fast / Turbo), the 3D view drawn at half width with Fast mode's
    # shortcuts and doubled sideways by the LCD scanout (engine/r_qogturbo.c)
    option(QOG_BOARD_TURBO "with FAST, the Turbo render mode (r_fast 2), a half width 3D view" ON)
    if(QOG_BOARD_FAST AND QOG_BOARD_TURBO)
        list(APPEND _cl_defs QOG_RTURBO)
        # Earlier work, in Turbo a model triangle within 3 x 3 pixels is drawn
        # without gradients, its pixels in the first corner's texel and light
        option(QOG_BOARD_TURBOTINY "with TURBO, tiny model triangles without gradients in Turbo" ON)
        if(QOG_BOARD_TURBOTINY)
            list(APPEND _cl_defs QOG_ALIAS_TURBOTINY)   # d_polyse.c D_QogTinyTriangle
        endif()
    endif()
    # QuakeOG, the LCD's pixel stream from a PIO state machine at
    # clk_sys over a whole number of clocks a bit, at most QOG_LCD_PIO_KHZ
    # (300 MHz gives 60 MHz, 300 / 5, where the PL022 gives 50, 300 / 6, and
    # no gap between pixels). OFF sends the pixels through the PL022 as before
    option(QOG_LCD_PIO "LCD pixels through PIO (platform/qog_video.c), OFF for the SPI peripheral" ON)
    set(QOG_LCD_PIO_KHZ 62500 CACHE STRING "highest LCD bit rate in kHz for QOG_LCD_PIO (the panel's 62.5 MHz)")
    if(QOG_LCD_PIO)
        list(APPEND _cl_defs QOG_LCD_PIO QOG_LCD_PIO_KHZ=${QOG_LCD_PIO_KHZ})
        message(STATUS "qog: LCD pixels through PIO, at most ${QOG_LCD_PIO_KHZ} kHz")
    endif()
    # QuakeOG, in Fast mode alias models (monsters, items, the view
    # weapon) and particles are made in integers, r_fast 0 draws as before
    option(QOG_BOARD_ALIASFAST "with FAST, alias vertices, triangle gradients and particles in integers in Fast mode" ON)
    if(QOG_BOARD_FAST AND QOG_BOARD_ALIASFAST)
        list(APPEND _cl_defs QOG_ALIAS_FAST)      # r_alias.c, d_polyse.c, d_part.c
    endif()
    # QuakeOG, in Fast mode the world and brush model edges are
    # projected and stepped in integers (r_draw.c), r_fast 0 is unchanged
    option(QOG_BOARD_FRONTFAST "Fast mode projects and sets up edges in fixed point (needs FAST, VERTCACHE, CLIPDIST)" ON)
    if(QOG_BOARD_FRONTFAST AND QOG_BOARD_FAST AND QOG_BOARD_VERTCACHE AND QOG_BOARD_CLIPDIST)
        list(APPEND _cl_defs QOG_FRONT_FAST)
        # exact, both modes, BSP node frustum tests in integers with a
        # margin, id's floats only near a plane (r_bsp.c R_QogCullNodeInt)
        option(QOG_BOARD_WORLDNODEINT "with FRONTFAST and WORLDNODE, node culling in integers first (exact)" ON)
        if(QOG_BOARD_WORLDNODEINT AND QOG_BOARD_WORLDNODE)
            list(APPEND _cl_defs QOG_WORLDNODE_INT)
        endif()
        # Earlier work, the same Fast values with fewer float calls (r_draw.c).
        # a brush entity's integer setup made again only for its origin when
        # its vectors are those of the setup before, and face gradients with
        # an exact axial dot product and one multiply less per gradient
        list(APPEND _cl_defs QOG_FX_REUSE QOG_FGRAD_AXIAL)
        # Earlier work round 4, a world vertex's four view plane distances at its
        # first need (r_draw.c)
        list(APPEND _cl_defs QOG_CLIPDIST_FILL4)
    endif()
    # Earlier work, the console's text 4 KB to 1 KB (26 lines of scrollback, the
    # 4 notify lines, MG24Quake keeps 913 B). It is taken from the client's
    # hunk, so the hunk below shrinks by the same 3 KB and every level keeps
    # The earlier margin, and the 3 KB become static RAM (docs/changes.md)
    # Earlier work, Fast mode's pixel loop takes the texel address and the colour
    # map row from the SIO interpolators (d_litspan.h, the same pixels)
    option(QOG_BOARD_INTERP "Fast lit spans address texels through the interpolators" ON)
    if(QOG_BOARD_FAST AND QOG_BOARD_INTERP)
        list(APPEND _cl_defs QOG_LITSPAN_INTERP)
    endif()
    # Earlier work, Fast and Turbo lit spans with 32-bit span starts, the z
    # written by the pixel loop, and the runs of light and pixels in
    # hand-written Thumb in RAM (engine/d_lit_q.h, port/rp2040/d_litq_rp2040.S)
    option(QOG_BOARD_LITQ "with FAST and INTERP, the Q span drawer for Fast and Turbo" ON)
    if(QOG_BOARD_FAST AND QOG_BOARD_INTERP AND QOG_BOARD_LITQ)
        list(APPEND _cl_defs QOG_LITSPAN_Q QOG_LITQ_ASM)
        target_sources(qog_engine_cl PRIVATE ${QOG_ROOT}/port/rp2040/d_litq_rp2040.S)
        # each light run of 8 pixels ends at the next run's start, one blend a
        # run instead of two (0.23% of Fast pixels, single colour map rows)
        option(QOG_BOARD_LITQSHARE "with LITQ, light runs share their ends (Fast and Turbo)" ON)
        if(QOG_BOARD_LITQSHARE)
            list(APPEND _cl_defs QOG_LITQ_SHARELIGHT)
        endif()
    endif()
    # Earlier work, the alias span drawer (models, the view weapon) in
    # hand-written Thumb, checked against the C at start-up (d_polyse.c,
    # port/rp2040/d_aspan_rp2040.S), exact
    option(QOG_BOARD_ASPANASM "alias span pixels in hand-written Thumb (exact)" ON)
    if(QOG_BOARD_ASPANASM)
        list(APPEND _cl_defs QOG_ASPAN_ASM)
        target_sources(qog_engine_cl PRIVATE ${QOG_ROOT}/port/rp2040/d_aspan_rp2040.S)
    endif()
    option(QOG_BOARD_CONSMALL "client console text 1 KB, its hunk 3 KB smaller" ON)
    if(QOG_BOARD_CONSMALL)
        list(TRANSFORM _cl_defs REPLACE "^QOG_CON_TEXTSIZE=[0-9]+$" "QOG_CON_TEXTSIZE=1024")
    endif()
    target_compile_definitions(qog_engine_cl PUBLIC ${_cl_defs} QOG_NET_LINK QOG_SPLIT_CLIENT)

    # server half, no client, renderer, console or sound. Its command buffer
    # is never used (the client's is the game's), so it gets a small one.
    set(QOG_SPLIT_SV_ENGINE cmd common crc cvar host host_cmd mathlib model net_link net_main net_none
        net_vcr pr_cmds pr_edict pr_exec sv_main sv_move sv_phys sv_user world zone)
    list(TRANSFORM QOG_SPLIT_SV_ENGINE PREPEND ${QOG_ROOT}/engine/)
    list(TRANSFORM QOG_SPLIT_SV_ENGINE APPEND .c)
    set(_sv_defs ${_split_defs})
    list(REMOVE_ITEM _sv_defs QOG_LOADGAME_ZTEMP QOG_PRECACHE_ZTEMP)
    list(TRANSFORM _sv_defs REPLACE "^QOG_CMDBUF_SIZE=[0-9]+$" "QOG_CMDBUF_SIZE=256")
    list(APPEND _sv_defs QOG_LOADGAME_STRSIZE=8192)   # a save's biggest block 2,485 B (id 32 KB on the hunk)
    list(APPEND _sv_defs QOG_LOADGAME_STACKBUF)       # earlier work: that buffer on the stack once the map has spawned, not on the hunk (e2m7 179 to 171 KB)
    list(APPEND _sv_defs QOG_RECONNECT_FIRST)         # earlier work: the client's reconnect before its message, else every changelevel stopped the client (sv_main.c)
    list(APPEND _sv_defs ${_ak_sv_defs})              # earlier work: QOG_BOARD_LINKOVERLAP
    list(TRANSFORM _sv_defs REPLACE "^QOG_ZONE_BOARD_KB=[0-9]+$" "QOG_ZONE_BOARD_KB=6")   # server zone peak 1.9 KB
    set(_sv_sound "")
    if(QOG_SOUND)
        # Earlier work, id's mixer for the client half, exact for the same calls
        set(_sv_sound ${QOG_ROOT}/engine/snd_dma.c ${QOG_ROOT}/engine/snd_mix.c ${QOG_ROOT}/engine/snd_mem.c
            ${QOG_ROOT}/port/common/qog_snd_host.c)
        list(APPEND _sv_defs
            QOG_SND_HOST                # the client half's view entity and ambient levels come with its calls
            QOG_SND_GETSAMPLES          # id's GetSoundtime path for an output that counts its samples
            QOG_SFX_NAMEPTR             # sound names point into the pack directory in flash (8 bytes a sound, id 68)
            QOG_MAX_SFX=256             # the registered game has 225 sound files (id 512)
            QOG_MAX_CHANNELS=104        # 12 plus static sounds, the whole game uses at most 99 (e2m6), id 128
            QOG_SND_HOSTMEM             # paint buffer and known_sfx from qog_snd_host.c, not the hunk
            QOG_SND_BOARDMEM)           # paint buffer and ring at the bottom of the server's stack between frames, known_sfx in SCRATCH_Y
    endif()
    add_library(qog_engine_sv STATIC EXCLUDE_FROM_ALL ${QOG_SPLIT_SV_ENGINE} ${QOG_PROGS_AOT}
        ${QOG_ROOT}/port/common/qog_split_sv.c ${_split_common} ${_sv_sound})
    target_compile_definitions(qog_engine_sv PUBLIC ${_sv_defs} QOG_NET_LINK QOG_SPLIT_SERVER
        QOG_LINK_SCRATCH)   # the link's 4 KB of rings in SCRATCH_X, unused on main
    if(QOG_BOARD_TIMING)
        target_compile_definitions(qog_engine_sv PUBLIC QOG_PHASE_TIME)   # earlier work
    endif()

    foreach(_lib qog_engine_cl qog_engine_sv)
        # both ends of the link use the same frame size, 512-byte frames
        target_compile_definitions(${_lib} PUBLIC QOG_LINK_CHUNK=512)
        target_include_directories(${_lib} PUBLIC
            ${QOG_ROOT}/engine ${QOG_ROOT}/port/common ${QOG_ROOT}/platform)
        target_compile_options(${_lib} PRIVATE -w -Wno-implicit-function-declaration -fstack-usage)
        target_compile_options(${_lib} PUBLIC -fno-short-enums)   # same layout as the PC and the baked image
        target_link_options(${_lib} INTERFACE -Wl,--no-enum-size-warning)
        target_link_libraries(${_lib} PUBLIC pico_stdlib)
    endforeach()
    # the server's rings are 2 KB. Earlier work, the client's are 1 KB, a frame that
    # does not fit is not acknowledged and comes again (exact, slower only in
    # the bursts of a level change)
    target_compile_definitions(qog_engine_sv PUBLIC QOG_LINK_TXRING=2048 QOG_LINK_RXRING=2048)
    target_compile_definitions(qog_engine_cl PUBLIC QOG_LINK_TXRING=1024 QOG_LINK_RXRING=1024)
    target_link_libraries(qog_engine_cl PUBLIC fwog_display_bsp)
    target_link_libraries(qog_engine_sv PUBLIC fwog_main_bsp)

    # the display app becomes the client half (the same sources, another library)
    target_sources(quake_display PRIVATE ${QOG_ROOT}/port/rp2040/link_rp2040.c)
    # the game owns the power poll, red powers off only when held alone (port/rp2040/power_qog.c)
    target_sources(quake_display PRIVATE ${QOG_ROOT}/port/rp2040/power_qog.c)
    target_compile_definitions(quake_display PRIVATE QOG_POWER_RED_ALONE)
    get_target_property(_disp_libs quake_display LINK_LIBRARIES)
    list(TRANSFORM _disp_libs REPLACE "^qog_engine$" "qog_engine_cl")
    set_property(TARGET quake_display PROPERTY LINK_LIBRARIES ${_disp_libs})
    set(QOG_CL_HUNK_KB 75 CACHE STRING "client half hunk in KB. Earlier work: every episode, the worst fight tours on the board images e2m6 and e4m5, see docs/changes.md (earlier work had 73 for episode 1)")
    # Earlier work, with sound the client gives 2 KB of its stack to the speaker's ring
    # (the board's watermark had 5.4 KB spare, the sound exchange adds about 1.1 KB)
    if(QOG_SOUND)
        set(_cl_stack_default 32)
    else()
        set(_cl_stack_default 34)
    endif()
    set(QOG_CL_STACK_KB ${_cl_stack_default} CACHE STRING "client half stack array in KB (tools/stack_chain.py from game_main, docs/changes.md)")
    get_target_property(_disp_defs quake_display COMPILE_DEFINITIONS)
    set(_cl_hunk_kb ${QOG_CL_HUNK_KB})
    if(QOG_BOARD_CONSMALL)
        math(EXPR _cl_hunk_kb "${QOG_CL_HUNK_KB} - 3")   # earlier work: the console's 3 KB (above)
    endif()
    list(TRANSFORM _disp_defs REPLACE "^QOG_HUNK_KB=[0-9]+$" "QOG_HUNK_KB=${_cl_hunk_kb}")
    list(TRANSFORM _disp_defs REPLACE "^QOG_STACK_KB=[0-9]+$" "QOG_STACK_KB=${QOG_CL_STACK_KB}")
    set_property(TARGET quake_display PROPERTY COMPILE_DEFINITIONS ${_disp_defs})
    # Earlier work, the BSP's frame sender without its 4 KB frame buffer (port/rp2040/link_rp2040.c)
    target_compile_definitions(quake_display PRIVATE QOG_LINK_STREAM_SEND QOG_VID_SURFCACHE_MIN)   # and a 64-byte surface cache header (vid_rp2040.c)
    # the level the board client starts in, since the board has no keyboard
    # for the console (empty, id's start, quake.rc and the demo loop)
    # Earlier work, with QOG_BOARD_BOOT (the default) the data image names it
    # (qogboot.txt from tools/mkboard.py --boot-map, e1m1 when it has none)
    # and QOG_BOARD_MAP is not used
    option(QOG_BOARD_BOOT "the board client starts in the level its data image names (qog_boot)" ON)
    set(QOG_BOARD_MAP "e1m1" CACHE STRING "level the board client starts in with QOG_BOARD_BOOT off, empty for none")
    # bring-up, both halves print link wait counters once a second over USB (port/rp2040/link_rp2040.c)
    option(QOG_LINK_DIAG "print link wait counters while the link waits" ON)
    if(QOG_LINK_DIAG)
        target_compile_definitions(quake_display PRIVATE QOG_LINK_DIAG QOG_STACK_WATERMARK)
    endif()
    # the game on the process stack, interrupts on the SDK's main stack (sys_rp2040.c)
    target_compile_definitions(quake_display PRIVATE QOG_GAME_PSP)
    if(QOG_BOARD_BOOT)
        target_compile_definitions(quake_display PRIVATE QOG_BOARD_BOOT)
    elseif(QOG_BOARD_MAP)
        target_compile_definitions(quake_display PRIVATE QOG_BOARD_MAP="${QOG_BOARD_MAP}")
    endif()
    target_link_options(quake_display PRIVATE -Wl,--wrap=fwog_link_uart_send_frame)

    set(QOG_SV_HUNK_KB 174 CACHE STRING "server half hunk in KB. Earlier work, with sound, a fight, a save and a load need start 96, e1m1 117, e1m2 144, e1m3 171, e1m4 161, e1m5 160, e1m6 146, e1m7 93, e1m8 121")
    set(QOG_SV_STACK_KB 21 CACHE STRING "server half stack array in KB. Earlier work, measured in board bytes on the PC (QOG_SV_STACKPROBE) the deepest episode 1 chain is 10,984 B, a constructed worst case (a pushed grenade on e1m3's deepest hull 0 leaf) about 16.4 KB. The mixer needs 1,816 B above its 6 KB at the bottom")
    add_executable(quake_main EXCLUDE_FROM_ALL
        ${QOG_ROOT}/port/rp2040/sys_rp2040.c
        ${QOG_ROOT}/port/rp2040/link_rp2040.c)
    target_compile_definitions(quake_main PRIVATE
        QOG_SYS_MAIN QOG_LINK_MAIN QOG_HUNK_KB=${QOG_SV_HUNK_KB} QOG_STACK_KB=${QOG_SV_STACK_KB})
    target_compile_options(quake_main PRIVATE -fstack-usage)
    # Format main's FAT volume for saves when it does not mount (user approved).
    option(QOG_FAT_FORMAT_IF_MISSING "Format main's save volume when it does not mount" ON)
    if(QOG_FAT_FORMAT_IF_MISSING)
        target_compile_definitions(quake_main PRIVATE QOG_FAT_FORMAT_IF_MISSING)
    endif()
    if(QOG_LINK_DIAG)
        target_compile_definitions(quake_main PRIVATE QOG_LINK_DIAG QOG_STACK_WATERMARK QOG_FAULT_RECORD)
    endif()
    # the server on the process stack, interrupts on the SDK's main stack (sys_rp2040.c)
    target_compile_definitions(quake_main PRIVATE QOG_GAME_PSP)
    # saves on main's FAT volume through the BSP's FatFs (sys_rp2040.c)
    target_link_libraries(quake_main qog_engine_sv fwog_main_bsp fwog_main_fs)
    target_link_options(quake_main PRIVATE -Wl,--allow-multiple-definition)
    # Earlier work, the BSP's frame sender without its 4 KB frame buffer, as on the display (port/rp2040/link_rp2040.c)
    target_compile_definitions(quake_main PRIVATE QOG_LINK_STREAM_SEND)
    target_link_options(quake_main PRIVATE -Wl,--wrap=fwog_link_uart_send_frame)
    # the display image main carries, quake_display, or for measuring main
    # while quake_display does not link, any display app (qog_display)
    set(QOG_SPLIT_MAIN_EMBED quake_display CACHE STRING "display app quake_main embeds")
    fwog_embed_display_image(quake_main ${QOG_SPLIT_MAIN_EMBED})
    fwog_main_app(quake_main
        VERSION 001
        DESCRIPTION "QuakeOG: the Quake server half (QuakeC, physics, edicts) on the main RP2040, with the client half on the display")
    # QuakeOG, faster clocks for both Quake apps only, set
    # at run time right after board_init() (port/rp2040/clock_rp2040.c). The
    # BSP, its display bootloader and every other app keep 200 MHz and flash
    # CLKDIV 4. QOG_SYS_KHZ=0 keeps the BSP's clock, QOG_FLASH_FAST=OFF the
    # flash divider. Earlier work, 300 MHz at 1.25 V (the user approved it), the
    # flash at clk_sys / 2 up to QOG_FLASH_MAX_KHZ with every RX delay checked.
    # Step back with -DQOG_SYS_KHZ=250000 -DQOG_SYS_MV=1200.
    set(QOG_SYS_KHZ 300000 CACHE STRING "clk_sys of the Quake apps in kHz, 0 keeps the BSP's 200 MHz")
    set(QOG_SYS_MV 1250 CACHE STRING "core voltage in mV for QOG_SYS_KHZ (1150 to 1300), set before the clock")
    option(QOG_FLASH_FAST "Quake apps switch the QSPI flash to clk_sys / 2 at run time, checked, falling back to 4" ON)
    set(QOG_FLASH_MAX_KHZ 150000 CACHE STRING "fastest flash clock in kHz the Quake apps try (the W25Q128JV is rated 133000)")
    set(QOG_TEMP_S 5 CACHE STRING "seconds between the die temperature lines of both Quake apps, 0 for none")
    if(QOG_FLASH_FAST)
        set(_flash_fast 1)
    else()
        set(_flash_fast 0)
    endif()
    foreach(_app quake_display quake_main)
        target_sources(${_app} PRIVATE ${QOG_ROOT}/port/rp2040/clock_rp2040.c)
        target_compile_definitions(${_app} PRIVATE QOG_CLOCK_BOOST
            QOG_SYS_KHZ=${QOG_SYS_KHZ} QOG_SYS_MV=${QOG_SYS_MV} QOG_FLASH_FAST=${_flash_fast}
            QOG_FLASH_MAX_KHZ=${QOG_FLASH_MAX_KHZ} QOG_TEMP_S=${QOG_TEMP_S})
        target_link_libraries(${_app} hardware_adc)
    endforeach()

    # QuakeOG, the board's PC-sampling profiler, off by default
    # (port/rp2040/prof_rp2040.c). The display samples core 0 at QOG_PROF_HZ,
    # with QOG_PROF_CORE1 core 1 too, and with QOG_PROF_MAIN main's core 0.
    # After each link tools/prof_table.py writes the function table into the
    # app's .elf, .bin and .uf2. The tables take QOG_PROF_HUNK_KB of the
    # client hunk (75 less 3 less 3 leaves 69 KB, timedemo demo3's e1m6 needs
    # about 66) and QOG_PROF_SV_HUNK_KB of the server hunk (e1m6 needs 138).
    option(QOG_PROF "board PC-sampling profiler on the display (build for measuring only)" OFF)
    option(QOG_PROF_CORE1 "the profiler samples the display's core 1 too" ON)
    option(QOG_PROF_MAIN "the profiler samples main's core 0 too" OFF)
    set(QOG_PROF_HZ 4000 CACHE STRING "profiler samples a second on each core")
    set(QOG_PROF_HUNK_KB 3 CACHE STRING "KB of client hunk the profiler build gives to its tables")
    set(QOG_PROF_SV_HUNK_KB 4 CACHE STRING "KB of server hunk the profiler build gives to its tables")
    set(QOG_PROF_TABLE_KB 96 CACHE STRING "KB of flash reserved for the profiler's function table")
    if(QOG_PROF)
        set(_prof_apps quake_display)
        if(QOG_PROF_MAIN)
            list(APPEND _prof_apps quake_main)
        endif()
        foreach(_app ${_prof_apps})
            target_sources(${_app} PRIVATE
                ${QOG_ROOT}/port/rp2040/prof_rp2040.c
                ${QOG_ROOT}/port/rp2040/prof_table_rp2040.c)
            target_compile_definitions(${_app} PRIVATE QOG_PROF
                QOG_PROF_HZ=${QOG_PROF_HZ} QOG_PROF_TABLE_KB=${QOG_PROF_TABLE_KB})
            if(_app STREQUAL "quake_display")
                set(_cut ${QOG_PROF_HUNK_KB})
                if(QOG_PROF_CORE1)
                    target_compile_definitions(${_app} PRIVATE QOG_PROF_CORE1)
                endif()
            else()
                set(_cut ${QOG_PROF_SV_HUNK_KB})
            endif()
            get_target_property(_defs ${_app} COMPILE_DEFINITIONS)
            foreach(_d ${_defs})
                if(_d MATCHES "^QOG_HUNK_KB=([0-9]+)$")
                    set(_was ${CMAKE_MATCH_1})
                    math(EXPR _kb "${_was} - ${_cut}")
                    list(TRANSFORM _defs REPLACE "^QOG_HUNK_KB=[0-9]+$" "QOG_HUNK_KB=${_kb}")
                    message(STATUS "qog: profiler build, ${_app} hunk ${_was} KB less ${_cut} KB is ${_kb} KB")
                endif()
            endforeach()
            set_property(TARGET ${_app} PROPERTY COMPILE_DEFINITIONS ${_defs})
            add_custom_command(TARGET ${_app} POST_BUILD
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/prof_table.py
                        --elf $<TARGET_FILE:${_app}>
                        --bin $<TARGET_FILE_DIR:${_app}>/${_app}.bin
                        --uf2 $<TARGET_FILE_DIR:${_app}>/${_app}.uf2
                VERBATIM)
        endforeach()
    endif()

    add_custom_command(TARGET quake_main POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_app_size.py
                $<TARGET_FILE_DIR:quake_main>/quake_main.bin
                ${CMAKE_SOURCE_DIR}/platform/qog_flashmap.h main
        VERBATIM)
endif()
