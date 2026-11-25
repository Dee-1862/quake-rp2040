#include "qog_video.h"
#include <string.h>

void qog_video_build_map(uint16_t *map, uint16_t out_n, uint16_t src_n) {
    /* Sample at pixel centres: src = floor((o + 0.5) * src_n / out_n). */
    for (uint32_t o = 0; o < out_n; o++)
        map[o] = (uint16_t)(((2u * o + 1u) * src_n) / (2u * out_n));
}

uint32_t qog_video_pio_bit_cycles(uint32_t clk_hz, uint32_t max_hz, uint32_t *hi, uint32_t *lo) {
    /* the fewest whole system clocks a bit with clk_hz / n <= max_hz */
    uint32_t n = max_hz ? (clk_hz + max_hz - 1u) / max_hz : 32u;
    if (n < 2u) n = 2u;
    if (n > 32u) n = 32u;   /* two instructions of at most 16 cycles each */
    if (hi) *hi = n / 2u;
    if (lo) *lo = n - n / 2u;
    return n;
}

#ifndef HOST_TEST

#include "fwog_display.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#define ST7789_RAMWR 0x2Cu

static qog_video_config_t s_cfg;
static uint16_t s_y0;                       /* first panel line */
static uint16_t s_xmap[QOG_LCD_W];
static bool     s_x_identity;
static uint16_t s_line[2][QOG_LCD_W] __attribute__((aligned(4)));   /* Agent AO: word stores (convert_line_double) */
static int      s_dma = -1;

static uint16_t s_pal[256];
static uint16_t s_pal_pending[256];
static volatile bool s_pal_dirty;

static volatile bool     s_busy;
/* QuakeOG (Agent AK): source rows the scanout in flight has read so far (it
   reads each row once, into a line buffer), and how many it sends */
static volatile uint32_t s_scan_row;
static uint32_t          s_scan_rows;
static uint32_t          s_scan_packed;   /* Agent AO: its first rows hold src_w / 2 pixels each, sent doubled */
static volatile uint32_t s_frames;
static volatile uint32_t s_last_scan_us;
static uint32_t          s_last_wait_us;
static void (*volatile s_hook)(void);
static uint32_t          s_last_scan_rows;   /* Agent AP */
/* Agent AP: characters over the packed rows (qog_video_set_text), the next
   present's and the scanout's in flight */
static const uint32_t   *s_text_next, *s_scan_text;
static const uint8_t    *s_chars_next, *s_scan_chars;
static uint32_t          s_text_next_n, s_scan_text_n;
static const uint8_t    *s_font;

/* QuakeOG (Agent AP): the pixel stream through PIO (qog_video_use_pio).
   Two instructions, side-set 1 bit on SCK, autopull 16 bits MSB first:
       out pins, 1   side 0 [lo - 1]    data out, SCK low
       nop           side 1 [hi - 1]    SCK high, the panel samples
   An empty FIFO stalls the out with SCK low (SPI mode 0 idle). A 16-bit
   DMA write to the TX FIFO lands in both halves of the word, and the left
   shift sends the upper copy. No gap between pixels or lines, where the
   PL022 in mode 0 leaves about 1.5 bit times between frames (Agent V
   measured 22.4 ms a scanout against 20.5 ms of wire at 50 MHz). */
static uint32_t s_pio_want_khz;     /* 0: PL022 only */
static PIO      s_pio;
static int      s_pio_sm = -1;
static uint     s_pio_offset;
static uint16_t s_pio_instr[2];
static struct pio_program s_pio_prog = { .instructions = s_pio_instr, .length = 2, .origin = -1 };
static uint32_t s_pio_hz;

/* ---- core 1 ---------------------------------------------------------- */

/* Hot loops live in RAM so core 1 never contends with core 0 for XIP while
   the renderer is streaming textures out of flash. QuakeOG (Agent AP): in
   the board build the compiler inlines them into scanout and scanout into
   core1_main, which is in flash, so the loop runs from flash and its few
   cache lines stay in the XIP cache. Keeping them in RAM with
   __no_inline_not_in_flash_func costs about 450 bytes of RAM. */
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

/* QuakeOG (Agent AO): a packed Turbo row, src_w / 2 pixels, each sent twice
   (platform/qog_video.h, qog_video_present_packed). Two pixels a word. */
static void __not_in_flash_func(convert_line_double)(uint16_t *dst, const uint8_t *src) {
    const uint16_t *pal = s_pal;
    const uint32_t *s4 = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)dst;
    for (uint32_t i = 0; i < QOG_LCD_W / 8u; i++) {
        const uint32_t w = s4[i];
        d[0] = pal[w & 0xFFu] * 0x10001u;
        d[1] = pal[(w >> 8) & 0xFFu] * 0x10001u;
        d[2] = pal[(w >> 16) & 0xFFu] * 0x10001u;
        d[3] = pal[w >> 24] * 0x10001u;
        d += 4;
    }
}

/* QuakeOG (Agent AP): the listed characters that cross source row sy, over
   its converted line, as Draw_Character draws them over a spread view */
static void text_line(uint16_t *dst, uint32_t sy) {
    const uint16_t *pal = s_pal;
    const uint32_t *t = s_scan_text;
    for (uint32_t i = 0, n = s_scan_text_n; i < n; i++) {
        const uint32_t e = t[i], r = sy - ((e >> 9) & 255u);
        if (r >= 8u) continue;
        const uint8_t *ch = s_scan_chars + (e >> 23);
        uint16_t *d = dst + (e & 511u);
        for (uint32_t k = 0, len = (e >> 17) & 63u; k < len; k++, d += 8) {
            const uint32_t num = ch[k];
            const uint8_t *src = s_font + ((num >> 4) << 10) + ((num & 15u) << 3) + (r << 7);
            for (uint32_t c = 0; c < 8u; c++)
                if (src[c]) d[c] = pal[src[c]];
        }
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
    /* QuakeOG (Agent AK): only the first s_scan_rows lines when the rows
       below them are already on the panel (qog_video_present_rows) */
    const uint32_t out_h = s_scan_rows;
    st7789_set_window(0u, s_y0, QOG_LCD_W, out_h);

    /* RAMWR as an 8-bit command, then switch the PL022 to 16-bit frames for
       the pixel stream so RGB565 goes out MSB-first with no byte swap. */
    const uint8_t ramwr = ST7789_RAMWR;
    gpio_put(PIN_LCD_CS, 0);
    gpio_put(PIN_LCD_DC, 0);
    spi_write_blocking(FWOG_LCD_SPI, &ramwr, 1);
    spi_drain();
    gpio_put(PIN_LCD_DC, 1);

    dma_channel_config c = dma_channel_get_default_config((uint)s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    volatile void *dr;
    const bool pio = s_pio_sm >= 0;
    if (pio) {
        /* Agent AP: SCK and MOSI to the state machine (both idle low) */
        const gpio_function_t f = pio_get_index(s_pio) ? GPIO_FUNC_PIO1 : GPIO_FUNC_PIO0;
        gpio_set_function(PIN_LCD_SCK, f);
        gpio_set_function(PIN_LCD_MOSI, f);
        channel_config_set_dreq(&c, pio_get_dreq(s_pio, (uint)s_pio_sm, true));
        dr = &s_pio->txf[s_pio_sm];
    } else {
        spi_set_format(FWOG_LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
        channel_config_set_dreq(&c, spi_get_dreq(FWOG_LCD_SPI, true));
        dr = &spi_get_hw(FWOG_LCD_SPI)->dr;
    }

    const uint32_t src_h = s_cfg.src_h, src_w = s_cfg.src_w, map_h = s_cfg.out_h;
    int last_sy = -1;
    unsigned cur = 0;
    for (uint32_t oy = 0; oy < out_h; oy++) {
        const int sy = (int)(((2u * oy + 1u) * src_h) / (2u * map_h));
        if (sy != last_sy) {
            /* New source line: build it in the free buffer. The other one
               may still be on the wire. */
            cur ^= 1u;
            if ((uint32_t)sy < s_scan_packed) {   /* Agent AO: a Turbo view's row, packed */
                convert_line_double(s_line[cur], fb + (uint32_t)sy * (src_w / 2u));
                if (s_scan_text_n) text_line(s_line[cur], (uint32_t)sy);   /* Agent AP */
            } else
                convert_line(s_line[cur], fb + (uint32_t)sy * src_w);
            last_sy = sy;
            __dmb();
            s_scan_row = (uint32_t)sy + 1u;   /* Agent AK: the game may draw over rows above this */
        }
        /* A repeated line (vertical scale-up) resends the same buffer, which
           is safe: DMA only reads it. */
        if (oy) dma_wait_with_hook();
        dma_channel_configure((uint)s_dma, &c, dr, s_line[cur], QOG_LCD_W, true);
    }
    dma_channel_wait_for_finish_blocking((uint)s_dma);
    if (pio) {
        /* Agent AP: the last bits have left the shift register once the
           state machine stalls on the empty FIFO (TXSTALL is set again in
           every cycle it waits, so clearing it first cannot miss the end).
           Then the pins go back to the PL022 for the BSP's commands. */
        const uint32_t stall = 1u << (PIO_FDEBUG_TXSTALL_LSB + (uint)s_pio_sm);
        while (!pio_sm_is_tx_fifo_empty(s_pio, (uint)s_pio_sm)) tight_loop_contents();
        s_pio->fdebug = stall;
        while (!(s_pio->fdebug & stall)) tight_loop_contents();
        gpio_put(PIN_LCD_CS, 1);
        gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
        gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
    } else {
        spi_drain();
        gpio_put(PIN_LCD_CS, 1);
        spi_set_format(FWOG_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    }

    s_last_scan_rows = out_h;
    s_last_scan_us = (uint32_t)(time_us_64() - t0);
}

/* Frame handoff. NOT the SIO FIFO: multicore_lockout_victim_init() installs
   a core-1 FIFO IRQ that pops and discards anything that is not a lockout
   request, so frame pointers pushed through the FIFO vanish (found on the
   board, docs/lessons.md L14). */
static const uint8_t *volatile s_next_fb;

/* QuakeOG (Agent V): one job core 0 hands to core 1 (qog_video_core1_start),
   run between scanouts. */
static void (*volatile s_job_fn)(void *);
static void *volatile s_job_arg;
static volatile bool s_job_done = true;
/* QuakeOG (Agent AM): core 1's time in scanouts and in jobs, us since boot
   (wrap after 71 minutes, the reader takes differences) */
static volatile uint32_t s_c1_scan_us, s_c1_job_us;

static void core1_main(void) {
    /* Lets core 0 pause us safely while it erases/programs flash (saves). */
    multicore_lockout_victim_init();
    for (;;) {
        const uint8_t *fb;
        while ((fb = s_next_fb) == NULL) {
            void (*job)(void *) = s_job_fn;
            if (job) {
                const uint32_t j0 = time_us_32();
                __dmb();
                s_job_fn = NULL;
                job(s_job_arg);
                s_c1_job_us = s_c1_job_us + (time_us_32() - j0);
                __dmb();
                s_job_done = true;
                __sev();
                continue;
            }
            void (*hook)(void) = s_hook;
            if (hook) hook();
            else __wfe();
        }
        s_next_fb = NULL;
        scanout(fb);
        s_c1_scan_us = s_c1_scan_us + s_last_scan_us;
        s_frames = s_frames + 1u;
        __dmb();
        s_busy = false;
        __sev();
    }
}

/* ---- core 0 ---------------------------------------------------------- */

/* QuakeOG (Agent AP): claim a state machine and load the stream program,
   pio1 first (pio0 holds the WS2812 bar's SM 0, and the speaker asks pio0
   first, sys_rp2040.c and snd_rp2040.c). Called once before core 1 starts,
   after the panel init, so the BSP's init and clear ran on the PL022. */
static bool pio_claim(PIO pio) {
    if (!pio_can_add_program(pio, &s_pio_prog)) return false;
    const int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) return false;
    s_pio = pio;
    s_pio_sm = sm;
    s_pio_offset = (uint)pio_add_program(pio, &s_pio_prog);
    return true;
}

static void pio_start(void) {
    const uint32_t clk = (uint32_t)clock_get_hz(clk_sys);
    uint32_t hi, lo;
    const uint32_t n = qog_video_pio_bit_cycles(clk, s_pio_want_khz * 1000u, &hi, &lo);
    s_pio_instr[0] = (uint16_t)(pio_encode_out(pio_pins, 1) | pio_encode_sideset(1, 0) | pio_encode_delay(lo - 1u));
    s_pio_instr[1] = (uint16_t)(pio_encode_nop() | pio_encode_sideset(1, 1) | pio_encode_delay(hi - 1u));
    if (!pio_claim(pio1) && !pio_claim(pio0)) {
        DIAG("[qog_video] lcd no free PIO state machine, staying on the PL022\n");
        return;
    }
    const uint sm = (uint)s_pio_sm;
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, s_pio_offset, s_pio_offset + 1u);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, PIN_LCD_SCK);
    sm_config_set_out_pins(&c, PIN_LCD_MOSI, 1);
    sm_config_set_out_shift(&c, false, true, 16);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);   /* 8 pixels of slack between line DMAs */
    sm_config_set_clkdiv_int_frac(&c, 1, 0);
    pio_sm_init(s_pio, sm, s_pio_offset, &c);
    const uint32_t mask = (1u << PIN_LCD_SCK) | (1u << PIN_LCD_MOSI);
    pio_sm_set_pins_with_mask(s_pio, sm, 0u, mask);
    pio_sm_set_pindirs_with_mask(s_pio, sm, mask, mask);
    /* the state machine drives the pins only during a scanout (their gpio
       function), the PL022 the rest of the time. Faster edges keep the
       shorter clock pulse whole */
    gpio_set_slew_rate(PIN_LCD_SCK, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PIN_LCD_MOSI, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(PIN_LCD_SCK, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PIN_LCD_MOSI, GPIO_DRIVE_STRENGTH_8MA);
    pio_sm_set_enabled(s_pio, sm, true);
    s_pio_hz = clk / n;
    DIAG("[qog_video] lcd pio%u sm %u, %u kHz (clk_sys %u kHz / %u, high %u low %u, limit %u kHz), commands on the PL022\n",
         (unsigned)pio_get_index(s_pio), sm, (unsigned)(s_pio_hz / 1000u), (unsigned)(clk / 1000u),
         (unsigned)n, (unsigned)hi, (unsigned)lo, (unsigned)s_pio_want_khz);
}

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
    if (s_pio_want_khz) pio_start();
    multicore_launch_core1(core1_main);
    DIAG("[qog_video] %ux%u -> 320x%u at y=%u, dma ch %d\n",
         cfg->src_w, cfg->src_h, cfg->out_h, s_y0, s_dma);
    return true;
}

void qog_video_use_pio(uint32_t max_khz) { s_pio_want_khz = max_khz; }

void qog_video_set_text(const uint32_t *runs, uint32_t nruns, const uint8_t *chars, const uint8_t *font) {
    s_text_next = runs;
    s_chars_next = chars;
    s_text_next_n = nruns;
    s_font = font;
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

void qog_video_wait_rows(uint32_t rows) {
    const uint64_t t0 = time_us_64();
    while (s_busy && s_scan_row < rows) tight_loop_contents();
    __dmb();
    s_last_wait_us = (uint32_t)(time_us_64() - t0);
}

void qog_video_wait(void) {
    const uint64_t t0 = time_us_64();
    while (s_busy) __wfe();
    s_last_wait_us = (uint32_t)(time_us_64() - t0);
}

void qog_video_present(const uint8_t *fb) {
    qog_video_present_rows(fb, s_cfg.out_h);
}

void qog_video_present_rows(const uint8_t *fb, uint32_t rows) {
    qog_video_present_packed(fb, rows, 0);
}

void qog_video_present_packed(const uint8_t *fb, uint32_t rows, uint32_t packed) {
    qog_video_wait();
    /* Agent AK: a part only when lines and rows are one to one */
    s_scan_rows = (rows && rows < s_cfg.out_h && s_cfg.src_h == s_cfg.out_h) ? rows : s_cfg.out_h;
    /* Agent AO: packed rows only for a 320 wide frame, rows one to one */
    s_scan_packed = (s_x_identity && s_cfg.src_h == s_cfg.out_h && packed <= s_cfg.src_h) ? packed : 0u;
    /* Agent AP: the characters only over packed rows */
    s_scan_text = s_text_next;
    s_scan_chars = s_chars_next;
    s_scan_text_n = (s_scan_packed && s_font) ? s_text_next_n : 0u;
    s_text_next_n = 0;
    s_scan_row = 0;
    s_busy = true;
    __dmb();
    s_next_fb = fb;
    __sev();
}

void qog_video_set_core1_hook(void (*fn)(void)) { s_hook = fn; }

void qog_video_core1_start(void (*fn)(void *), void *arg) {
    s_job_arg = arg;
    s_job_done = false;
    __dmb();
    s_job_fn = fn;
    __sev();
}

void qog_video_core1_join(void) {
    while (!s_job_done) __wfe();
    __dmb();
}

void qog_video_core1_times(uint32_t *scan_us, uint32_t *job_us) {
    *scan_us = s_c1_scan_us;
    *job_us = s_c1_job_us;
}

qog_video_stats_t qog_video_stats(void) {
    qog_video_stats_t st = { s_frames, s_last_scan_us, s_last_wait_us, s_last_scan_rows };
    return st;
}

#endif /* HOST_TEST */
