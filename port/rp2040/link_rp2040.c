/* QuakeOG: the split game's link on the board (port/common/qog_link.h).
 *
 * The inter-CPU UART (UART0 with RTS/CTS, the BSP's link_uart.c) at
 * FWOG_LINK_BAUD, framed by the BSP's link codec. Built into both apps:
 *
 *   QOG_LINK_MAIN    the main CPU (server half, apps/quake/main): the link is
 *                    already up after fwog_display_update_run(), the wait
 *                    loops kick the 8.3 s watchdog, and frames of other
 *                    protocols are ignored (main starts no breakout I/O).
 *   otherwise        the display CPU (client half): the wait loops keep the
 *                    6 s red-hold power-off alive (fwog_power_poll, whose
 *                    buttons the main loop reads) and hand the breakout I/O
 *                    messages 0x20-0x22 to fwog_ioexp_link_handle, which the
 *                    display app must keep servicing.
 *
 * Not run on hardware yet. */
#include <stdint.h>
#include "qog_link.h"
#include "pico/stdlib.h"
#include "common/link/link_uart.h"
#include "common/diag.h"
#ifdef QOG_LINK_MAIN
#include "fwog_main.h"
#else
#include "fwog_display.h"
#include "io_expander/ioexp_link.h"
extern uint8_t qog_rp2040_buttons;   /* sys_rp2040.c */
#endif

void qog_link_hw_open(int role) {
    (void)role;
#ifndef QOG_LINK_MAIN
    /* main brought the link up for the display update; the display's app
       starts with it down */
    if (!fwog_link_uart_init(FWOG_LINK_BAUD)) {
        for (;;) {
            DIAG("[quake] link: %u baud unreachable\n", (unsigned)FWOG_LINK_BAUD);
            qog_link_hw_idle();
            sleep_ms(500);
        }
    }
#endif
}

#ifdef QOG_LINK_DIAG
/* QuakeOG: bring-up counters, printed once a second while the link waits */
static uint32_t s_diag_tx, s_diag_rx;
#endif

void qog_link_hw_write(const void *data, int len) {
    /* QuakeOG: byte by byte, and while the FIFO is full (the peer's RTS holds
       us) keep reading what the peer sends. With both ends blocked in
       uart_write_blocking and both receive FIFOs full, neither would ever
       read again: the first board run hung exactly so. */
    const uint8_t *p = data;
#ifdef QOG_LINK_DIAG
    s_diag_tx += (uint32_t)len;
#endif
    for (int i = 0; i < len; i++) {
        while (!uart_is_writable(FWOG_LINK_UART)) {
            qog_link_rx_drain();
            qog_link_hw_idle();
        }
        uart_putc_raw(FWOG_LINK_UART, p[i]);
    }
}

int qog_link_hw_read(void *dst, int max) {
    uint8_t *p = dst;
    int n = 0;
    while (n < max && fwog_link_uart_read(p + n)) n++;
#ifdef QOG_LINK_DIAG
    s_diag_rx += (uint32_t)n;
#endif
    return n;
}

uint32_t qog_link_hw_ms(void) { return to_ms_since_boot(get_absolute_time()); }

uint32_t qog_link_hw_rate(void) { return FWOG_LINK_BAUD / 10u; }

void qog_link_hw_idle(void) {
#ifdef QOG_LINK_DIAG
    {
        static uint32_t last_diag;
        uint32_t t = qog_link_hw_ms();
        if (t - last_diag >= 1000u) {
            last_diag = t;
#ifdef QOG_LINK_MAIN
            DIAG("[quake_main] link wait t=%lu ms tx=%lu rx=%lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx);
#elif defined(QOG_STACK_WATERMARK)
            extern uint32_t qog_stack_free(void);
            DIAG("[quake_display] link wait t=%lu ms tx=%lu rx=%lu stack free %lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx,
                 (unsigned long)qog_stack_free());
#else
            DIAG("[quake_display] link wait t=%lu ms tx=%lu rx=%lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx);
#endif
        }
    }
#endif
#ifdef QOG_LINK_MAIN
    board_watchdog_kick();
#else
    static uint32_t last;
    uint32_t now = qog_link_hw_ms();
    if (now - last >= 5u) {      /* the hold machine counts milliseconds */
        fwog_power_t pw = fwog_power_poll(now);
        qog_rp2040_buttons = pw.buttons.down;   /* level, as the main loop reads it */
        last = now;
    }
#endif
}

#ifdef QOG_LINK_STREAM_SEND
/* QuakeOG (Agent S): the BSP's fwog_link_uart_send_frame encodes into a
   4,165-byte static frame buffer. Linked with
   -Wl,--wrap=fwog_link_uart_send_frame, its callers (the breakout I/O acks
   of ioexp_link.c) get this one instead, which writes the same bytes
   (fwog_link_encode's SOF, length, payload and CRC) in three pieces, as
   qog_link.c's send_payload does, and needs no buffer. */
#include "common/link/link_frame.h"
#include "common/crc.h"
bool __wrap_fwog_link_uart_send_frame(const void *payload, size_t len) {
    uint8_t h[3], c[2];
    uint16_t crc;
    if (len > FWOG_LINK_MAX_PAYLOAD) return false;
    h[0] = FWOG_LINK_SOF;
    h[1] = (uint8_t)(len & 0xFFu);
    h[2] = (uint8_t)(len >> 8);
    crc = fwog_crc16_xmodem_span(h + 1, 2, payload, len);
    c[0] = (uint8_t)(crc & 0xFFu);
    c[1] = (uint8_t)(crc >> 8);
    fwog_link_uart_write(h, 3);
    fwog_link_uart_write(payload, len);
    fwog_link_uart_write(c, 2);
    return true;
}
#endif

void qog_link_hw_other(const void *payload, int len) {
#ifdef QOG_LINK_MAIN
    (void)payload; (void)len;
#else
    (void)fwog_ioexp_link_handle(payload, (size_t)len);
#endif
}
