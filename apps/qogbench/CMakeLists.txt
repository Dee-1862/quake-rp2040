# QuakeOG benchmark pair. The display half times Quake's world-drawing inner
# loops on the real chip and prints the results over its USB console. The
# main half is the same minimal companion as apps/qog.
add_executable(qogbench_display display/main.c)
target_link_libraries(qogbench_display qog_platform fwog_display_bsp hardware_clocks)
fwog_display_app(qogbench_display
    VERSION 001
    DESCRIPTION "QuakeOG benchmark: world span drawing, z spans, float divide and flash reads on the display CPU")
qog_check_app_size(qogbench_display)

add_executable(qogbench_main main/main.c)
target_link_libraries(qogbench_main fwog_main_bsp)
fwog_embed_display_image(qogbench_main)
fwog_main_app(qogbench_main
    VERSION 001
    DESCRIPTION "QuakeOG benchmark companion: boots the display CPU and kicks the watchdog. Does no game work.")
