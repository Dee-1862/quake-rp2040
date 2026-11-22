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

add_executable(quakeog_pc
    ${ENGINE_SRC}
    ${ROOT}/port/pc/sys_pc.c
    ${ROOT}/port/pc/vid_pc.c
    ${ROOT}/port/common/qog_mem.c
    ${ROOT}/port/common/qog_aim.c
    ${ROOT}/platform/qog_input.c)
target_include_directories(quakeog_pc PRIVATE
    ${ROOT}/engine ${ROOT}/port/common ${ROOT}/platform)
target_compile_definitions(quakeog_pc PRIVATE
    HOST_TEST                       # platform/ pure code only, no Pico SDK
    _CRT_SECURE_NO_WARNINGS
    QOG_DEFAULT_BASEDIR="${ROOT}/data")
# id's 1996 code is noisy at higher warning levels; keep ours strict later.
target_compile_options(quakeog_pc PRIVATE /W1 /MP)
target_link_libraries(quakeog_pc PRIVATE user32 gdi32 winmm)
set_target_properties(quakeog_pc PROPERTIES WIN32_EXECUTABLE OFF)
