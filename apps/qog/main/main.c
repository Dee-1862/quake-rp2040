/* QuakeOG main-CPU companion. Deliberately does no game work.
 *
 * Everything this chip does is here and in board_init(): clocks, the 8.3 s
 * watchdog, parking radio/FPGA pins, loading the stock FPGA bitstream (done
 * by the BSP's board_init), pushing the display image over the link,
 * releasing the display from reset, then kicking the watchdog forever.
 * That is what lets the project claim "Quake runs on one RP2040". */
#include "fwog_main.h"
#include "pico/stdlib.h"

FWOG_WATCHDOG_DEFAULT();

int main(void) {
    board_init();
    fwog_display_result_t d = fwog_display_update_run();

    /* Repeat for 10 s: anything printed before USB CDC enumerates is lost. */
    const absolute_time_t announce_until = make_timeout_time_ms(10000);
    while (true) {
        board_watchdog_kick();
        if (!time_reached(announce_until))
            DIAG("[qog_main] display: %s\n", fwog_display_result_text(d));
        sleep_ms(500);
    }
}
