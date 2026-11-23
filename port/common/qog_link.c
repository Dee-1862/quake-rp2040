/* QuakeOG: reliable byte stream over the BSP's link frames. See qog_link.h.
 *
 * Pure C: the frame codec is the BSP's (bsp/common/link/link_frame.c,
 * common/crc.c), which has no SDK dependency, so the PC build compiles the
 * very same files. */
#include <string.h>
#include "qog_link.h"
#include "common/link/link_frame.h"

#if (QOG_LINK_TXRING & (QOG_LINK_TXRING - 1)) || (QOG_LINK_RXRING & (QOG_LINK_RXRING - 1))
#error QOG_LINK_TXRING and QOG_LINK_RXRING must be powers of two
#endif

qog_link_stats_t qog_link_stats;
int qog_link_peer_restarted;

static int      s_role;
static int      s_up;
static uint32_t s_session;          /* ours */
static uint32_t s_peer_session;

/* transmit: bytes [tx_acked, tx_head) are unacknowledged, of which the
   first tx_inflight were sent in the frame with sequence tx_seq */
static uint8_t  s_tx[QOG_LINK_TXRING];
static uint32_t s_tx_head, s_tx_acked;
static int      s_tx_inflight;
static uint8_t  s_tx_seq;
static uint32_t s_tx_sent_ms, s_tx_timeout_ms;
static int      s_tx_tries;

/* receive: [rx_tail, rx_head) are delivered and not yet read */
static uint8_t  s_rx[QOG_LINK_RXRING];
static uint32_t s_rx_head, s_rx_tail;
static uint8_t  s_rx_expect;

static fwog_link_rx_t s_dec;
static uint8_t  s_frame[QOG_LINK_CHUNK + 2 + FWOG_LINK_OVERHEAD];

static void send_payload(const uint8_t *payload, int len) {
    size_t n = fwog_link_encode(s_frame, sizeof s_frame, payload, (size_t)len);
    if (!n) return;
    qog_link_hw_write(s_frame, (int)n);
    qog_link_stats.wire_tx += (uint32_t)n;
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
            send_small(QOG_LINK_T_ACK, p[1]);
        } else if ((uint8_t)(p[1] + 1) == s_rx_expect) {
            qog_link_stats.dups++;             /* our ack was lost: say it again */
            send_small(QOG_LINK_T_ACK, p[1]);
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
            send_hello(QOG_LINK_T_HELLOK);
        }
        return;
    case QOG_LINK_T_HELLOK:
        if (len >= 6 && s_role == QOG_LINK_ROLE_CLIENT)
            peer_session((uint32_t)p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[5] << 24);
        return;
    default:
        qog_link_stats.other_rx++;
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
            size_t len;
            uint8_t st = s_dec.state;
            if (fwog_link_rx_byte(&s_dec, buf[i], &len))
                handle_payload(s_dec.buf, (int)len);
            else if (st == FWOG_LINK_ST_CRC_HI)
                qog_link_stats.crc_drops++;    /* a frame ended with a bad CRC */
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

void qog_link_start(int role) {
    uint32_t last = 0;
    s_role = role;
    fwog_link_rx_init(&s_dec);
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
