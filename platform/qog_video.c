#include "qog_video.h"
#include <string.h>

void qog_video_build_map(uint16_t *map, uint16_t out_n, uint16_t src_n) {
    /* Sample at pixel centres: src = floor((o + 0.5) * src_n / out_n). */
    for (uint32_t o = 0; o < out_n; o++)
        map[o] = (uint16_t)(((2u * o + 1u) * src_n) / (2u * out_n));
}

#ifndef HOST_TEST

#include "fwog_display.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#define ST7789_RAMWR 0x2Cu

static qog_video_config_t s_cfg;
static uint16_t s_y0;                       /* first panel line */
static uint16_t s_xmap[QOG_LCD_W];
static bool     s_x_identity;
static uint16_t s_line[2][QOG_LCD_W];
static int      s_dma = -1;

static uint16_t s_pal[256];
static uint16_t s_pal_pending[256];
static volatile bool s_pal_dirty;

static volatile bool     s_busy;
static volatile uint32_t s_frames;
static volatile uint32_t s_last_scan_us;
static uint32_t          s_last_wait_us;
static void (*volatile s_hook)(void);

/* ---- core 1 ---------------------------------------------------------- */

/* Hot loops live in RAM so core 1 never contends with core 0 for XIP while
   the renderer is streaming textures out of flash. */
static void __not_in_flash_func(convert_line)(uint16_t *dst, const uint8_t *src) {
    const uint16_t *pal = s_pal;
    if (s_x_identity) {
        /* 320 wide: four pixels per word read. */
        const uint32_t *s4 = (const uint32_t *)src;
        for (uint32_t i = 0; i < QOG_LCD_W / 4u; i++) {
            const uint32_t w = s4[i];
            dst[0] = pal[w & 0xFFu];
            dst[1] = pal[(w >> 8) & 0xFFu];
            dst[2] = pal[(w >> 16) & 0xFFu];
            dst[3] = pal[w >> 24];
            dst += 4;
        }
    } else {
        const uint16_t *xm = s_xmap;
        for (uint32_t i = 0; i < QOG_LCD_W; i++) dst[i] = pal[src[xm[i]]];
    }
}

static inline void spi_drain(void) {
    spi_hw_t *hw = spi_get_hw(FWOG_LCD_SPI);
    while (hw->sr & SPI_SSPSR_BSY_BITS) tight_loop_contents();
    while (hw->sr & SPI_SSPSR_RNE_BITS) (void)hw->dr;
    hw->icr = SPI_SSPICR_RORIC_BITS;   /* we never read RX; clear the overrun */
}

static inline void dma_wait_with_hook(void) {
    void (*hook)(void) = s_hook;
    if (hook && dma_channel_is_busy((uint)s_dma)) hook();
    dma_channel_wait_for_finish_blocking((uint)s_dma);
}

static void __not_in_flash_func(scanout)(const uint8_t *fb) {
    const uint64_t t0 = time_us_64();
    if (s_pal_dirty) {
        memcpy(s_pal, s_pal_pending, sizeof s_pal);
        s_pal_dirty = false;
    }

    st7789_dma_wait();   /* the BSP's own fill DMA, if any */
    st7789_set_window(0u, s_y0, QOG_LCD_W, s_cfg.out_h);

    /* RAMWR as an 8-bit command, then switch the PL022 to 16-bit frames for
       the pixel stream so RGB565 goes out MSB-first with no byte swap. */
    const uint8_t ramwr = ST7789_RAMWR;
    gpio_put(PIN_LCD_CS, 0);
    gpio_put(PIN_LCD_DC, 0);
    spi_write_blocking(FWOG_LCD_SPI, &ramwr, 1);
    spi_drain();
    gpio_put(PIN_LCD_DC, 1);
    spi_set_format(FWOG_LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    dma_channel_config c = dma_channel_get_default_config((uint)s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(FWOG_LCD_SPI, true));
    volatile void *dr = &spi_get_hw(FWOG_LCD_SPI)->dr;

    const uint32_t out_h = s_cfg.out_h, src_h = s_cfg.src_h, src_w = s_cfg.src_w;
    int last_sy = -1;
    unsigned cur = 0;
    for (uint32_t oy = 0; oy < out_h; oy++) {
        const int sy = (int)(((2u * oy + 1u) * src_h) / (2u * out_h));
        if (sy != last_sy) {
            /* New source line: build it in the free buffer. The other one
               may still be on the wire. */
            cur ^= 1u;
            convert_line(s_line[cur], fb + (uint32_t)sy * src_w);
            last_sy = sy;
        }
        /* A repeated line (vertical scale-up) resends the same buffer, which
           is safe: DMA only reads it. */
        if (oy) dma_wait_with_hook();
        dma_channel_configure((uint)s_dma, &c, dr, s_line[cur], QOG_LCD_W, true);
    }
    dma_channel_wait_for_finish_blocking((uint)s_dma);
    spi_drain();
    gpio_put(PIN_LCD_CS, 1);
    spi_set_format(FWOG_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    s_last_scan_us = (uint32_t)(time_us_64() - t0);
}

/* Frame handoff. NOT the SIO FIFO: multicore_lockout_victim_init() installs
   a core-1 FIFO IRQ that pops and discards anything that is not a lockout
   request, so frame pointers pushed through the FIFO vanish (found on the
   board, docs/lessons.md L14). */
static const uint8_t *volatile s_next_fb;

static void core1_main(void) {
    /* Lets core 0 pause us safely while it erases/programs flash (saves). */
    multicore_lockout_victim_init();
    for (;;) {
        const uint8_t *fb;
        while ((fb = s_next_fb) == NULL) {
            void (*hook)(void) = s_hook;
            if (hook) hook();
            else __wfe();
        }
        s_next_fb = NULL;
        scanout(fb);
        s_frames = s_frames + 1u;
        __dmb();
        s_busy = false;
        __sev();
    }
}

/* ---- core 0 ---------------------------------------------------------- */

bool qog_video_init(const qog_video_config_t *cfg) {
    if (!cfg || cfg->src_w == 0u || cfg->src_w > QOG_LCD_W || cfg->src_h == 0u ||
        cfg->out_h == 0u || cfg->out_h > QOG_LCD_H)
        return false;
    s_cfg = *cfg;
    s_y0 = (uint16_t)((QOG_LCD_H - cfg->out_h) / 2u);
    s_x_identity = (cfg->src_w == QOG_LCD_W);
    qog_video_build_map(s_xmap, QOG_LCD_W, cfg->src_w);

    /* Grey ramp until the game sets a palette, so a first frame is visible. */
    for (unsigned i = 0; i < 256u; i++) s_pal[i] = st7789_rgb565((uint8_t)i, (uint8_t)i, (uint8_t)i);

    st7789_init_begin();
    const absolute_time_t deadline = make_timeout_time_ms(1000);
    while (!st7789_ready() && !time_reached(deadline)) {
        st7789_init_step();
        sleep_ms(1);
    }
    if (!st7789_ready()) {
        DIAG("[qog_video] panel never became ready\n");
        return false;
    }
    st7789_clear(0x0000);
    st7789_dma_wait();
    /* The panel driver leaves the backlight alone; without this the panel
       is drawn perfectly and stays dark (docs/lessons.md L18). */
    board_backlight(255);

    s_dma = dma_claim_unused_channel(false);
    if (s_dma < 0) {
        DIAG("[qog_video] no free DMA channel\n");
        return false;
    }
    multicore_launch_core1(core1_main);
    DIAG("[qog_video] %ux%u -> 320x%u at y=%u, dma ch %d\n",
         cfg->src_w, cfg->src_h, cfg->out_h, s_y0, s_dma);
    return true;
}

void qog_video_set_palette565(const uint16_t pal[256]) {
    memcpy(s_pal_pending, pal, sizeof s_pal_pending);
    __dmb();
    s_pal_dirty = true;
}

void qog_video_set_palette_rgb(const uint8_t rgb[768]) {
    for (unsigned i = 0; i < 256u; i++)
        s_pal_pending[i] = st7789_rgb565(rgb[3u * i], rgb[3u * i + 1u], rgb[3u * i + 2u]);
    __dmb();
    s_pal_dirty = true;
}

bool qog_video_busy(void) { return s_busy; }

void qog_video_wait(void) {
    const uint64_t t0 = time_us_64();
    while (s_busy) __wfe();
    s_last_wait_us = (uint32_t)(time_us_64() - t0);
}

void qog_video_present(const uint8_t *fb) {
    qog_video_wait();
    s_busy = true;
    __dmb();
    s_next_fb = fb;
    __sev();
}

void qog_video_set_core1_hook(void (*fn)(void)) { s_hook = fn; }

qog_video_stats_t qog_video_stats(void) {
    qog_video_stats_t st = { s_frames, s_last_scan_us, s_last_wait_us };
    return st;
}

#endif /* HOST_TEST */
