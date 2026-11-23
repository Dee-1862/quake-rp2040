# QuakeOG: a Quake port to ONE RP2040 (the FreeWili OG's display CPU).
#
# This project consumes the FreeWili OG BSP (third_party/wiliOGbsp) the way
# its AGENTS.md "Consuming this BSP from another project" section describes:
# we supply PICO_BOARD, the board header dir, PICO_STDIO_UART 0 and Python3,
# then add_subdirectory(bsp). The BSP's own apps are not built here.
cmake_minimum_required(VERSION 3.13)

set(QOG_BSP_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party/wiliOGbsp)
if(NOT EXISTS ${QOG_BSP_DIR}/bsp/CMakeLists.txt)
    message(FATAL_ERROR
        "BSP not found at ${QOG_BSP_DIR}. Clone it first:\n"
        "  git clone https://github.com/freewili/wiliOGbsp third_party/wiliOGbsp")
endif()

set(PICO_BOARD freewili_og CACHE STRING "Board type" FORCE)
list(APPEND PICO_BOARD_HEADER_DIRS "${QOG_BSP_DIR}/bsp/boards")
# UART0 is the inter-CPU link on both chips and must never carry stdio.
set(PICO_STDIO_UART 0 CACHE BOOL "stdio over UART (forced off: UART0 is the link)" FORCE)

include(${QOG_BSP_DIR}/pico_sdk_import.cmake)
project(quakeog C CXX ASM)
set(CMAKE_C_STANDARD 11)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
pico_sdk_init()

add_subdirectory(${QOG_BSP_DIR}/bsp ${CMAKE_BINARY_DIR}/fwog_bsp)

# The BSP's top-level CMakeLists normally provides these two; the UF2 record
# of the main image carries them.
execute_process(COMMAND git describe --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE FWOG_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR FWOG_GIT_DESCRIBE STREQUAL "")
    set(FWOG_GIT_DESCRIBE "dev")
endif()
execute_process(COMMAND git log -1 --format=%ct
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE FWOG_GIT_COMMIT_TS OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR NOT FWOG_GIT_COMMIT_TS MATCHES "^[0-9]+$")
    set(FWOG_GIT_COMMIT_TS 0)
endif()

add_subdirectory(platform)
add_subdirectory(apps/qog)
add_subdirectory(apps/quake)   # engine on the board: measurement targets, not in the default build
