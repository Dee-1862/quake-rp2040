/* QuakeOG: the inter-chip link on the PC, for the split game (port/common/qog_link.h).
 *
 * A TCP connection on 127.0.0.1 stands for the UART between the two RP2040s.
 * The server process listens, the client connects. Writes are held back so
 * the bytes arrive no sooner than the UART would deliver them:
 *
 *   -linkport N      TCP port (default 27511)
 *   -linkrate B      bytes per second (default 300000, 3 Mbaud 8N1)
 *   -linkcorrupt N   flip one byte in about 1 of every N writes (tests resync)
 *   -linkdrop N      lose about 1 of every N writes entirely (tests resends)
 *
 * The error injection uses its own generator, never the game's rand(). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
/* engine functions, declared here: quakedef.h and winsock2.h clash (htonl) */
int COM_CheckParm(char *parm);
extern int com_argc;
extern char **com_argv;
int Q_atoi(char *str);
void Sys_Error(char *error, ...);
void Sys_Quit(void);
#include "qog_link.h"

static SOCKET s_sock = INVALID_SOCKET;
static double s_rate = 300000.0;
static double s_free_at;            /* when the simulated wire is idle again */
static int    s_corrupt, s_drop;
static unsigned s_errseed = 12345;

static double now_s(void) {
    static LARGE_INTEGER freq, start;
    LARGE_INTEGER t;
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); }
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

static unsigned err_rand(void) {
    s_errseed = s_errseed * 1103515245u + 12345u;
    return (s_errseed >> 8) & 0xFFFFFF;
}

static int parm_int(const char *name, int def) {
    int i = COM_CheckParm((char *)name);
    return (i && i + 1 < com_argc) ? Q_atoi(com_argv[i + 1]) : def;
}

void qog_link_hw_open(int role) {
    WSADATA wsa;
    struct sockaddr_in a;
    u_long nb = 1;
    int one = 1;
    int port = parm_int("-linkport", 27511);
    s_rate = parm_int("-linkrate", 300000);
    s_corrupt = parm_int("-linkcorrupt", 0);
    s_drop = parm_int("-linkdrop", 0);
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) Sys_Error("link: WSAStartup failed");
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (role == QOG_LINK_ROLE_SERVER) {
        SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        setsockopt(l, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
        if (bind(l, (struct sockaddr *)&a, sizeof a) || listen(l, 1))
            Sys_Error("link: cannot listen on 127.0.0.1:%d", port);
        printf("link: server waiting for the client on 127.0.0.1:%d\n", port);
        s_sock = accept(l, NULL, NULL);
        closesocket(l);
        if (s_sock == INVALID_SOCKET) Sys_Error("link: accept failed");
    } else {
        double give_up = now_s() + 30.0;
        for (;;) {
            s_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (!connect(s_sock, (struct sockaddr *)&a, sizeof a)) break;
            closesocket(s_sock);
            if (now_s() > give_up) Sys_Error("link: no server on 127.0.0.1:%d", port);
            Sleep(20);
        }
    }
    setsockopt(s_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    ioctlsocket(s_sock, FIONBIO, &nb);
    printf("link: connected (%s), %.0f bytes/s%s%s\n", role == QOG_LINK_ROLE_SERVER ? "server" : "client",
           s_rate, s_corrupt ? ", corrupting writes" : "", s_drop ? ", dropping writes" : "");
}

static void send_raw(const void *data, int len);

void qog_link_hw_write(const void *data, int len) {
    double start = now_s(), done;
    if (s_free_at > start) start = s_free_at;
    done = start + len / s_rate;
    s_free_at = done;
    while (now_s() < done)   /* the UART is still shifting the bytes out */
        SwitchToThread();
    send_raw(data, len);
}

/* bytes off the simulated wire into the socket, with the injected errors */
static void send_raw(const void *data, int len) {
    const char *p = data;
    static char copy[8192];
    if (s_drop && (int)(err_rand() % (unsigned)s_drop) == 0)
        return;
    if (s_corrupt && len <= (int)sizeof copy && (int)(err_rand() % (unsigned)s_corrupt) == 0) {
        memcpy(copy, data, (size_t)len);
        copy[err_rand() % (unsigned)len] ^= 0x5A;
        p = copy;
    }
    while (len > 0) {
        int n = send(s_sock, p, len, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) { SwitchToThread(); continue; }
            printf("link: peer gone (send), exiting\n");
            Sys_Quit();
        }
        p += n;
        len -= n;
    }
}

#ifdef QOG_LINK_TXASYNC
/* QuakeOG (Agent AK): the board's DMA transmit, emulated. A frame is held
   for its time on the simulated wire and handed to the socket at its end,
   while the game goes on, so qog_link.c's background frame state (replies
   held while a frame is out, the resend timer from a frame's end) runs as
   on the board. */
static char   s_txheld[4096];
static int    s_txheld_len;

static void tx_flush_held(void) {
    if (s_txheld_len && now_s() >= s_free_at) {
        int len = s_txheld_len;
        s_txheld_len = 0;
        send_raw(s_txheld, len);
    }
}

int qog_link_hw_tx_busy(void) {
    tx_flush_held();
    return s_txheld_len != 0;
}

void qog_link_hw_tx_frame(const uint8_t *frame, int len) {
    double start = now_s();
    while (qog_link_hw_tx_busy()) SwitchToThread();
    if (len > (int)sizeof s_txheld) Sys_Error("link: frame of %d bytes", len);
    if (s_free_at > start) start = s_free_at;
    memcpy(s_txheld, frame, (size_t)len);
    s_txheld_len = len;
    s_free_at = start + len / s_rate;
}
#endif

int qog_link_hw_read(void *dst, int max) {
    int n;
#ifdef QOG_LINK_TXASYNC
    tx_flush_held();
#endif
    n = recv(s_sock, dst, max, 0);
    if (n > 0) return n;
    if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
        printf("link: peer gone (recv), exiting\n");
        Sys_Quit();
    }
    return 0;
}

uint32_t qog_link_hw_ms(void) { return (uint32_t)(now_s() * 1000.0); }
void qog_link_hw_idle(void) {
#ifdef QOG_LINK_TXASYNC
    tx_flush_held();
#endif
    SwitchToThread();
}
void qog_link_hw_other(const void *payload, int len) { (void)payload; (void)len; }
uint32_t qog_link_hw_rate(void) { return (uint32_t)s_rate; }
