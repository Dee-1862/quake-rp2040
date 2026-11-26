/* QuakeOG, the split game's link on the board (port/common/qog_link.h).
 *
 * The inter-CPU UART (UART0 with RTS/CTS, the BSP's link_uart.c) at
 * FWOG_LINK_BAUD, framed by the BSP's link codec. Built into both apps.
 *
 *   QOG_LINK_MAIN    the main CPU (server half, apps/quake/main), the link is
 *                    already up after fwog_display_update_run(), the wait
 *                    loops kick the 8.3 s watchdog, and frames of other
 *                    protocols are ignored (main starts no breakout I/O).
 *   otherwise        the display CPU (client half), the wait loops keep the
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
#include "qog_power.h"
#include "io_expander/ioexp_link.h"
extern uint8_t qog_rp2040_buttons;   /* sys_rp2040.c */
extern uint8_t qog_rp2040_buttons_seen;
#ifdef QOG_SND_REMOTE
#include "qog_snd.h"
#endif
#endif

void qog_link_hw_open(int role) {
    (void)role;
#ifndef QOG_LINK_MAIN
    /* main brought the link up for the display update. The display's app
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
/* QuakeOG, bring-up counters, printed once a second while the link waits */
static uint32_t s_diag_tx, s_diag_rx;
#endif
#if defined(QOG_LINK_DIAG) && defined(QOG_LINK_MAIN)
/* QuakeOG, microseconds main's CPU waited for room to write to the
   link (the display not reading), printed as txstall in the diag line */
static uint32_t s_diag_stall_us;
#define STALL_T(v)      uint32_t v = time_us_32()
#define STALL_ADD(v)    (s_diag_stall_us += time_us_32() - (v))
#else
#define STALL_T(v)
#define STALL_ADD(v)
#endif

#ifdef QOG_LINK_TXASYNC
/* QuakeOG, a DMA channel feeds the link UART's transmit FIFO from
   qog_link.c's frame buffer, paced by the UART's DREQ, which the hardware
   flow control holds while the peer does not read. So main never stalls in
   a write while the display draws, it reads the client's tick and runs its
   frame meanwhile (docs/changes.md). The SDK's uart_init turned the UART's
   DMA requests on. */
#include "hardware/dma.h"
static int s_txdma = -1;

int qog_link_hw_tx_busy(void) {
    return s_txdma >= 0 && dma_channel_is_busy((uint)s_txdma);
}

void qog_link_hw_tx_frame(const uint8_t *frame, int len) {
    dma_channel_config c;
    if (s_txdma < 0) s_txdma = (int)dma_claim_unused_channel(true);
    while (qog_link_hw_tx_busy()) tight_loop_contents();   /* qog_link.c starts one only when idle */
#ifdef QOG_LINK_DIAG
    s_diag_tx += (uint32_t)len;
#endif
    c = dma_channel_get_default_config((uint)s_txdma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, uart_get_dreq(FWOG_LINK_UART, true));
    dma_channel_configure((uint)s_txdma, &c, &uart_get_hw(FWOG_LINK_UART)->dr, frame, (uint)len, true);
}

/* a write outside qog_link.c's frames (the BSP's frame sender) waits for the
   frame on the wire, so two frames never interleave */
static void tx_dma_idle(void) {
    if (!qog_link_hw_tx_busy()) return;
    STALL_T(t0);
    while (qog_link_hw_tx_busy()) {
        qog_link_rx_drain();
        qog_link_hw_idle();
    }
    STALL_ADD(t0);
}
#endif

void qog_link_hw_write(const void *data, int len) {
    /* QuakeOG, byte by byte, and while the FIFO is full (the peer's RTS holds
       us) keep reading what the peer sends. With both ends blocked in
       uart_write_blocking and both receive FIFOs full, neither would ever
       read again, the first board run hung exactly so. */
    const uint8_t *p = data;
#ifdef QOG_LINK_DIAG
    s_diag_tx += (uint32_t)len;
#endif
#ifdef QOG_LINK_TXASYNC
    tx_dma_idle();
#endif
    for (int i = 0; i < len; i++) {
        if (!uart_is_writable(FWOG_LINK_UART)) {
            STALL_T(t0);
            while (!uart_is_writable(FWOG_LINK_UART)) {
                qog_link_rx_drain();
                qog_link_hw_idle();
            }
            STALL_ADD(t0);
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
#if defined(QOG_LINK_MAIN) && defined(QOG_FAULT_RECORD)
            {
                extern const uint32_t *qog_fault_last(void);
                const uint32_t *f = qog_fault_last();
                if (f)
                    DIAG("[quake_main] LAST %s pc=%08lx lr=%08lx xpsr=%08lx sp=%08lx\n",
                         f[0] == 0x51484E47u ? "HANG" : "FAULT",
                         (unsigned long)f[1], (unsigned long)f[2], (unsigned long)f[3], (unsigned long)f[4]);
                if (f) {
                    DIAG("[quake_main] stack");
                    for (int i = 5; i < 21; i++) DIAG(" %08lx", (unsigned long)f[i]);
                    DIAG(" parm0=%08lx v=%08lx\n", (unsigned long)f[21], (unsigned long)f[22]);
                }
            }
#endif
#if defined(QOG_LINK_MAIN) && defined(QOG_STACK_WATERMARK)
            extern uint32_t qog_stack_free(void);
            /* Earlier work, txstall, ms in the last second main waited to write */
            DIAG("[quake_main] link wait t=%lu ms tx=%lu rx=%lu stack free %lu txstall %lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx,
                 (unsigned long)qog_stack_free(), (unsigned long)(s_diag_stall_us / 1000u));
            s_diag_stall_us = 0;
#elif defined(QOG_LINK_MAIN)
            DIAG("[quake_main] link wait t=%lu ms tx=%lu rx=%lu txstall %lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx,
                 (unsigned long)(s_diag_stall_us / 1000u));
            s_diag_stall_us = 0;
#elif defined(QOG_STACK_WATERMARK) && defined(QOG_ZEDGE_STACK_GUARD)
            /* Earlier work, and the edge fallbacks so far (engine/r_edge.c), those
               that moved the edges to the stack and those drawn without z */
            extern uint32_t qog_stack_free(void);
            extern int r_qog_zstack, r_qog_znoz;
            DIAG("[quake_display] link wait t=%lu ms tx=%lu rx=%lu stack free %lu zedge %d noz %d\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx,
                 (unsigned long)qog_stack_free(), r_qog_zstack - r_qog_znoz, r_qog_znoz);
#elif defined(QOG_STACK_WATERMARK)
            extern uint32_t qog_stack_free(void);
            DIAG("[quake_display] link wait t=%lu ms tx=%lu rx=%lu stack free %lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx,
                 (unsigned long)qog_stack_free());
#else
            DIAG("[quake_display] link wait t=%lu ms tx=%lu rx=%lu\n",
                 (unsigned long)t, (unsigned long)s_diag_tx, (unsigned long)s_diag_rx);
#endif
#if !defined(QOG_LINK_MAIN) && defined(QOG_HUD_SKIP)
            {   /* Earlier work, frames that sent the status bar rows to the LCD */
                extern unsigned vid_qog_frames, vid_qog_hudsent;
                /* Earlier work, and the last scanout's time, to confirm the LCD rate */
                extern void VID_QogScanStats(unsigned *us, unsigned *rows);
                unsigned scan_us, scan_rows;
                VID_QogScanStats(&scan_us, &scan_rows);
                DIAG("[quake_display] status bar rows sent in %u of %u frames, last scan %u us for %u rows\n",
                     vid_qog_hudsent, vid_qog_frames, scan_us, scan_rows);
                vid_qog_frames = vid_qog_hudsent = 0;
            }
#endif
        }
    }
#endif
#ifdef QOG_LINK_MAIN
    board_watchdog_kick();
#ifdef QOG_FAULT_RECORD
    {
        extern volatile uint32_t qog_progress;   /* sys_rp2040.c hang detector */
        qog_progress++;
    }
#endif
#else
    static uint32_t last;
    uint32_t now = qog_link_hw_ms();
    if (now - last >= 5u) {      /* the hold machine counts milliseconds */
        fwog_power_t pw = QOG_POWER_POLL(now);
        qog_rp2040_buttons = pw.buttons.down;   /* level, as the main loop reads it */
        qog_rp2040_buttons_seen |= pw.buttons.down;
        last = now;
#ifdef QOG_SND_REMOTE
        qog_snd_out_idle();      /* Earlier work, silence what played while the server half is busy */
#endif
    }
#endif
}

#ifdef QOG_LINK_STREAM_SEND
/* QuakeOG, the BSP's fwog_link_uart_send_frame encodes into a
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
#ifdef QOG_LINK_TXASYNC
    tx_dma_idle();      /* Earlier work, not into a frame the DMA is sending */
#endif
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
