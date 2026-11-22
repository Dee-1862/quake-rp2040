# QuakeOG app pair. The display half is the whole game; the main half is the
# minimal BSP companion (boot the display, kick the watchdog) and nothing else.
add_executable(qog_display display/main.c)
target_link_libraries(qog_display qog_platform fwog_display_bsp)
fwog_display_app(qog_display
    VERSION 001
    DESCRIPTION "QuakeOG platform test: 8-bit video pipeline, input and flash data region (Phase 2)")
qog_check_app_size(qog_display)
# Test frame size: cmake --preset target -DQOG_TEST_W=240 -DQOG_TEST_H=150
set(QOG_TEST_W 320 CACHE STRING "platform test frame width")
set(QOG_TEST_H 200 CACHE STRING "platform test frame height")
option(QOG_TEST_TILT "platform test: tilt moves the box" OFF)
target_compile_definitions(qog_display PRIVATE QOG_TEST_W=${QOG_TEST_W} QOG_TEST_H=${QOG_TEST_H}
    QOG_TEST_TILT=$<BOOL:${QOG_TEST_TILT}>)

add_executable(qog_main main/main.c)
target_link_libraries(qog_main fwog_main_bsp)
fwog_embed_display_image(qog_main)
fwog_main_app(qog_main
    VERSION 001
    DESCRIPTION "QuakeOG companion: boots the display CPU and kicks the watchdog. Does no game work.")
