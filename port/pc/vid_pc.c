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
}

void VID_ShiftPalette(unsigned char *palette) { VID_SetPalette(palette); }

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
    for (int i = 0; i < QOG_PC_W * QOG_PC_H; i++) h = (h ^ vid_buffer[i]) * 16777619u;
    for (int i = 0; i < 256; i++) {
        h = (h ^ s_bmi.pal[i].rgbRed) * 16777619u;
        h = (h ^ s_bmi.pal[i].rgbGreen) * 16777619u;
        h = (h ^ s_bmi.pal[i].rgbBlue) * 16777619u;
    }
    Sys_Printf("fh %d %08x\n", host_framecount, h);
    if (s_fh_index >= s_fh_first && s_fh_index <= s_fh_last) write_shot(va("fd_%05d", s_fh_index));
    s_fh_index++;
}

void VID_Update(vrect_t *rects) {
    (void)rects;
#ifdef QOG_HUD_IN_ZBUFFER
    R_HudFrameEnd();
#endif
    if (s_framehash) framehash_print();
    qog_mem_sample();   /* peaks need every frame, not just title updates */
    HDC dc = GetDC(s_wnd);
    RECT r;
    GetClientRect(s_wnd, &r);
    StretchDIBits(dc, 0, 0, r.right, r.bottom, 0, 0, QOG_PC_W, QOG_PC_H,
                  vid_buffer, (BITMAPINFO *)&s_bmi, DIB_RGB_COLORS, SRCCOPY);
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
    fwrite(vid_buffer, img, 1, f);
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
    case QOG_ACT_PREV_WEAPON: return '\\';
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
    Cmd_AddCommand("qog_framehash", cmd_qog_framehash);
    Cmd_AddCommand("qog_rpeaks", cmd_qog_rpeaks);
    {
        extern void qog_manifest_register(void);
        qog_manifest_register();
        extern void qog_tour_register(void);
        qog_tour_register();
        extern void qog_fly_register(void);
        qog_fly_register();
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
#ifdef QOG_EDGES_IN_ZBUFFER
    {   /* QuakeOG: the whole run's figures for the edges in the z-buffer */
        extern int r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans,
                   r_qog_minspanroom, r_qog_peakframespans;
        Sys_Printf("z edges at exit: flushes before the edges %d, edge moves %d, stack fallbacks %d, "
                   "most waiting spans %d, least span room %d, most spans in a frame %d\n",
                   r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans,
                   r_qog_minspanroom, r_qog_peakframespans);
    }
#endif
    if (s_wnd) DestroyWindow(s_wnd);
    s_wnd = NULL;
}
