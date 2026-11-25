/* RP2040 video and input: id's vid.h plus Sys_SendKeyEvents.
 *
 * The 8-bit frame goes to the LCD through platform/qog_video (core 1, DMA,
 * palette lookup per line). The z-buffer covers only the 3D view rows
 * (QOG_MAX_VIEWSIZE keeps the view above the 48-row status bar). Buttons go
 * through platform/qog_input, the same control map the PC board mode uses. */
#include "quakedef.h"
#include "d_local.h"
#include "qog_input.h"
#include "qog_video.h"
#include "qog_mem.h"
#include "qog_aim.h"
#include "qog_phase.h"

#define W 320
#define H 200
#ifdef QOG_MAX_VIEWSIZE
#define ZROWS (H - 48)
#else
#define ZROWS H
#endif

/* z-buffer width and rows. QuakeOG: with QOG_RENDER_SCALE (changes the
   picture, off in the shipped flag lists) the 3D view is drawn smaller and
   the z-buffer only covers it (QOG_ZBUFFER_VIEW, engine/d_local.h). */
#ifdef QOG_RENDER_SCALE
#define ZW ((W * QOG_RENDER_SCALE / 100) & ~7)
#define ZH ((ZROWS * QOG_RENDER_SCALE / 100) & ~1)
#else
#define ZW W
#define ZH ZROWS
#endif
#define ZBUFFER_BYTES   (ZW * ZH * (int)sizeof(d_zval_t))

viddef_t vid;
#ifdef QOG_VID_NO_8TO16
const unsigned short d_8to16table[256];   /* QuakeOG (Agent S): see vid.h */
#else
unsigned short d_8to16table[256];
#endif
unsigned d_8to24table[256];

#ifdef QOG_HUD_IN_ZBUFFER
/* QuakeOG: the frame's rows below the 3D view (the status bar) are the first
   bytes of the z-buffer. The z-buffer is idle while the 2D overlay is drawn
   and while core 1 scans the frame out (VID_Update waits for the scanout
   before the next frame can draw), and R_RenderView repaints those rows
   after each 3D view (engine/r_main.c). Same pixels, 15 KB less. */
#if !defined(QOG_MAX_VIEWSIZE)
#error "QOG_HUD_IN_ZBUFFER needs QOG_MAX_VIEWSIZE"
#endif
#define HUD_BYTES       (W * (H - ZROWS))
static byte  video_mem[W * ZROWS + (ZBUFFER_BYTES > HUD_BYTES ? ZBUFFER_BYTES : HUD_BYTES)]
    __attribute__((aligned(4)));
#define vid_buffer      video_mem
#define zbuffer         ((d_zval_t *)(video_mem + W * ZROWS))
#define VIDEO_BYTES     ((int)sizeof video_mem)
#else
static byte  vid_buffer[W * H] __attribute__((aligned(4)));
static d_zval_t zbuffer[ZW * ZH];
#define VIDEO_BYTES     ((int)(sizeof vid_buffer + sizeof zbuffer))
#endif
#ifdef QOG_VID_SURFCACHE_MIN
/* QuakeOG (Agent S): nothing allocates from it without QOG_SURFCACHE, so
   only D_InitCaches' header and guard (D_InitCaches prints 0k) */
static byte  surfcache[64] __attribute__((aligned(4)));
#else
static byte  surfcache[1024];   /* no surface cache (engine/d_scanlit.c); D_InitCaches still writes a header */
#endif

static void vid_menu_draw(void) { Draw_String(8 * 8, 6 * 8, "Video mode is fixed: 320x200"); }
void M_Menu_Options_f(void);
static void vid_menu_key(int key) { if (key == K_ESCAPE) M_Menu_Options_f(); }
void (*vid_menudrawfn)(void) = vid_menu_draw;
void (*vid_menukeyfn)(int key) = vid_menu_key;

#ifdef QOG_HUD_SKIP
static bool s_pal_changed;
unsigned vid_qog_frames, vid_qog_hudsent;   /* Agent AK: for the diag line (link_rp2040.c) */
#define PAL_CHANGED()   (s_pal_changed = true)
#else
#define PAL_CHANGED()
#endif
void VID_SetPalette(unsigned char *palette) { qog_video_set_palette_rgb(palette); PAL_CHANGED(); }
void VID_ShiftPalette(unsigned char *palette) { qog_video_set_palette_rgb(palette); PAL_CHANGED(); }

void VID_Init(unsigned char *palette) {
    const qog_video_config_t vc = { W, H, H };
    vid.maxwarpwidth = vid.width = vid.conwidth = W;
    vid.maxwarpheight = vid.height = vid.conheight = H;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0f / 240.0f);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - vid.colormap[16384];   /* one byte after the 64x256 table; the colormap may be read in place from flash, where the 3 bytes after the file are not ours */
    vid.buffer = vid.conbuffer = vid_buffer;
    vid.rowbytes = vid.conrowbytes = W;
    d_pzbuffer = zbuffer;
#if defined(QOG_ZBUFFER_VIEW) || defined(QOG_RENDER_SCALE)
    d_zbase = zbuffer;
#endif
#ifdef QOG_HUD_IN_ZBUFFER
    r_zalias_row = ZROWS;
#endif
#ifdef QOG_ZTEMP
    /* QuakeOG: short-lived renderer tables use the z-buffer (engine/r_shared.h) */
    r_ztemp_base = (byte *)zbuffer;
    r_ztemp_bytes = ZBUFFER_BYTES & ~3;
#endif
    D_InitCaches(surfcache, sizeof surfcache);
    if (!qog_video_init(&vc)) Sys_Error("VID_Init: LCD did not start");
    VID_SetPalette(palette);
    qog_mem_register();
    qog_aim_register();
    qog_mem_set_video(VIDEO_BYTES + (int)sizeof surfcache);
}

void VID_Shutdown(void) {}

#ifdef QOG_VID_LOCKWAIT
/* QuakeOG (Agent V): VID_Update leaves the scanout running and the first
   VID_LockBuffer after it (engine/quakedef.h) waits for its end, so the next
   frame's input, link exchange and parsing overlap the 22 ms scanout. Only
   drawing into the frame or the status bar rows needs the wait, and id's
   code locks the buffer before any of it (screen.c, draw.c, menu.c). The
   z-buffer temporaries that parsing uses (QOG_PRECACHE_ZTEMP,
   QOG_LOADGAME_ZTEMP) stay clear of the status bar rows. */
static bool s_scan_pending;
#ifdef QOG_VID_SCANRACE
static bool s_racing;           /* Agent AK: the 3D view runs beside the scanout */
static uint8_t s_scan_rows;     /* rows the scanout in flight sends */
static uint8_t s_scan_packed;   /* Agent AO: its first rows that hold a packed Turbo view */
#endif

void VID_LockBuffer(void) {
    if (!s_scan_pending) return;
#ifdef QOG_VID_SCANRACE
    if (s_racing) return;       /* the view waits where it writes (VID_QogScanFree) */
#endif
    QOG_PH_T(hp_wait);
    qog_video_wait();
    QOG_HP_ADD(QOG_HP_SCANWAIT, hp_wait);
    s_scan_pending = false;
}

#ifdef QOG_VID_SCANRACE
/* QuakeOG (Agent AK): the scanout reads the frame's rows from the top down,
   each once into a line buffer (platform/qog_video.c), and the world's
   front end at the start of a view writes only its edge cache and vertex
   cache in the first rows and its edge table at the end of the z-buffer.
   So the view starts while the last frame is sent and waits only until the
   scanout has read the rows it is about to write (engine/r_main.c). Same
   pixels on the panel: no row changes before it was sent. */
void VID_QogRaceBegin(void) {
    if (s_scan_pending) s_racing = true;
}

/* QuakeOG (Agent AO): the rows of the scanout in flight that read a byte of
   [lo, hi). Rows below s_scan_packed read W / 2 bytes each from the start
   of the frame (a Turbo view), the others their W bytes. */
int VID_QogScanNeed(const void *lo, const void *hi) {
    int l = (const byte *)lo - vid_buffer, h = (const byte *)hi - vid_buffer;
    int p = s_scan_packed, end = s_scan_rows * W, need = 0;
    if (!s_scan_pending || h <= 0 || h <= l) return 0;
    if (l < 0) l = 0;
    if (l < p * (W / 2)) need = ((h < p * (W / 2) ? h : p * (W / 2)) - 1) / (W / 2) + 1;
    if (h > p * W && l < end) {
        int r = ((h < end ? h : end) - 1) / W + 1;
        if (r > need) need = r;
    }
    return need;
}

void VID_QogScanFree(const void *lo, const void *hi) {
    int need = VID_QogScanNeed(lo, hi);
    if (!need) return;
    if (need >= s_scan_rows) {
        VID_QogScanDone();
        return;
    }
    QOG_PH_T(hp_wait);
    qog_video_wait_rows((uint32_t)need);
    QOG_HP_ADD(QOG_HP_RACEWAIT, hp_wait);
}

void VID_QogScanDone(void) {
    s_racing = false;
    VID_LockBuffer();
}
#endif
#endif



/* QuakeOG (Agent AO): the frame's first rows may hold a Turbo view packed
   W / 2 bytes a row, which the scanout sends twice as wide (r_qogturbo.c) */
#ifdef QOG_RTURBO
#define TURBO_PACKED    r_qog_turbopacked
#else
#define TURBO_PACKED    0
#endif

void VID_Update(vrect_t *rects) {
    QOG_PH_T(hp_present);   /* QuakeOG (Agent V) */
    (void)rects;
#if defined(QOG_HUD_SKIP)
    /* QuakeOG (Agent AK): only the rows above the status bar when the panel
       shows the status bar rows this frame has, in the same palette */
    {
        int rows = R_HudFrameEndRows(!s_pal_changed);
        if (rows >= H) s_pal_changed = false;
        vid_qog_frames++;
        if (rows >= H) vid_qog_hudsent++;
        qog_video_present_packed(vid_buffer, (uint32_t)rows, (uint32_t)TURBO_PACKED);
#ifdef QOG_VID_SCANRACE
        s_scan_rows = (uint8_t)rows;
        s_scan_packed = (uint8_t)TURBO_PACKED;
        s_racing = false;
#endif
    }
#else
#ifdef QOG_HUD_IN_ZBUFFER
    R_HudFrameEnd();
#endif
    qog_video_present_packed(vid_buffer, H, (uint32_t)TURBO_PACKED);
#ifdef QOG_VID_SCANRACE
    s_scan_rows = H;
    s_scan_packed = (uint8_t)TURBO_PACKED;
    s_racing = false;
#endif
#endif
#ifdef QOG_VID_LOCKWAIT
    s_scan_pending = true;
#else
    qog_video_wait();   /* single buffer: the next frame may not draw until scanout ends */
#endif
    QOG_HP_ADD(QOG_HP_PRESENT, hp_present);
}

#ifdef QOG_SPANS_CORE1
/* QuakeOG (Agent V): half of each lit surface's spans on core 1
   (engine/d_scanlit.c). Spans are drawn only after the scanout ended (the
   frame is locked), so core 1 is free and starts at once. */
void Sys_QogCore1Start(void (*fn)(void *), void *arg) { qog_video_core1_start(fn, arg); }
void Sys_QogCore1Join(void) { qog_video_core1_join(); }
#endif

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height) {
    (void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}
void D_EndDirectRect(int x, int y, int width, int height) { (void)x; (void)y; (void)width; (void)height; }
void VID_HandlePause(qboolean pause) { (void)pause; }

/* ---- buttons ---------------------------------------------------------- */

extern uint8_t qog_rp2040_buttons;   /* sys_rp2040.c, from fwog_power_poll */
extern uint8_t qog_rp2040_buttons_seen;
static qog_input_state_t s_input;
static qboolean s_input_ready;

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

/* QuakeOG: menus and the console get a layout of their own, since the game's
   combos make them hard to drive (down was red + yellow, and left + right is
   the menu chord). Each button press is one key press and release:
     yellow up, blue down, green left, red right, gray enter,
     green + blue escape (the same chord that opens the menu). */
static bool s_in_menu;        /* the last step drove the menu layout */
static bool s_wait_release;   /* back in the game: ignore buttons until all are up */
static uint8_t s_menu_prev;
static bool s_menu_chord;

static void menu_keys(uint8_t b, uint8_t seen) {
#define prev  s_menu_prev
#define chord s_menu_chord
    if (!s_in_menu) {
        /* buttons already down when the menu opened (the chord that opened
           it) are not presses here */
        s_in_menu = true;
        prev = b;
        chord = (b & ((1u << 2) | (1u << 3))) == ((1u << 2) | (1u << 3));
        return;
    }
    const uint8_t gray = 1u << 0, yellow = 1u << 1, green = 1u << 2, blue = 1u << 3, red = 1u << 4;
    const uint8_t went_down = (uint8_t)((b | seen) & ~prev);
    static const struct { uint8_t bit; int key; } map[] = {
        { 1u << 1, K_UPARROW }, { 1u << 3, K_DOWNARROW }, { 1u << 2, K_LEFTARROW },
        { 1u << 4, K_RIGHTARROW }, { 1u << 0, K_ENTER },
    };
    (void)gray; (void)yellow; (void)red;
    if ((b & (green | blue)) == (green | blue)) {
        if (!chord) { Key_Event(K_ESCAPE, true); Key_Event(K_ESCAPE, false); chord = true; }
    } else {
        if (!(b & (green | blue))) chord = false;
        if (!chord)
            for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
                if (went_down & map[i].bit) { Key_Event(map[i].key, true); Key_Event(map[i].key, false); }
    }
    prev = b;
#undef prev
#undef chord
}

void Sys_SendKeyEvents(void) {
    static int held_key[QOG_ACT_COUNT];
    if (!s_input_ready) { qog_input_reset(&s_input); s_input_ready = true; }
    if (key_dest != key_game) {
        /* release anything the game layout still holds, then drive the menu */
        for (int a = 0; a < QOG_ACT_COUNT; a++)
            if (held_key[a]) { Key_Event(held_key[a], false); held_key[a] = 0; }
        qog_input_reset(&s_input);
        menu_keys(qog_rp2040_buttons, qog_rp2040_buttons_seen);
        qog_rp2040_buttons_seen = 0;
        return;
    }
    if (s_in_menu) {
        /* buttons still down when the menu closes (green + blue that closed
           it) must not act in the game, or the chord would reopen the menu */
        s_in_menu = false;
        s_wait_release = true;
    }
    if (s_wait_release) {
        if (qog_rp2040_buttons) { qog_rp2040_buttons_seen = 0; return; }
        s_wait_release = false;
        qog_input_reset(&s_input);
    }
    s_input.seen |= qog_rp2040_buttons_seen;   /* taps between game frames */
    qog_rp2040_buttons_seen = 0;
    qog_input_t in = qog_input_step(&s_input, qog_rp2040_buttons, (uint32_t)(Sys_FloatTime() * 1000.0));
    for (int a = 0; a < QOG_ACT_COUNT; a++) {
        if (in.pressed & QOG_ACT_BIT(a)) {
            held_key[a] = action_key(a);
            if (held_key[a]) Key_Event(held_key[a], true);
        }
        if ((in.released & QOG_ACT_BIT(a)) && held_key[a]) {
            Key_Event(held_key[a], false);
            held_key[a] = 0;
        }
    }
}
