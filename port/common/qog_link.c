/* QuakeOG: reliable byte stream over the BSP's link frames. See qog_link.h.
 *
 * Pure C. The frame format, its constants and its CRC are the BSP's
 * (bsp/common/link/link_frame.h, common/crc.c, which have no SDK dependency,
 * so the PC build compiles the very same files). The encoder writes a frame
 * in three pieces and the decoder follows link_frame.c's state machine with a
 * buffer sized for our largest frame instead of the BSP's 4,160 bytes: the
 * display has little RAM to spare. A longer frame (only the bootloader's
 * update chunks are longer, and they never reach a running app) is dropped
 * and the decoder resynchronizes, as it does after a bad CRC. */
#include <string.h>
#include "qog_link.h"
#include "common/link/link_frame.h"
#include "common/crc.h"

#if (QOG_LINK_TXRING & (QOG_LINK_TXRING - 1)) || (QOG_LINK_RXRING & (QOG_LINK_RXRING - 1))
#error QOG_LINK_TXRING and QOG_LINK_RXRING must be powers of two
#endif

qog_link_stats_t qog_link_stats;
int qog_link_peer_restarted;
void (*qog_link_side)(const uint8_t *payload, int len);   /* QuakeOG (Agent T) */

static int      s_role;
static int      s_up;
static uint32_t s_session;          /* ours */
static uint32_t s_peer_session;

/* QuakeOG (Agent T): QOG_LINK_SCRATCH puts the two rings in the RP2040's
   SCRATCH_X bank (4 KB), which the main chip's server half does not use
   otherwise: core1 never starts there, so no core1 stack is linked. */
#ifdef QOG_LINK_SCRATCH
#define LINK_RING_PLACE __attribute__((section(".scratch_x.qog_link")))
#else
#define LINK_RING_PLACE
#endif

/* transmit: bytes [tx_acked, tx_head) are unacknowledged, of which the
   first tx_inflight were sent in the frame with sequence tx_seq */
static uint8_t  s_tx[QOG_LINK_TXRING] LINK_RING_PLACE;
static uint32_t s_tx_head, s_tx_acked;
static int      s_tx_inflight;
static uint8_t  s_tx_seq;
static uint32_t s_tx_sent_ms, s_tx_timeout_ms;
static int      s_tx_tries;

/* receive: [rx_tail, rx_head) are delivered and not yet read */
static uint8_t  s_rx[QOG_LINK_RXRING] LINK_RING_PLACE;
static uint32_t s_rx_head, s_rx_tail;
static uint8_t  s_rx_expect;

/* decoder, link_frame.c's states */
#define DEC_MAX (QOG_LINK_CHUNK + 2 < 64 ? 64 : QOG_LINK_CHUNK + 2)
static struct {
    uint8_t  state;
    uint16_t len, got, crc;
    uint8_t  buf[DEC_MAX];
} s_dec;

/* returns the payload length when a good frame completed, -1 otherwise */
static int dec_byte(uint8_t b) {
    switch (s_dec.state) {
    case FWOG_LINK_ST_SOF:
        if (b == FWOG_LINK_SOF) s_dec.state = FWOG_LINK_ST_LEN_LO;
        return -1;
    case FWOG_LINK_ST_LEN_LO:
        s_dec.len = b;
        s_dec.state = FWOG_LINK_ST_LEN_HI;
        return -1;
    case FWOG_LINK_ST_LEN_HI:
        s_dec.len |= (uint16_t)(b << 8);
        if (s_dec.len > DEC_MAX) { s_dec.state = FWOG_LINK_ST_SOF; return -1; }   /* resync */
        s_dec.got = 0;
        s_dec.state = s_dec.len ? FWOG_LINK_ST_PAYLOAD : FWOG_LINK_ST_CRC_LO;
        return -1;
    case FWOG_LINK_ST_PAYLOAD:
        s_dec.buf[s_dec.got++] = b;
        if (s_dec.got == s_dec.len) s_dec.state = FWOG_LINK_ST_CRC_LO;
        return -1;
    case FWOG_LINK_ST_CRC_LO:
        s_dec.crc = b;
        s_dec.state = FWOG_LINK_ST_CRC_HI;
        return -1;
    default: {
        uint8_t hdr[2] = { (uint8_t)s_dec.len, (uint8_t)(s_dec.len >> 8) };
        s_dec.crc |= (uint16_t)(b << 8);
        s_dec.state = FWOG_LINK_ST_SOF;
        if (fwog_crc16_xmodem_span(hdr, 2, s_dec.buf, s_dec.len) != s_dec.crc) {
            qog_link_stats.crc_drops++;
            return -1;
        }
        return s_dec.len;
    }
    }
}

/* QuakeOG: while a frame is being written, hardware flow control can hold us
   until the peer reads, and the peer may be holding on us the same way. So
   the port keeps decoding what arrives (qog_link_rx_drain) while it waits,
   and any reply that decoding asks for is remembered here and sent once the
   current frame is complete, so two frames never interleave on the wire. */
static int s_in_write;
static int s_pend_ack = -1;     /* sequence to acknowledge, -1 for none */
static int s_pend_hellok;

static void send_small(uint8_t type, uint8_t a);
static void send_hello(uint8_t type);

/* [SOF][len16][payload][crc16], fwog_link_encode's bytes */
static void send_payload(const uint8_t *payload, int len) {
    uint8_t h[3] = { FWOG_LINK_SOF, (uint8_t)len, (uint8_t)(len >> 8) };
    uint16_t crc = fwog_crc16_xmodem_span(h + 1, 2, payload, (size_t)len);
    uint8_t c[2] = { (uint8_t)crc, (uint8_t)(crc >> 8) };
    s_in_write++;
    qog_link_hw_write(h, 3);
    qog_link_hw_write(payload, len);
    qog_link_hw_write(c, 2);
    s_in_write--;
    qog_link_stats.wire_tx += (uint32_t)len + FWOG_LINK_OVERHEAD;
    if (!s_in_write) {
        if (s_pend_hellok) {
            s_pend_hellok = 0;
            send_hello(QOG_LINK_T_HELLOK);
        }
        if (s_pend_ack >= 0) {
            uint8_t a = (uint8_t)s_pend_ack;
            s_pend_ack = -1;
            send_small(QOG_LINK_T_ACK, a);
        }
    }
}

static void reply_ack(uint8_t seq) {
    if (s_in_write) s_pend_ack = seq;
    else send_small(QOG_LINK_T_ACK, seq);
}

static void reply_hellok(void) {
    if (s_in_write) s_pend_hellok = 1;
    else send_hello(QOG_LINK_T_HELLOK);
}

/* QuakeOG (Agent T): a side frame, sent at once (see qog_link.h) */
void qog_link_send_side(const void *payload, int len) {
    if (len > 0 && len <= DEC_MAX) send_payload(payload, len);
}

static void send_small(uint8_t type, uint8_t a) {
    uint8_t p[2] = { type, a };
    send_payload(p, 2);
}

static void send_hello(uint8_t type) {
    uint8_t p[6] = { type, (uint8_t)s_role,
                     (uint8_t)s_session, (uint8_t)(s_session >> 8),
                     (uint8_t)(s_session >> 16), (uint8_t)(s_session >> 24) };
    send_payload(p, 6);
}

/* A new session from the peer: it starts its sequence at 0 and so do we.
   Unacknowledged bytes stay queued and go out again from the start. */
static void peer_session(uint32_t session) {
    if (session == s_peer_session && s_up) return;
    if (s_up) qog_link_peer_restarted = 1;
    s_peer_session = session;
    s_rx_expect = 0;
    s_tx_seq = 0;
    s_tx_inflight = 0;
    s_tx_tries = 0;
    s_up = 1;
    qog_link_stats.sessions++;
}

static uint32_t frame_timeout(int bytes) {
    uint32_t rate = qog_link_hw_rate();
    uint32_t wire = rate ? (uint32_t)(((uint64_t)(bytes + 16) * 1000u * 2u) / rate) : 0;
    uint32_t t = 30u + 2u * wire;
    int i;
    for (i = 0; i < s_tx_tries && t < 1000u; i++) t *= 2;   /* back off while the peer is busy */
    return t;
}

static void handle_payload(const uint8_t *p, int len) {
    if (len < 1) return;
    switch (p[0]) {
    case QOG_LINK_T_DATA: {
        int n = len - 2;
        if (len < 2 || !s_up) return;
        if (p[1] == s_rx_expect) {
            if ((uint32_t)n > QOG_LINK_RXRING - (s_rx_head - s_rx_tail)) {
                qog_link_stats.full_drops++;   /* no room: no ack, the sender waits */
                return;
            }
            for (int i = 0; i < n; i++)
                s_rx[(s_rx_head + (uint32_t)i) & (QOG_LINK_RXRING - 1)] = p[2 + i];
            s_rx_head += (uint32_t)n;
            s_rx_expect++;
            qog_link_stats.data_rx += (uint32_t)n;
            qog_link_stats.frames_rx++;
            reply_ack(p[1]);
        } else if ((uint8_t)(p[1] + 1) == s_rx_expect) {
            qog_link_stats.dups++;             /* our ack was lost: say it again */
            reply_ack(p[1]);
        }
        return;
    }
    case QOG_LINK_T_ACK:
        if (len >= 2 && s_tx_inflight && p[1] == s_tx_seq) {
            s_tx_acked += (uint32_t)s_tx_inflight;
            s_tx_inflight = 0;
            s_tx_seq++;
            s_tx_tries = 0;
        }
        return;
    case QOG_LINK_T_HELLO:
        if (len >= 6) {
            peer_session((uint32_t)p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 24);
            reply_hellok();
        }
        return;
    case QOG_LINK_T_HELLOK:
        if (len >= 6 && s_role == QOG_LINK_ROLE_CLIENT)
            peer_session((uint32_t)p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 24);
        return;
    default:
        qog_link_stats.other_rx++;
        if (s_in_write)
            return;     /* their handlers may send at once: dropped, both are resent */
        if (p[0] >= QOG_LINK_T_SAVES_ASK && p[0] <= QOG_LINK_T_SIDE_LAST) {
            if (qog_link_side) qog_link_side(p, len);   /* QuakeOG (Agent T): side frames */
            return;
        }
        qog_link_hw_other(p, len);
        return;
    }
}

static void poll_rx(void) {
    uint8_t buf[256];
    int n;
    while ((n = qog_link_hw_read(buf, (int)sizeof buf)) > 0) {
        qog_link_stats.wire_rx += (uint32_t)n;
        for (int i = 0; i < n; i++) {
            int len = dec_byte(buf[i]);
            if (len >= 0)
                handle_payload(s_dec.buf, len);
        }
    }
}

static void pump_tx(void) {
    uint32_t now;
    if (!s_up) return;
    now = qog_link_hw_ms();
    if (s_tx_inflight && (uint32_t)(now - s_tx_sent_ms) < s_tx_timeout_ms)
        return;
    if (s_tx_inflight) {
        qog_link_stats.resends++;
        s_tx_tries++;
    } else {
        uint32_t pending = s_tx_head - s_tx_acked;
        if (!pending) return;
        s_tx_inflight = pending > QOG_LINK_CHUNK ? QOG_LINK_CHUNK : (int)pending;
        qog_link_stats.data_tx += (uint32_t)s_tx_inflight;
        qog_link_stats.frames_tx++;
    }
    {
        static uint8_t p[QOG_LINK_CHUNK + 2];
        p[0] = QOG_LINK_T_DATA;
        p[1] = s_tx_seq;
        for (int i = 0; i < s_tx_inflight; i++)
            p[2 + i] = s_tx[(s_tx_acked + (uint32_t)i) & (QOG_LINK_TXRING - 1)];
        send_payload(p, s_tx_inflight + 2);
    }
    s_tx_sent_ms = qog_link_hw_ms();
    s_tx_timeout_ms = frame_timeout(s_tx_inflight);
}

void qog_link_pump(void) {
    poll_rx();
    pump_tx();
}

void qog_link_rx_drain(void) {
    s_in_write++;
    poll_rx();
    s_in_write--;
}

void qog_link_start(int role) {
    uint32_t last = 0;
    s_role = role;
    s_dec.state = FWOG_LINK_ST_SOF;
    qog_link_hw_open(role);
    s_session = qog_link_hw_ms() * 2654435761u ^ (uint32_t)role ^ 0x51u;
    if (!s_session) s_session = 1;
    if (role == QOG_LINK_ROLE_CLIENT) {
        while (!s_up) {
            uint32_t now = qog_link_hw_ms();
            if (!last || now - last > 100u) { send_hello(QOG_LINK_T_HELLO); last = now ? now : 1; }
            poll_rx();
            qog_link_hw_idle();
        }
    } else {
        while (!s_up) {
            poll_rx();
            qog_link_hw_idle();
        }
    }
    qog_link_peer_restarted = 0;
}

int qog_link_up(void) { return s_up; }

void qog_link_write(const void *data, int len) {
    const uint8_t *p = data;
    while (len > 0) {
        uint32_t room = QOG_LINK_TXRING - (s_tx_head - s_tx_acked);
        if (!room) {
            qog_link_pump();
            qog_link_hw_idle();
            continue;
        }
        while (room && len) {
            s_tx[s_tx_head & (QOG_LINK_TXRING - 1)] = *p++;
            s_tx_head++;
            room--;
            len--;
        }
    }
}

int qog_link_avail(void) {
    return (int)(s_rx_head - s_rx_tail);
}

void qog_link_read(void *dst, int len) {
    uint8_t *p = dst;
    while (len > 0) {
        if (s_rx_head == s_rx_tail) {
            qog_link_pump();
            if (s_rx_head == s_rx_tail) qog_link_hw_idle();
            continue;
        }
        while (len && s_rx_head != s_rx_tail) {
            *p++ = s_rx[s_rx_tail & (QOG_LINK_RXRING - 1)];
            s_rx_tail++;
            len--;
        }
    }
}

void qog_link_drain(void) {
    while (s_tx_head != s_tx_acked) {
        qog_link_pump();
        qog_link_hw_idle();
    }
}

#ifdef QOG_LINK_PIPELINE
void qog_link_push(void) {
    for (;;) {
        qog_link_pump();
        if (s_tx_head - s_tx_acked <= (uint32_t)s_tx_inflight) return;   /* all sent at least once */
        qog_link_hw_idle();
    }
}
#endif
