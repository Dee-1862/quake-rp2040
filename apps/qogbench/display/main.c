/* QuakeOG display benchmark: how fast is Quake's world drawing on this chip?
 *
 * Times the inner loops the engine spends its frame in, on the real RP2040
 * (Cortex-M0+, no FPU), with textures read from the flash data region the way
 * the game reads them. Nothing here is a game: the scene is one textured
 * plane seen in perspective, filling the 320x152 3D view.
 *
 *   lit_flash   engine/d_scanlit.c D_DrawSpans8Lit + LitPixel, copied as is,
 *               running from XIP flash: texel from flash, 4-sample light
 *               blend, colormap, two modulos per pixel (the texture is not
 *               a power of two). This is what the game does today.
 *   lit_ram     the same code placed in RAM (__not_in_flash_func).
 *   lit_pow2    lit_ram with a 64x64 texture and masks instead of modulos.
 *   surfcache   id's D_DrawSpans8 from a lit surface cache in RAM (no
 *               lighting per pixel), for comparison with id's design.
 *   zspans      id's D_DrawZSpans: 16-bit depth for every view pixel.
 *   fdiv        one float divide (the per-8-pixel perspective step).
 *   xip_seq     64 KB memcpy from the flash data region.
 *   xip_rand    64 K byte reads scattered over 1 MB of flash (cache misses).
 *
 * Every 2 s it prints one line over the USB console (fw console or
 * tools/bench.py --watch). The screen shows the last lit frame. No sound.
 */
#include <string.h>
#include "fwog_display.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "qog_data.h"
#include "qog_video.h"

FWOG_POWER_DEFAULT();

#define W        320
#define H        200
#define VIEW_H   152          /* 3D view above the 48-row status bar */
#define FRAMES   10

typedef int fixed16_t;
typedef struct espan_s { int u, v, count; struct espan_s *pnext; } espan_t;

static uint8_t  s_fb[W * H] __attribute__((aligned(4)));
static short    s_z[W * VIEW_H];
static espan_t  s_spans[VIEW_H];
static uint8_t  s_colormap[64 * 256];       /* the game reads it from flash */
static unsigned blocklights[18 * 18];
static uint8_t  s_cache[64 * 64];           /* stands in for a surface cache block */
static uint8_t  s_tex64[64 * 64];

/* Span drawer state, the same globals id's d_scan.c uses. */
static uint8_t *d_viewbuffer = s_fb;
static int      screenwidth = W;
static float    d_sdivzstepu, d_tdivzstepu, d_zistepu;
static float    d_sdivzstepv, d_tdivzstepv, d_zistepv;
static float    d_sdivzorigin, d_tdivzorigin, d_ziorigin;
static fixed16_t sadjust, tadjust, bbextents, bbextentt;

static const uint8_t *lit_tex;
static int lit_tw, lit_th, lit_soff, lit_toff, lit_smax, lit_lshift;
static const uint8_t *lit_colormap = s_colormap;

/* ---- engine/d_scanlit.c, copied ---- */
#define LITPIXEL_BODY(MODX, MODY)                                              \
    int tx, ty, ls, lt, fx, fy, top, bot, light;                               \
    unsigned *l;                                                               \
    tx = MODX;                                                                 \
    ty = MODY;                                                                 \
    ls = s >> lit_lshift;                                                      \
    lt = t >> lit_lshift;                                                      \
    fx = (ls >> 8) & 0xFF;                                                     \
    fy = (lt >> 8) & 0xFF;                                                     \
    l = blocklights + (lt >> 16) * lit_smax + (ls >> 16);                      \
    top = (int)l[0] + ((((int)l[1] - (int)l[0]) * fx) >> 8);                   \
    bot = (int)l[lit_smax] + ((((int)l[lit_smax+1] - (int)l[lit_smax]) * fx) >> 8); \
    light = top + (((bot - top) * fy) >> 8);                                   \
    return lit_colormap[(light & 0xFF00) + lit_tex[ty * lit_tw + tx]];

static uint8_t LitPixel_flash(fixed16_t s, fixed16_t t) {
    LITPIXEL_BODY(((s >> 16) + lit_soff) % lit_tw, ((t >> 16) + lit_toff) % lit_th)
}
static uint8_t __not_in_flash_func(LitPixel_ram)(fixed16_t s, fixed16_t t) {
    LITPIXEL_BODY(((s >> 16) + lit_soff) % lit_tw, ((t >> 16) + lit_toff) % lit_th)
}
static uint8_t __not_in_flash_func(LitPixel_pow2)(fixed16_t s, fixed16_t t) {
    LITPIXEL_BODY(((s >> 16) + lit_soff) & 63, ((t >> 16) + lit_toff) & 63)
}
static uint8_t __not_in_flash_func(CachePixel)(fixed16_t s, fixed16_t t) {
    return s_cache[((t >> 16) & 63) * 64 + ((s >> 16) & 63)];
}

/* D_DrawSpans8 / D_DrawSpans8Lit, with the pixel fetch passed in. */
#define DRAWSPANS_BODY(PIXEL)                                                  \
    int count, spancount;                                                      \
    unsigned char *pdest;                                                      \
    fixed16_t s, t, snext, tnext, sstep, tstep;                                \
    float sdivz, tdivz, zi, z, du, dv, spancountminus1;                        \
    float sdivz8stepu, tdivz8stepu, zi8stepu;                                  \
    sstep = 0; tstep = 0;                                                      \
    sdivz8stepu = d_sdivzstepu * 8;                                            \
    tdivz8stepu = d_tdivzstepu * 8;                                            \
    zi8stepu = d_zistepu * 8;                                                  \
    do {                                                                       \
        pdest = (unsigned char *)(d_viewbuffer + (screenwidth * pspan->v) + pspan->u); \
        count = pspan->count;                                                  \
        du = (float)pspan->u;                                                  \
        dv = (float)pspan->v;                                                  \
        sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;             \
        tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;             \
        zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;                         \
        z = (float)0x10000 / zi;                                               \
        s = (int)(sdivz * z) + sadjust;                                        \
        if (s > bbextents) s = bbextents; else if (s < 0) s = 0;               \
        t = (int)(tdivz * z) + tadjust;                                        \
        if (t > bbextentt) t = bbextentt; else if (t < 0) t = 0;               \
        do {                                                                   \
            spancount = count >= 8 ? 8 : count;                                \
            count -= spancount;                                                \
            if (count) {                                                       \
                sdivz += sdivz8stepu; tdivz += tdivz8stepu; zi += zi8stepu;    \
                z = (float)0x10000 / zi;                                       \
                snext = (int)(sdivz * z) + sadjust;                            \
                if (snext > bbextents) snext = bbextents; else if (snext < 8) snext = 8; \
                tnext = (int)(tdivz * z) + tadjust;                            \
                if (tnext > bbextentt) tnext = bbextentt; else if (tnext < 8) tnext = 8; \
                sstep = (snext - s) >> 3;                                      \
                tstep = (tnext - t) >> 3;                                      \
            } else {                                                           \
                spancountminus1 = (float)(spancount - 1);                      \
                sdivz += d_sdivzstepu * spancountminus1;                       \
                tdivz += d_tdivzstepu * spancountminus1;                       \
                zi += d_zistepu * spancountminus1;                             \
                z = (float)0x10000 / zi;                                       \
                snext = (int)(sdivz * z) + sadjust;                            \
                if (snext > bbextents) snext = bbextents; else if (snext < 8) snext = 8; \
                tnext = (int)(tdivz * z) + tadjust;                            \
                if (tnext > bbextentt) tnext = bbextentt; else if (tnext < 8) tnext = 8; \
                if (spancount > 1) {                                           \
                    sstep = (snext - s) / (spancount - 1);                     \
                    tstep = (tnext - t) / (spancount - 1);                     \
                }                                                              \
            }                                                                  \
            do {                                                               \
                *pdest++ = PIXEL(s, t);                                        \
                s += sstep; t += tstep;                                        \
            } while (--spancount > 0);                                         \
            s = snext; t = tnext;                                              \
        } while (count > 0);                                                   \
    } while ((pspan = pspan->pnext) != NULL);

static void DrawSpans_lit_flash(espan_t *pspan) { DRAWSPANS_BODY(LitPixel_flash) }
static void __not_in_flash_func(DrawSpans_lit_ram)(espan_t *pspan) { DRAWSPANS_BODY(LitPixel_ram) }
static void __not_in_flash_func(DrawSpans_lit_pow2)(espan_t *pspan) { DRAWSPANS_BODY(LitPixel_pow2) }
static void __not_in_flash_func(DrawSpans_cache)(espan_t *pspan) { DRAWSPANS_BODY(CachePixel) }

/* id's D_DrawZSpans (d_scan.c), C version. */
static void __not_in_flash_func(DrawZSpans)(espan_t *pspan) {
    int count, doublecount, izistep, izi;
    short *pdest;
    unsigned ltemp;
    float zi, du, dv;
    izistep = (int)(d_zistepu * 0x8000 * 0x10000);
    do {
        pdest = s_z + W * pspan->v + pspan->u;
        count = pspan->count;
        du = (float)pspan->u;
        dv = (float)pspan->v;
        zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
        izi = (int)(zi * 0x8000 * 0x10000);
        if ((uintptr_t)pdest & 0x02) { *pdest++ = (short)(izi >> 16); izi += izistep; count--; }
        if ((doublecount = count >> 1) > 0) {
            do {
                ltemp = izi >> 16; izi += izistep;
                ltemp |= izi & 0xFFFF0000; izi += izistep;
                *(int *)pdest = (int)ltemp;
                pdest += 2;
            } while (--doublecount > 0);
        }
        if (count & 1) *pdest = (short)(izi >> 16);
    } while ((pspan = pspan->pnext) != NULL);
}

/* One plane, yawed and pitched, so 1/z changes along rows and columns like
   a real wall or floor. Gradients follow d_scan.c's conventions. */
static void setup_scene(int tw, int th) {
    d_ziorigin    = 0.0025f;  d_zistepu = 0.0000040f;  d_zistepv = 0.0000150f;
    d_sdivzorigin = 0.10f;    d_sdivzstepu = 0.00110f; d_sdivzstepv = 0.00010f;
    d_tdivzorigin = 0.05f;    d_tdivzstepu = 0.00008f; d_tdivzstepv = 0.00085f;
    sadjust = 0; tadjust = 0;
    /* A surface 17x17 light cells (16 texels each) big, like a large face. */
    bbextents = (16 * 16 << 16) - 1;
    bbextentt = (16 * 16 << 16) - 1;
    lit_tw = tw; lit_th = th;
    lit_soff = 7; lit_toff = 3;
    lit_smax = 18;
    lit_lshift = 4;
    for (int v = 0; v < VIEW_H; v++) {
        s_spans[v].u = 0; s_spans[v].v = v; s_spans[v].count = W;
        s_spans[v].pnext = (v + 1 < VIEW_H) ? &s_spans[v + 1] : NULL;
    }
}

static uint32_t time_frames(void (*fn)(espan_t *)) {
    const uint64_t t0 = time_us_64();
    for (int i = 0; i < FRAMES; i++) fn(s_spans);
    return (uint32_t)((time_us_64() - t0) / FRAMES);
}

int main(void) {
    board_init();

    qog_data_status_t ds = qog_data_open();
    const uint8_t *flash_tex = NULL, *flash_pal = NULL;
    uint32_t tex_sz = 0, pal_sz = 0;
    if (ds == QOG_DATA_OK) {
        flash_tex = qog_data_find("testimg", &tex_sz);   /* 320x200 picture in flash */
        flash_pal = qog_data_find("testpal", &pal_sz);
    }
    const bool tex_in_flash = flash_tex && tex_sz == W * H;

    /* Light grid, colormap and the small textures: deterministic contents. */
    uint32_t seed = 12345;
    for (int i = 0; i < 18 * 18; i++) { seed = seed * 1103515245u + 12345u; blocklights[i] = (seed >> 8) & 0x3FFF; }
    for (int lvl = 0; lvl < 64; lvl++)
        for (int c = 0; c < 256; c++) s_colormap[lvl * 256 + c] = (uint8_t)((c * (63 - lvl)) / 63);
    for (int i = 0; i < 64 * 64; i++) { s_tex64[i] = (uint8_t)(i * 7); s_cache[i] = (uint8_t)(i * 3); }
    static uint8_t ram_tex[64 * 64];
    if (!tex_in_flash) memcpy(ram_tex, s_tex64, sizeof ram_tex);

    const qog_video_config_t vc = { W, H, 200 };
    const bool video_ok = qog_video_init(&vc);
    if (video_ok) {
        board_backlight(255);
        if (flash_pal && pal_sz == 768u) qog_video_set_palette_rgb(flash_pal);
        else {
            uint8_t pal[768];
            for (int i = 0; i < 256; i++) pal[3*i] = pal[3*i+1] = pal[3*i+2] = (uint8_t)i;
            qog_video_set_palette_rgb(pal);
        }
    }

    uint32_t r_lit_flash = 0, r_lit_ram = 0, r_lit_pow2 = 0, r_cache = 0, r_z = 0;
    uint32_t r_fdiv_ns = 0, r_xip_seq = 0, r_xip_rand = 0, rounds = 0;
    absolute_time_t next_report = make_timeout_time_ms(2000);
    absolute_time_t next_bench = get_absolute_time();

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        fwog_power_poll(now);   /* keeps the 6 s red-hold power off working */

        if (time_reached(next_bench)) {
            if (video_ok) qog_video_wait();   /* never draw into a frame being scanned */

            /* Today's path: texture from flash (or RAM if the data image has no picture). */
            setup_scene(tex_in_flash ? W : 64, tex_in_flash ? H : 64);
            lit_tex = tex_in_flash ? flash_tex : ram_tex;
            r_lit_flash = time_frames(DrawSpans_lit_flash);
            r_lit_ram   = time_frames(DrawSpans_lit_ram);

            setup_scene(64, 64);
            lit_tex = s_tex64;
            r_lit_pow2 = time_frames(DrawSpans_lit_pow2);
            r_cache    = time_frames(DrawSpans_cache);
            r_z        = time_frames(DrawZSpans);

            /* Leave the flash-textured lit frame on screen. */
            setup_scene(tex_in_flash ? W : 64, tex_in_flash ? H : 64);
            lit_tex = tex_in_flash ? flash_tex : ram_tex;
            DrawSpans_lit_flash(s_spans);
            memset(s_fb + W * VIEW_H, 0, W * (H - VIEW_H));

            /* One float divide, averaged. volatile keeps it from being folded. */
            volatile float acc = 1.0f, dv = 1.0001f;
            uint64_t t0 = time_us_64();
            for (int i = 0; i < 20000; i++) acc = (float)0x10000 / (acc + dv);
            r_fdiv_ns = (uint32_t)((time_us_64() - t0) * 1000u / 20000u);

            /* Flash reads: sequential copy, then scattered single bytes. */
            const uint8_t *flash = (const uint8_t *)0x10400000u;   /* data region */
            static uint8_t xbuf[4096];
            t0 = time_us_64();
            volatile uint32_t sum = 0;
            for (int k = 0; k < 16; k++) {
                memcpy(xbuf, flash + k * 4096u, sizeof xbuf);
                sum += xbuf[k * 255u];   /* read the copy, or the compiler drops it */
            }
            r_xip_seq = (uint32_t)(time_us_64() - t0);
            uint32_t idx = 1;
            t0 = time_us_64();
            for (int k = 0; k < 65536; k++) { idx = idx * 1664525u + 1013904223u; sum += flash[(idx >> 8) & 0xFFFFFu]; }
            r_xip_rand = (uint32_t)(time_us_64() - t0);

            rounds++;
            if (video_ok) qog_video_present(s_fb);
            next_bench = make_timeout_time_ms(500);
        }

        if (time_reached(next_report)) {
            next_report = make_timeout_time_ms(2000);
            DIAG("[bench] clk=%luMHz view=%dx%d tex=%s data=%s round=%lu | "
                 "lit_flash=%luus lit_ram=%luus lit_pow2=%luus surfcache=%luus zspans=%luus | "
                 "fdiv=%luns xip_seq64K=%luus xip_rand64K=%luus\n",
                 (unsigned long)(clock_get_hz(clk_sys) / 1000000u), W, VIEW_H,
                 tex_in_flash ? "flash320x200" : "ram64x64", qog_data_status_text(ds),
                 (unsigned long)rounds,
                 (unsigned long)r_lit_flash, (unsigned long)r_lit_ram, (unsigned long)r_lit_pow2,
                 (unsigned long)r_cache, (unsigned long)r_z,
                 (unsigned long)r_fdiv_ns, (unsigned long)r_xip_seq, (unsigned long)r_xip_rand);
        }
        sleep_ms(1);
    }
}
