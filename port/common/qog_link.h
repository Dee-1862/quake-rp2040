/* QuakeOG: a reliable, ordered byte stream between the two RP2040s.
 *
 * The client (display CPU) and the server (main CPU) talk over the BSP's
 * inter-CPU link: UART0 with RTS/CTS, frames [0x7E][len16][payload][crc16]
 * built by the BSP's own codec (bsp/common/link/link_frame.c). Byte 0 of a
 * payload is the message type. 0x01-0x1F belong to the display bootloader
 * and 0x20-0x22 to the breakout I/O messages, so Quake uses 0x40-0x43:
 *
 *   0x40 DATA   [type][seq][bytes...]   up to QOG_LINK_CHUNK stream bytes
 *   0x41 ACK    [type][seq]             DATA seq arrived in order
 *   0x42 HELLO  [type][role][session4]  start (or restart) of a session
 *   0x43 HELLOK [type][role][session4]  answer to HELLO
 *
 * One DATA frame is in flight at a time (stop and wait). A frame that is
 * lost or fails its CRC is sent again after a timeout, a duplicate is
 * acknowledged again and dropped, and the frame decoder resynchronizes on
 * the next 0x7E with a good CRC. A receiver whose stream buffer is full
 * does not acknowledge, so the sender waits: that and the UART's RTS/CTS
 * are the flow control. Any other payload type is handed to
 * qog_link_hw_other(), which on the display services the breakout I/O
 * messages (fwog_ioexp_link_handle).
 *
 * The same code runs on the PC over a TCP socket (port/pc/link_pc.c), rate
 * limited to the UART's speed, so the protocol is tested there.
 */
#ifndef QOG_LINK_H
#define QOG_LINK_H

#include <stdint.h>

#define QOG_LINK_T_DATA     0x40u
#define QOG_LINK_T_ACK      0x41u
#define QOG_LINK_T_HELLO    0x42u
#define QOG_LINK_T_HELLOK   0x43u

#define QOG_LINK_ROLE_SERVER 1   /* main CPU */
#define QOG_LINK_ROLE_CLIENT 2   /* display CPU */

#ifndef QOG_LINK_CHUNK
#define QOG_LINK_CHUNK      1024  /* stream bytes per DATA frame */
#endif
#ifndef QOG_LINK_TXRING
#define QOG_LINK_TXRING     4096  /* unacknowledged bytes this side may hold (power of two) */
#endif
#ifndef QOG_LINK_RXRING
#define QOG_LINK_RXRING     4096  /* received bytes not yet read (power of two) */
#endif

typedef struct {
    uint32_t wire_tx, wire_rx;      /* bytes on the wire, frames and acks included */
    uint32_t data_tx, data_rx;      /* stream bytes delivered */
    uint32_t frames_tx, frames_rx;
    uint32_t resends, dups, crc_drops, full_drops, sessions;
    uint32_t other_rx;              /* frames of other protocols */
} qog_link_stats_t;

extern qog_link_stats_t qog_link_stats;

/* Start the session: the client sends HELLO until the server answers. Blocks
   until the peer is there. Bytes written before this are kept and sent
   after it. */
void qog_link_start(int role);
int  qog_link_up(void);

/* Queue bytes on the stream. Blocks (pumping the link) while the ring is full. */
void qog_link_write(const void *data, int len);
/* Read exactly len bytes, blocking and pumping until they are there. */
void qog_link_read(void *dst, int len);
/* Bytes that can be read without blocking. */
int  qog_link_avail(void);
/* Send what is queued and receive what arrived. Never blocks for long. */
void qog_link_pump(void);
/* Pump until every written byte is acknowledged. */
void qog_link_drain(void);
/* Set when the peer started a new session (it restarted). The caller clears it. */
extern int qog_link_peer_restarted;

/* ---- supplied by the port (port/pc/link_pc.c, port/rp2040/link_rp2040.c) ---- */
/* Bring the transport up. */
void     qog_link_hw_open(int role);
/* Write all bytes (blocks while the peer's flow control holds us). */
void     qog_link_hw_write(const void *data, int len);
/* Read what is there, up to max bytes. Returns 0 when nothing is waiting. */
int      qog_link_hw_read(void *dst, int max);
/* A millisecond clock. */
uint32_t qog_link_hw_ms(void);
/* Called in every wait loop: the main CPU kicks its watchdog here. */
void     qog_link_hw_idle(void);
/* A complete frame of another protocol (type outside 0x40-0x43). */
void     qog_link_hw_other(const void *payload, int len);
/* Bytes per second the transport carries, for the resend timeout. */
uint32_t qog_link_hw_rate(void);

#endif
