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

# Engine: id's files, minus the real sound mixer (snd_null.c stands in for now).
file(GLOB ENGINE_SRC ${ROOT}/engine/*.c)
list(REMOVE_ITEM ENGINE_SRC
    ${ROOT}/engine/snd_dma.c ${ROOT}/engine/snd_mem.c ${ROOT}/engine/snd_mix.c)

# QuakeC compiled to C from the player's own progs.dat (never committed).
# Without the pak, qc2c writes an empty table and the interpreter runs.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(QOG_PAK ${ROOT}/data/id1/pak0.pak)
set(PROGS_AOT ${CMAKE_BINARY_DIR}/progs_aot.c)
set(_pak_dep "")
if(EXISTS ${QOG_PAK})
    set(_pak_dep ${QOG_PAK})
endif()
add_custom_command(OUTPUT ${PROGS_AOT}
    COMMAND ${Python3_EXECUTABLE} ${ROOT}/tools/qc2c.py --pak ${QOG_PAK} -o ${PROGS_AOT}
    DEPENDS ${ROOT}/tools/qc2c.py ${_pak_dep}
    COMMENT "Compiling QuakeC to C")

add_executable(quakeog_pc
    ${ENGINE_SRC}
    ${PROGS_AOT}
    ${ROOT}/port/pc/sys_pc.c
    ${ROOT}/port/pc/vid_pc.c
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
    QOG_SHOT_DIR="${CMAKE_BINARY_DIR}/shots")
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
