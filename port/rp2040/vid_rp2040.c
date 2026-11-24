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
unsigned short d_8to16table[256];
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

void VID_SetPalette(unsigned char *palette) { qog_video_set_palette_rgb(palette); }
void VID_ShiftPalette(unsigned char *palette) { qog_video_set_palette_rgb(palette); }

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

void VID_Update(vrect_t *rects) {
    (void)rects;
#ifdef QOG_HUD_IN_ZBUFFER
    R_HudFrameEnd();
#endif
    qog_video_present(vid_buffer);
    qog_video_wait();   /* single buffer: the next frame may not draw until scanout ends */
}

void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height) {
    (void)x; (void)y; (void)pbitmap; (void)width; (void)height;
}
void D_EndDirectRect(int x, int y, int width, int height) { (void)x; (void)y; (void)width; (void)height; }
void VID_HandlePause(qboolean pause) { (void)pause; }

/* ---- buttons ---------------------------------------------------------- */

extern uint8_t qog_rp2040_buttons;   /* sys_rp2040.c, from fwog_power_poll */
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

void Sys_SendKeyEvents(void) {
    static int held_key[QOG_ACT_COUNT];
    if (!s_input_ready) { qog_input_reset(&s_input); s_input_ready = true; }
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
