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

viddef_t vid;
unsigned short d_8to16table[256];
unsigned d_8to24table[256];

static byte  vid_buffer[W * H] __attribute__((aligned(4)));
static short zbuffer[W * ZROWS];
static byte  surfcache[1024];   /* no surface cache (engine/d_scanlit.c); D_InitCaches still writes a header */

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
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = vid_buffer;
    vid.rowbytes = vid.conrowbytes = W;
    d_pzbuffer = zbuffer;
    D_InitCaches(surfcache, sizeof surfcache);
    if (!qog_video_init(&vc)) Sys_Error("VID_Init: LCD did not start");
    VID_SetPalette(palette);
    qog_mem_register();
    qog_aim_register();
    qog_mem_set_video((int)(sizeof vid_buffer + sizeof zbuffer + sizeof surfcache));
}

void VID_Shutdown(void) {}

void VID_Update(vrect_t *rects) {
    (void)rects;
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
    case QOG_ACT_PREV_WEAPON: return '\\';
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
