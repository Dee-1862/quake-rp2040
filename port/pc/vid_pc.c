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
void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);

static byte  vid_buffer[QOG_PC_W * QOG_PC_H];
static short zbuffer[QOG_PC_W * QOG_PC_H];
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

void VID_Update(vrect_t *rects) {
    (void)rects;
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
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));
    vid.buffer = vid.conbuffer = vid_buffer;
    vid.rowbytes = vid.conrowbytes = QOG_PC_W;

    d_pzbuffer = zbuffer;
    surfcache_size = D_SurfaceCacheForRes(QOG_PC_W, QOG_PC_H);
    surfcache = malloc(surfcache_size);
    if (!surfcache) Sys_Error("no memory for the surface cache");
    D_InitCaches(surfcache, surfcache_size);
    Sys_Printf("VID: %dx%d, frame %d KB, z-buffer %d KB, surface cache %d KB\n", QOG_PC_W, QOG_PC_H,
               (int)(sizeof vid_buffer / 1024), (int)(sizeof zbuffer / 1024), surfcache_size / 1024);

    s_bmi.h.biSize = sizeof s_bmi.h;
    s_bmi.h.biWidth = QOG_PC_W;
    s_bmi.h.biHeight = -QOG_PC_H;   /* top-down */
    s_bmi.h.biPlanes = 1;
    s_bmi.h.biBitCount = 8;
    s_bmi.h.biCompression = BI_RGB;
    s_bmi.h.biClrUsed = 256;
    VID_SetPalette(palette);
    qog_input_reset(&s_board_state);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "QuakeOGPC";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);
    RECT r = { 0, 0, QOG_PC_W * WIN_SCALE, QOG_PC_H * WIN_SCALE };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    s_wnd = CreateWindowA("QuakeOGPC", "QuakeOG PC", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                          CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!s_wnd) Sys_Error("could not create the window");
}

void VID_Shutdown(void) {
    if (s_wnd) DestroyWindow(s_wnd);
    s_wnd = NULL;
}
