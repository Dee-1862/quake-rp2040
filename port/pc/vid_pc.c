/* Win32 window, 8-bit video and keyboard for the PC development build.
 * Implements id's vid.h plus Sys_SendKeyEvents, so the engine is unchanged.
 *
 * Keyboard mode (default): normal Quake keys.
 * Board mode (F12 toggles): keys 1..5 are the FreeWili OG's buttons
 *   1 gray  2 yellow  3 green  4 blue  5 red
 * fed through platform/qog_input.c, the same code the firmware runs, so the
 * five-button scheme can be playtested here.
 *
 * The title bar is the live RAM meter (port/common/qog_mem.c). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include "quakedef.h"
#include "d_local.h"
#include "qog_input.h"
#include "qog_mem.h"
#include "qog_aim.h"

#ifndef QOG_PC_W
#define QOG_PC_W 320
#endif
#ifndef QOG_PC_H
#define QOG_PC_H 200
#endif
#define WIN_SCALE 3

viddef_t vid;
unsigned short d_8to16table[256];
unsigned d_8to24table[256];

/* id's Options > Video menu calls these unconditionally; NULL crashed the
   game. The mode is fixed on this port (and on the board), so just say so. */
void M_Menu_Options_f(void);
static void vid_menu_draw(void) {
    Draw_String(8 * 8, 6 * 8, "Video mode is fixed:");
    Draw_String(8 * 8, 8 * 8, "320x200, 8-bit");
    Draw_String(8 * 8, 11 * 8, "Press ESC to go back");
}
static void vid_menu_key(int key) {
    if (key == K_ESCAPE) M_Menu_Options_f();
}
void (*vid_menudrawfn)(void) = vid_menu_draw;
void (*vid_menukeyfn)(int key) = vid_menu_key;

/* With QOG_MAX_VIEWSIZE 100 the 3D view stops above the 48-row status bar
   (engine/screen.c, r_main.c), so the z-buffer only needs those rows. */
#ifdef QOG_MAX_VIEWSIZE
#define QOG_PC_ZROWS (QOG_PC_H - 48)
#else
#define QOG_PC_ZROWS QOG_PC_H
#endif
/* z-buffer width and rows. QuakeOG: with QOG_RENDER_SCALE (changes the
   picture, off in the shipped flag lists) the 3D view is drawn smaller and
   the z-buffer only covers it (QOG_ZBUFFER_VIEW, engine/d_local.h). */
#ifdef QOG_RENDER_SCALE
#define QOG_PC_ZW ((QOG_PC_W * QOG_RENDER_SCALE / 100) & ~7)
#define QOG_PC_ZH ((QOG_PC_ZROWS * QOG_RENDER_SCALE / 100) & ~1)
#else
#define QOG_PC_ZW QOG_PC_W
#define QOG_PC_ZH QOG_PC_ZROWS
#endif
#define ZBUFFER_BYTES   (QOG_PC_ZW * QOG_PC_ZH * (int)sizeof(d_zval_t))
#define FRAME_BYTES     (QOG_PC_W * QOG_PC_H)
#ifdef QOG_HUD_IN_ZBUFFER
/* QuakeOG: the frame's rows below the 3D view (the status bar) are the first
   bytes of the z-buffer, which is idle while the 2D overlay is drawn and the
   frame is shown. R_RenderView repaints them after each 3D view
   (engine/r_main.c), so the frame holds the same pixels as with two arrays. */
#ifndef QOG_MAX_VIEWSIZE
#error "QOG_HUD_IN_ZBUFFER needs QOG_MAX_VIEWSIZE"
#endif
#define HUD_BYTES       (QOG_PC_W * (QOG_PC_H - QOG_PC_ZROWS))
static __declspec(align(16)) byte video_mem[QOG_PC_W * QOG_PC_ZROWS +
                                            (ZBUFFER_BYTES > HUD_BYTES ? ZBUFFER_BYTES : HUD_BYTES)];
#define vid_buffer      video_mem
#define zbuffer         ((d_zval_t *)(video_mem + QOG_PC_W * QOG_PC_ZROWS))
#define VIDEO_BYTES     ((int)sizeof video_mem)
#else
static byte  vid_buffer[FRAME_BYTES];
static d_zval_t zbuffer[QOG_PC_ZW * QOG_PC_ZH];
#define VIDEO_BYTES     ((int)(sizeof vid_buffer + sizeof zbuffer))
#endif
static byte *surfcache;
static int   surfcache_size;

static HWND s_wnd;
static struct { BITMAPINFOHEADER h; RGBQUAD pal[256]; } s_bmi;
static qboolean s_pal_changed;   /* QuakeOG (Agent AK): since the last full update */

static qboolean s_board_mode;
static uint8_t s_board_btns;
static qog_input_state_t s_board_state;

/* ---- palette & frames ------------------------------------------------ */

void VID_SetPalette(unsigned char *palette) {
    for (int i = 0; i < 256; i++) {
        s_bmi.pal[i].rgbRed = palette[i * 3];
        s_bmi.pal[i].rgbGreen = palette[i * 3 + 1];
        s_bmi.pal[i].rgbBlue = palette[i * 3 + 2];
        s_bmi.pal[i].rgbReserved = 0;
    }
    s_pal_changed = true;
}

void VID_ShiftPalette(unsigned char *palette) { VID_SetPalette(palette); }

#ifdef QOG_HUD_SKIP
/* QuakeOG (Agent AK): what the board's panel shows. VID_Update copies the
   rows it sends (the status bar rows only when they changed, r_main.c), and
   shots and frame hashes are taken from here, so they show what the LCD
   shows. With QOG_HUD_SKIP_CHECK the status bar rows are still repainted
   in every frame, and a frame that does not send them checks that they
   equal the panel's. */
static byte s_panel[QOG_PC_W * QOG_PC_H];
static unsigned s_hud_frames, s_hud_skips, s_hud_bad;
#define SHOT_PIXELS s_panel
#elif defined(QOG_RTURBO)
/* QuakeOG (Agent AO): a Turbo frame's packed rows as the panel shows them */
static byte s_panel[QOG_PC_W * QOG_PC_H];
static qboolean s_panel_turbo;   /* the last frame presented was a Turbo one */
#define SHOT_PIXELS (s_panel_turbo ? s_panel : vid_buffer)
#else
#define SHOT_PIXELS vid_buffer
#endif
#ifdef QOG_RTURBO
#define TURBO_PACKED r_qog_turbopacked
#else
#define TURBO_PACKED 0
#endif

#ifdef QOG_RTURBO
/* QuakeOG (Agent AP): the characters listed over a packed Turbo view
   (engine/r_qogturbo.c R_QogTurboText), drawn over the doubled rows as the
   board's scanout draws them. Takes the frame's list every frame. */
static void panel_text(byte *panel) {
    extern byte *draw_chars;
    int i, k, r, c;
    const r_qogtext_t *t = R_QogTurboTextTake();
    for (i = 0; i < t->numruns; i++) {
        const unsigned e = t->runs[i];
        const int x = (int)(e & 511), y = (int)((e >> 9) & 255), len = (int)((e >> 17) & 63), first = (int)(e >> 23);
        for (k = 0; k < len; k++) {
            const int num = t->chars[first + k];
            const byte *src = draw_chars + ((num >> 4) << 10) + ((num & 15) << 3);
            for (r = 0; r < 8; r++, src += 128)
                for (c = 0; c < 8; c++)
                    if (src[c]) panel[(y + r) * QOG_PC_W + x + 8 * k + c] = src[c];
        }
    }
}
#endif

/* QuakeOG (Agent AO): the panel's first rows, as the board's scanout sends
   them: rows below `packed` hold QOG_PC_W / 2 pixels each from the start of
   the frame (a Turbo view, engine/r_qogturbo.c), each sent twice */
static void panel_rows(byte *panel, int rows, int packed) {
    int r, x;
    for (r = 0; r < rows; r++) {
        byte *d = panel + r * QOG_PC_W;
        if (r < packed) {
            const byte *s = vid_buffer + r * (QOG_PC_W / 2);
            for (x = 0; x < QOG_PC_W / 2; x++) d[2 * x] = d[2 * x + 1] = s[x];
        } else
            memcpy(d, vid_buffer + r * QOG_PC_W, QOG_PC_W);
    }
}

static void update_title(void) {
    static double last;
    static int frames;
    frames++;
    double now = Sys_FloatTime();
    if (now - last < 0.5) return;
    const qog_mem_t *m = qog_mem_sample();
    char t[256];
    snprintf(t, sizeof t,
             "QuakeOG PC %dx%d [%s]  dyn %d KB (peak %d)  low %d high %d cache %d zone %d/%d KB  %.0f fps",
             QOG_PC_W, QOG_PC_H, s_board_mode ? "BOARD keys 1-5" : "keyboard, F12=board",
             m->total / 1024, m->peak_total / 1024, m->hunk_low / 1024, m->hunk_high / 1024,
             m->cache / 1024, m->zone_used / 1024, m->zone_size / 1024, frames / (now - last));
    SetWindowTextA(s_wnd, t);
    frames = 0;
    last = now;
}

/* `qog_framehash 1` prints a hash of every presented frame (pixels and
   palette) to stdout, so a whole scripted run, tours and fights included,
   can be compared frame by frame against another build. `qog_framehash 0`
   stops it. stdout only, so the console and the frame stay untouched.
   `qog_framehash 1 <first> <last>` also saves the frames with those
   positions in the hash list (counted from 0) as shots fd_<position>.
   While hashing, the console cursor does not blink: its blink follows real
   time, which would make the same frame differ between runs. */
static int s_framehash, s_fh_index, s_fh_first = -1, s_fh_last = -1;
static void write_shot(const char *name);
static void cmd_qog_framehash(void) {
    extern float con_cursorspeed;
    s_framehash = Cmd_Argc() > 1 ? Q_atoi(Cmd_Argv(1)) : !s_framehash;
    if (Cmd_Argc() > 3) {
        s_fh_first = Q_atoi(Cmd_Argv(2));
        s_fh_last = Q_atoi(Cmd_Argv(3));
    }
    con_cursorspeed = s_framehash ? 0 : 4;
}
static void framehash_print(void) {
    unsigned h = 2166136261u;
    for (int i = 0; i < QOG_PC_W * QOG_PC_H; i++) h = (h ^ SHOT_PIXELS[i]) * 16777619u;
    for (int i = 0; i < 256; i++) {
        h = (h ^ s_bmi.pal[i].rgbRed) * 16777619u;
        h = (h ^ s_bmi.pal[i].rgbGreen) * 16777619u;
        h = (h ^ s_bmi.pal[i].rgbBlue) * 16777619u;
    }
    Sys_Printf("fh %d %08x\n", host_framecount, h);
    if (s_fh_index >= s_fh_first && s_fh_index <= s_fh_last) write_shot(va("fd_%05d", s_fh_index));
    s_fh_index++;
}

#ifdef QOG_VID_LOCKWAIT
/* QuakeOG (Agent V): the board's VID_Update leaves the LCD scanout running
   and its VID_LockBuffer waits for it (port/rp2040/vid_rp2040.c). The PC
   checks the rule that makes this safe: from VID_Update to the next
   VID_LockBuffer nothing may change the 320x200 bytes the scanout reads
   (the 3D rows and the status bar rows at the start of the z-buffer).
   `qog_lockcheck` prints the counts. */
static unsigned s_lk_hash, s_lk_frames, s_lk_bad;
static qboolean s_lk_pending;

static unsigned lockcheck_hash(void) {
    unsigned h = 2166136261u;
    for (int i = 0; i < QOG_PC_W * QOG_PC_H; i++) h = (h ^ vid_buffer[i]) * 16777619u;
    return h;
}

#ifdef QOG_VID_SCANRACE
/* QuakeOG (Agent AK): the board's 3D view starts while the last scanout
   runs and waits only for the rows it writes (port/rp2040/vid_rp2040.c).
   The PC checks the rule at every wait: no byte the scanout sends and has
   not released yet may have changed since VID_Update. */
static byte s_sr_copy[QOG_PC_W * QOG_PC_H];
static int s_sr_relrow, s_sr_rows, s_sr_packed;   /* Agent AO: rows released, sent, packed (Turbo) */
static unsigned s_sr_checks, s_sr_waits, s_sr_bad;
static qboolean s_racing;
static qboolean s_sm_race;   /* Agent AP: the view started beside this scanout */
static int s_sm_front;       /* Agent AP: and its front end waited for the whole scan */

/* the bytes scan row r reads: a packed Turbo row QOG_PC_W / 2 from the
   frame's start, the others their own QOG_PC_W */
static void scanrace_check(void) {
    int r;
    s_sr_checks++;
    for (r = s_sr_relrow; r < s_sr_rows; r++) {
        int ofs = r < s_sr_packed ? r * (QOG_PC_W / 2) : r * QOG_PC_W;
        int len = r < s_sr_packed ? QOG_PC_W / 2 : QOG_PC_W;
        if (memcmp(vid_buffer + ofs, s_sr_copy + ofs, (size_t)len)) {
            if (!s_sr_bad++)
                Con_Printf("qog_scanrace: the frame changed before its scanout passed (frame %d)\n", host_framecount);
            break;
        }
    }
}

/* QuakeOG (Agent AO): the rows of the scanout in flight that read a byte of
   [lo, hi), as port/rp2040/vid_rp2040.c */
int VID_QogScanNeed(const void *lo, const void *hi) {
    const int W = QOG_PC_W;
    int l = (int)((const byte *)lo - vid_buffer), h = (int)((const byte *)hi - vid_buffer);
    int p = s_sr_packed, end = s_sr_rows * W, need = 0;
    if (!s_lk_pending || h <= 0 || h <= l) return 0;
    if (l < 0) l = 0;
    if (l < p * (W / 2)) need = ((h < p * (W / 2) ? h : p * (W / 2)) - 1) / (W / 2) + 1;
    if (h > p * W && l < end && end > p * W) {
        int r = ((h < end ? h : end) - 1) / W + 1;
        if (r > need) need = r;
    }
    return need;
}

void VID_QogRaceBegin(void) {
    if (s_lk_pending) s_racing = s_sm_race = true;
}

void VID_QogScanFree(const void *lo, const void *hi) {
    int need = VID_QogScanNeed(lo, hi);
    if (!need) return;
    if (need >= s_sr_rows) {
        if (s_racing) s_sm_front = 1;   /* Agent AP */
        VID_QogScanDone();
        return;
    }
    scanrace_check();
    s_sr_waits++;
    if (need > s_sr_relrow) s_sr_relrow = need;
}

void VID_QogScanDone(void) {
    s_racing = false;
    VID_LockBuffer();
}
#endif

#ifdef QOG_VID_SCANRACE
/* QuakeOG (Agent AP): per frame, the rows the next view waited for before
   the end of the scanout (the front end's caches) and the rows the scanout
   sent, so `qog_scanmodel <pre ms> <world ms>` can work out the board's
   race wait at each LCD row time: the front end waits until the scanout
   has sent its rows, less the work before the view (pre), and after the
   world's front end (world) the view waits for the end of the scanout. */
#define SM_MAX 8192
static unsigned char s_sm_need[SM_MAX], s_sm_rows[SM_MAX], s_sm_turbo[SM_MAX], s_sm_raced[SM_MAX];
static int s_sm_n;

static void scanmodel_record(void) {
    if (s_sm_n >= SM_MAX) return;
    s_sm_need[s_sm_n] = (unsigned char)s_sr_relrow;
    s_sm_rows[s_sm_n] = (unsigned char)s_sr_rows;
    s_sm_turbo[s_sm_n] = (unsigned char)(s_sr_packed > 0);
    s_sm_raced[s_sm_n] = (unsigned char)s_sm_race;
    if (s_sm_front) s_sm_need[s_sm_n] = (unsigned char)s_sr_rows;   /* the front end waited for the whole scan */
    s_sm_n++;
}

static void scanmodel_print(void (*pr)(char *fmt, ...), double pre, double world) {
    /* us per row: the PL022 at 50 MHz (Agent V's 22.4 ms for 200 rows on
       the board) and at 62.5 MHz (250 MHz), then PIO at 50, 60 and 75 MHz,
       5,120 bits a row with no gap and about 0.4 us a line to restart DMA */
    static const struct { const char *name; double us; } rate[] = {
        { "spi 50.0 MHz", 112.0 }, { "spi 62.5 MHz", 91.5 },
        { "pio 50.0 MHz", 102.8 }, { "pio 60.0 MHz", 85.7 }, { "pio 75.0 MHz", 68.7 },
    };
    for (int t = 0; t < 2; t++) {
        int n = 0, raced = 0;
        double need = 0, rows = 0;
        for (int i = 0; i < s_sm_n; i++)
            if (s_sm_turbo[i] == t) { n++; need += s_sm_need[i]; rows += s_sm_rows[i]; raced += s_sm_raced[i]; }
        if (!n) continue;
        pr("qog_scanmodel: %s, %d frames (%d raced), rows waited for avg %.1f, rows sent avg %.1f, pre %.1f ms world %.1f ms\n",
           t ? "Turbo" : "Exact and Fast", n, raced, need / n, rows / n, pre, world);
        for (unsigned k = 0; k < sizeof rate / sizeof rate[0]; k++) {
            double w1 = 0, w2 = 0, scan = 0;
            for (int i = 0; i < s_sm_n; i++) {
                if (s_sm_turbo[i] != t) continue;
                double front = s_sm_need[i] * rate[k].us / 1000.0, end = s_sm_rows[i] * rate[k].us / 1000.0;
                double start = front > pre ? front : pre;
                if (!s_sm_raced[i]) {   /* locked before the view: waits for the end */
                    if (end > pre) w1 += end - pre;
                    scan += end;
                    continue;
                }
                w1 += start - pre;
                if (end > start + world) w2 += end - start - world;
                scan += end;
            }
            pr("qog_scanmodel:   %s scan %.2f ms, race wait %.2f ms (front end %.2f, end of scan %.2f)\n",
               rate[k].name, scan / n, (w1 + w2) / n, w1 / n, w2 / n);
        }
    }
}

static void scanmodel_con(char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Con_Printf("%s", buf);
}

static void cmd_qog_scanmodel(void) {
    scanmodel_print(scanmodel_con, Cmd_Argc() > 1 ? Q_atof(Cmd_Argv(1)) : 5.0,
                    Cmd_Argc() > 2 ? Q_atof(Cmd_Argv(2)) : 8.0);
}
#endif

void VID_LockBuffer(void) {
    if (!s_lk_pending) return;
#ifdef QOG_VID_SCANRACE
    if (s_racing) return;
    scanrace_check();
    scanmodel_record();   /* Agent AP */
    s_lk_pending = false;
    s_lk_frames++;
#else
    s_lk_pending = false;
    s_lk_frames++;
    if (lockcheck_hash() != s_lk_hash && !s_lk_bad++)
        Con_Printf("qog_lockcheck: the frame changed during its scanout (frame %d)\n", host_framecount);
#endif
}

static void cmd_qog_lockcheck(void) {
    Con_Printf("qog_lockcheck: %u scanouts checked, %u changed while scanned\n", s_lk_frames, s_lk_bad);
#ifdef QOG_VID_SCANRACE
    Con_Printf("qog_scanrace: %u checks, %u waits for rows, %u writes before the scanout passed\n",
               s_sr_checks, s_sr_waits, s_sr_bad);
#endif
}
#endif

#ifdef QOG_SPANS_CORE1
/* QuakeOG (Agent V): the board draws half of each lit surface's spans on its
   second core (engine/d_scanlit.c). The PC runs that half on a second
   thread at the same time (default), or on the game's thread before
   (-core1first) or after (-core1last) the first half. */
static HANDLE s_c1_go, s_c1_done;
static void (*volatile s_c1_fn)(void *);
static void *volatile s_c1_arg;
static int s_c1_mode = -1;     /* 0 thread, 1 first, 2 last */

static DWORD WINAPI core1_thread(LPVOID unused) {
    (void)unused;
    for (;;) {
        WaitForSingleObject(s_c1_go, INFINITE);
        s_c1_fn(s_c1_arg);
        SetEvent(s_c1_done);
    }
}

void Sys_QogCore1Start(void (*fn)(void *), void *arg) {
    if (s_c1_mode < 0) {
        s_c1_mode = COM_CheckParm("-core1first") ? 1 : COM_CheckParm("-core1last") ? 2 : 0;
        if (!s_c1_mode) {
            s_c1_go = CreateEventA(NULL, FALSE, FALSE, NULL);
            s_c1_done = CreateEventA(NULL, FALSE, FALSE, NULL);
            CreateThread(NULL, 0, core1_thread, NULL, 0, NULL);
        }
        Con_Printf("QOG_SPANS_CORE1: the second core's spans %s\n",
                   s_c1_mode == 1 ? "first, same thread" : s_c1_mode == 2 ? "last, same thread" : "on a second thread");
    }
    s_c1_fn = fn;
    s_c1_arg = arg;
    if (s_c1_mode == 1) { fn(arg); s_c1_fn = NULL; }
    else if (s_c1_mode == 0) SetEvent(s_c1_go);
}

void Sys_QogCore1Join(void) {
    if (s_c1_mode == 0) WaitForSingleObject(s_c1_done, INFINITE);
    else if (s_c1_mode == 2 && s_c1_fn) s_c1_fn(s_c1_arg);
    s_c1_fn = NULL;
}
#endif

void VID_Update(vrect_t *rects) {
    int rows = QOG_PC_H;
    (void)rects;
#if defined(QOG_HUD_SKIP)
    /* QuakeOG (Agent AK): the rows the board sends this frame */
    rows = R_HudFrameEndRows(!s_pal_changed);
    s_hud_frames++;
    if (rows < QOG_PC_H) {
        s_hud_skips++;
#ifdef QOG_HUD_SKIP_CHECK
        if (memcmp(vid_buffer + rows * QOG_PC_W, s_panel + rows * QOG_PC_W, (size_t)((QOG_PC_H - rows) * QOG_PC_W))
            && !s_hud_bad++)
            Con_Printf("qog_hudskip: the status bar rows not sent differ from the panel's (frame %d)\n", host_framecount);
#endif
    }
    panel_rows(s_panel, rows, TURBO_PACKED);   /* Agent AO: a Turbo view doubled as on the panel */
#ifdef QOG_RTURBO
    panel_text(s_panel);   /* Agent AP: and the characters listed over it */
#endif
#elif defined(QOG_HUD_IN_ZBUFFER)
    R_HudFrameEnd();
#endif
#if !defined(QOG_HUD_SKIP) && defined(QOG_RTURBO)
    s_panel_turbo = TURBO_PACKED > 0;
    if (s_panel_turbo) panel_rows(s_panel, QOG_PC_H, TURBO_PACKED);
    panel_text(s_panel);   /* Agent AP */
#endif
    if (rows >= QOG_PC_H) s_pal_changed = false;
#ifdef QOG_VID_LOCKWAIT
    s_lk_hash = lockcheck_hash();   /* QuakeOG (Agent V): the board's scanout starts here */
    s_lk_pending = true;
#endif
#ifdef QOG_VID_SCANRACE
    memcpy(s_sr_copy, vid_buffer, (size_t)(rows * QOG_PC_W));
    s_sr_relrow = 0;
    s_sr_rows = rows;
    s_sr_packed = TURBO_PACKED;
    s_racing = false;
    s_sm_race = false;   /* Agent AP */
    s_sm_front = 0;
#endif
    if (s_framehash) framehash_print();
#ifdef QOG_EDGES_IN_ZBUFFER
    {   /* QuakeOG (Agent AC): -zedgeshots saves every frame in which the edge
           fallback ran (engine/r_edge.c, R_QogEdgesToStack) as zedge_<frame> */
        extern int r_qog_zstack;
        static int s_zstack_seen;
        if (r_qog_zstack < s_zstack_seen) s_zstack_seen = r_qog_zstack;   /* qog_rpeaks reset */
        if (r_qog_zstack > s_zstack_seen) {
            s_zstack_seen = r_qog_zstack;
            if (COM_CheckParm("-zedgeshots")) {
                Sys_Printf("zedge frame %d\n", host_framecount);
                write_shot(va("zedge_%06d", host_framecount));
            }
        }
    }
#endif
    qog_mem_sample();   /* peaks need every frame, not just title updates */
    HDC dc = GetDC(s_wnd);
    RECT r;
    GetClientRect(s_wnd, &r);
    StretchDIBits(dc, 0, 0, r.right, r.bottom, 0, 0, QOG_PC_W, QOG_PC_H,
#ifdef QOG_RTURBO
                  SHOT_PIXELS,   /* Agent AO: what the panel shows */
#else
                  vid_buffer,
#endif
                  (BITMAPINFO *)&s_bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(s_wnd, dc);
    update_title();
}

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height) {
    (void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}
void D_EndDirectRect(int x, int y, int width, int height) { (void)x; (void)y; (void)width; (void)height; }
void VID_HandlePause(qboolean pause) { (void)pause; }

/* `qog_shot [name]` writes the current 8-bit frame as build-pc-shots/<name>.bmp
   so renderer changes can be compared frame by frame. */
/* `qog_rpeaks [reset]` prints renderer table measurements kept by the engine
   (r_main.c, r_edge.c): edge plus surface bytes in one frame, spans in one
   frame and in one flush, and the span room R_ScanEdges had. */
static void cmd_qog_rpeaks(void) {
    extern int r_qog_peakedgesurfbytes, r_qog_peakspanroom, r_qog_minspanroom,
               r_qog_peakframespans, r_qog_peakflushspans;
    Con_Printf("qog_rpeaks: edge+surf bytes %d, spans per frame %d, per flush %d, span room %d..%d\n",
               r_qog_peakedgesurfbytes, r_qog_peakframespans, r_qog_peakflushspans,
               r_qog_minspanroom, r_qog_peakspanroom);
#ifdef QOG_EDGES_IN_ZBUFFER
    {   /* QuakeOG: edges in the z-buffer (engine/r_edge.c, R_QogScanRows) */
        extern int r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans;
        Con_Printf("qog_rpeaks: z edges, flushes before the edges %d, edge moves %d, "
                   "stack fallbacks %d, most waiting spans %d\n",
                   r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans);
        if (Cmd_Argc() > 1)
            r_qog_zforced = r_qog_zcompacts = r_qog_zstack = r_qog_peakheldspans = 0;
    }
#endif
    if (Cmd_Argc() > 1)
        r_qog_peakedgesurfbytes = r_qog_peakspanroom = r_qog_minspanroom =
            r_qog_peakframespans = r_qog_peakflushspans = 0;
}

#ifdef QOG_BOARD_MENU
/* QuakeOG (Agent X): `qog_key <key> ...` presses and releases each key, so a
   test script can drive the board's menus (engine/menu.c) as the buttons do
   (enter, escape and the arrows). Test builds with QOG_BOARD_MENU only. */
static void cmd_qog_key(void) {
    for (int i = 1; i < Cmd_Argc(); i++) {
        int k = Key_StringToKeynum(Cmd_Argv(i));
        if (k < 0) { Con_Printf("qog_key: no key %s\n", Cmd_Argv(i)); continue; }
        Key_Event(k, true);
        Key_Event(k, false);
    }
}
#endif

#ifdef QOG_ZEDGE_COMPARE
/* Agent AC: a view that reached the edge fallback, drawn exactly (exact) and
   again with the board's graceful path (in the frame now), engine/view.c */
void qog_zedge_compare_shots(byte *exact) {
    static byte noz[QOG_PC_W * QOG_PC_H];
    int i, n = 0;
    memcpy(noz, vid_buffer, sizeof noz);
    for (i = 0; i < QOG_PC_W * QOG_PC_H; i++) n += noz[i] != exact[i];
    Sys_Printf("zedge compare frame %d: %d pixels differ\n", host_framecount, n);
    memcpy(vid_buffer, exact, sizeof noz);
    write_shot(va("zcmp_%06d_exact", host_framecount));
    memcpy(vid_buffer, noz, sizeof noz);
    write_shot(va("zcmp_%06d_noz", host_framecount));
}
#endif
static void cmd_qog_shot(void) {
    write_shot(Cmd_Argc() > 1 ? Cmd_Argv(1) : "shot");
}
static void write_shot(const char *name) {
    char path[256];
    CreateDirectoryA(QOG_SHOT_DIR, NULL);
    snprintf(path, sizeof path, "%s/%s.bmp", QOG_SHOT_DIR, name);
    FILE *f = fopen(path, "wb");
    if (!f) { Con_Printf("qog_shot: cannot write %s\n", path); return; }
    const int pal_bytes = 256 * 4, img = QOG_PC_W * QOG_PC_H;
    BITMAPFILEHEADER fh = {0};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof(BITMAPINFOHEADER) + pal_bytes;
    fh.bfSize = fh.bfOffBits + img;
    BITMAPINFOHEADER ih = s_bmi.h;
    ih.biHeight = -QOG_PC_H;
    ih.biSizeImage = img;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    fwrite(s_bmi.pal, pal_bytes, 1, f);
    fwrite(SHOT_PIXELS, img, 1, f);
    fclose(f);
    if (strncmp(name, "fd_", 3))   /* frame hash dumps stay out of the console */
        Con_Printf("qog_shot: wrote %s (frame %d, cl.time %.3f, realtime %.3f)\n", path, host_framecount, cl.time, realtime);
}

/* ---- keyboard -------------------------------------------------------- */

static int map_vk(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (int)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return (int)vk;
    if (vk >= VK_F1 && vk <= VK_F11) return K_F1 + (int)(vk - VK_F1);
    switch (vk) {
    case VK_UP: return K_UPARROW;
    case VK_DOWN: return K_DOWNARROW;
    case VK_LEFT: return K_LEFTARROW;
    case VK_RIGHT: return K_RIGHTARROW;
    case VK_CONTROL: return K_CTRL;
    case VK_SHIFT: return K_SHIFT;
    case VK_MENU: return K_ALT;
    case VK_RETURN: return K_ENTER;
    case VK_ESCAPE: return K_ESCAPE;
    case VK_BACK: return K_BACKSPACE;
    case VK_TAB: return K_TAB;
    case VK_SPACE: return ' ';
    case VK_PAUSE: return K_PAUSE;
    case VK_OEM_3: return '`';
    case VK_OEM_2: return '/';
    case VK_OEM_5: return '\\';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_1: return ';';
    case VK_OEM_7: return '\'';
    case VK_OEM_4: return '[';
    case VK_OEM_6: return ']';
    }
    return 0;
}

/* Board actions -> the Quake keys they press. FIRE doubles as ENTER while a
   menu or the console has focus, so menus are usable with five buttons. */
static int action_key(int action) {
    switch (action) {
    case QOG_ACT_FORWARD: return K_UPARROW;
    case QOG_ACT_BACK: return K_DOWNARROW;
    case QOG_ACT_TURN_LEFT: return K_LEFTARROW;
    case QOG_ACT_TURN_RIGHT: return K_RIGHTARROW;
    case QOG_ACT_FIRE: return key_dest == key_game ? K_CTRL : K_ENTER;
    case QOG_ACT_JUMP: return ' ';
    case QOG_ACT_DOWN: return 'c';
    case QOG_ACT_NEXT_WEAPON: return '/';
    case QOG_ACT_PREV_WEAPON:
        /* id's default.cfg binds '\' to +mlook, not to a weapon. QuakeC's
           impulse 12 is the previous weapon (CycleWeaponReverseCommand). */
        if (key_dest == key_game) Cbuf_AddText("impulse 12\n");
        return 0;
    case QOG_ACT_MENU: return K_ESCAPE;
    }
    return 0;
}

static void board_tick(void) {
    static int held_key[QOG_ACT_COUNT];
    qog_input_t in = qog_input_step(&s_board_state, s_board_btns, (uint32_t)(Sys_FloatTime() * 1000.0));
    for (int a = 0; a < QOG_ACT_COUNT; a++) {
        if (in.pressed & QOG_ACT_BIT(a)) {
            held_key[a] = action_key(a);   /* remember it: FIRE's key depends on focus */
            if (held_key[a]) Key_Event(held_key[a], true);
        }
        if ((in.released & QOG_ACT_BIT(a)) && held_key[a]) {
            Key_Event(held_key[a], false);
            held_key[a] = 0;
        }
    }
}

static void board_key(WPARAM vk, qboolean down) {
    static const uint8_t bit[5] = { 1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4 }; /* gray yellow green blue red */
    if (vk >= '1' && vk <= '5') {
        if (down) s_board_btns |= bit[vk - '1'];
        else s_board_btns &= (uint8_t)~bit[vk - '1'];
    }
}

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
    case WM_KEYUP: case WM_SYSKEYUP: {
        const qboolean down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (wp == VK_F12) {
            if (down && !(lp & (1 << 30))) {
                s_board_mode = !s_board_mode;
                s_board_btns = 0;
                Con_Printf("Board mode %s (keys 1-5 = gray yellow green blue red)\n", s_board_mode ? "ON" : "off");
            }
            return 0;
        }
        if (s_board_mode && wp != VK_OEM_3) board_key(wp, down);
        else if (map_vk(wp)) Key_Event(map_vk(wp), down);
        return 0;
    }
    case WM_CLOSE:
        Sys_Quit();
        return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

void Sys_SendKeyEvents(void) {
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    if (s_board_mode) board_tick();
}

/* ---- init ------------------------------------------------------------ */

void VID_Init(unsigned char *palette) {
    vid.maxwarpwidth = vid.width = vid.conwidth = QOG_PC_W;
    vid.maxwarpheight = vid.height = vid.conheight = QOG_PC_H;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0f / 240.0f);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - vid.colormap[16384];   /* one byte after the 64x256 table; the colormap may be read in place from flash, where the 3 bytes after the file are not ours */
    vid.buffer = vid.conbuffer = vid_buffer;
    vid.rowbytes = vid.conrowbytes = QOG_PC_W;

    d_pzbuffer = zbuffer;
#if defined(QOG_ZBUFFER_VIEW) || defined(QOG_RENDER_SCALE)
    d_zbase = zbuffer;
#endif
#ifdef QOG_HUD_IN_ZBUFFER
    r_zalias_row = QOG_PC_ZROWS;
#endif
#ifdef QOG_ZTEMP
    /* QuakeOG: short-lived renderer tables use the z-buffer (engine/r_shared.h) */
    r_ztemp_base = (byte *)zbuffer;
    r_ztemp_bytes = ZBUFFER_BYTES & ~3;
#endif
#ifdef QOG_SURFCACHE
    surfcache_size = D_SurfaceCacheForRes(QOG_PC_W, QOG_PC_H);
#else
    /* The world is lit per pixel (engine/d_scanlit.c), so nothing allocates
       from the surface cache. D_InitCaches still writes a header and guard. */
    surfcache_size = 1024;
#endif
    surfcache = malloc(surfcache_size);
    if (!surfcache) Sys_Error("no memory for the surface cache");
    D_InitCaches(surfcache, surfcache_size);
    Sys_Printf("VID: %dx%d, frame %d KB, z-buffer %d KB, video memory %d KB, surface cache %d KB\n", QOG_PC_W,
               QOG_PC_H, FRAME_BYTES / 1024, ZBUFFER_BYTES / 1024, VIDEO_BYTES / 1024, surfcache_size / 1024);

    s_bmi.h.biSize = sizeof s_bmi.h;
    s_bmi.h.biWidth = QOG_PC_W;
    s_bmi.h.biHeight = -QOG_PC_H;   /* top-down */
    s_bmi.h.biPlanes = 1;
    s_bmi.h.biBitCount = 8;
    s_bmi.h.biCompression = BI_RGB;
    s_bmi.h.biClrUsed = 256;
    VID_SetPalette(palette);
    qog_input_reset(&s_board_state);
    qog_mem_register();   /* Cmd_AddCommand is refused once Host_Init finishes */
    qog_aim_register();
    Cmd_AddCommand("qog_shot", cmd_qog_shot);
#ifdef QOG_VID_LOCKWAIT
    Cmd_AddCommand("qog_lockcheck", cmd_qog_lockcheck);   /* QuakeOG (Agent V) */
#endif
#ifdef QOG_VID_SCANRACE
    Cmd_AddCommand("qog_scanmodel", cmd_qog_scanmodel);   /* QuakeOG (Agent AP) */
#endif
    Cmd_AddCommand("qog_framehash", cmd_qog_framehash);
    Cmd_AddCommand("qog_rpeaks", cmd_qog_rpeaks);
#ifdef QOG_BOARD_MENU
    Cmd_AddCommand("qog_key", cmd_qog_key);
#endif
    {
        extern void qog_manifest_register(void);
        qog_manifest_register();
        extern void qog_tour_register(void);
        qog_tour_register();
        extern void qog_fly_register(void);
        qog_fly_register();
        extern void qog_demo_register(void);
        qog_demo_register();
    }
    qog_mem_set_video(VIDEO_BYTES + surfcache_size);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "QuakeOGPC";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    RECT r = { 0, 0, QOG_PC_W * WIN_SCALE, QOG_PC_H * WIN_SCALE };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    /* -hidden: scripted test runs render and take screenshots without
       opening a visible window on the user's desktop. */
    const DWORD vis = COM_CheckParm("-hidden") ? 0 : WS_VISIBLE;
    s_wnd = CreateWindowA("QuakeOGPC", "QuakeOG PC", WS_OVERLAPPEDWINDOW | vis, CW_USEDEFAULT,
                          CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!s_wnd) Sys_Error("could not create the window");
}

void VID_Shutdown(void) {
#ifdef QOG_VID_LOCKWAIT
    Sys_Printf("qog_lockcheck: %u scanouts checked, %u changed while scanned\n", s_lk_frames, s_lk_bad);
#endif
#ifdef QOG_VID_SCANRACE
    Sys_Printf("qog_scanrace: %u checks, %u waits for rows, %u writes before the scanout passed\n",
               s_sr_checks, s_sr_waits, s_sr_bad);
#ifdef QOG_RTURBO
    {   /* Agent AP: characters over packed Turbo views (engine/r_qogturbo.c) */
        extern int r_qog_turbotextmax, r_qog_turbotextover, r_qog_turbotextframes;
        Sys_Printf("qog_turbotext: %d frames with characters over a packed view, most in a frame %d of %d, %d did not fit\n",
                   r_qog_turbotextframes, r_qog_turbotextmax, QOG_TURBO_CHARS, r_qog_turbotextover);
    }
#endif
    if (COM_CheckParm("-scanmodel")) {   /* Agent AP: the board's race wait, a few guesses of pre */
        scanmodel_print(Sys_Printf, 3.0, 8.6);
        scanmodel_print(Sys_Printf, 5.0, 8.6);
        scanmodel_print(Sys_Printf, 7.0, 8.6);
    }
#endif
#ifdef QOG_HUD_SKIP
#ifdef QOG_HUD_SKIP_CHECK
#define HUDCHK " (checked)"
#else
#define HUDCHK " (not checked)"
#endif
    Sys_Printf("qog_hudskip: %u frames, %u without the status bar rows, %u of them differed from the panel%s\n",
               s_hud_frames, s_hud_skips, s_hud_bad, HUDCHK);
#endif
#ifdef QOG_EDGES_IN_ZBUFFER
    {   /* QuakeOG: the whole run's figures for the edges in the z-buffer */
        extern int r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans,
                   r_qog_minspanroom, r_qog_peakframespans;
        Sys_Printf("z edges at exit: flushes before the edges %d, edge moves %d, stack fallbacks %d, "
                   "most waiting spans %d, least span room %d, most spans in a frame %d\n",
                   r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans,
                   r_qog_minspanroom, r_qog_peakframespans);
#ifdef QOG_CL_PEAKS
        {   /* QuakeOG (Agent S) */
            extern int r_qog_zstackedges;
            Sys_Printf("z edges at exit: most edges in one stack fallback %d\n", r_qog_zstackedges);
        }
#endif
#if defined(QOG_ZEDGE_STACK_GUARD) || defined(QOG_ZEDGE_FORCE_NOZ)
        {   /* QuakeOG (Agent AC): the graceful fallback, R_QogFlushNoZ */
            extern int r_qog_znoz, r_qog_znozrows;
            Sys_Printf("z edges at exit: flushes without z %d, rows drawn without z %d\n", r_qog_znoz, r_qog_znozrows);
        }
#endif
    }
#endif
    if (s_wnd) DestroyWindow(s_wnd);
    s_wnd = NULL;
}
