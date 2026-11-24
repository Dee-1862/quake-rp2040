/* RP2040 sound output.
 *
 * QOG_SND_REMOTE (Agent W), the split game's client half on the display
 * CPU: the server half mixes (port/common/qog_snd.h) and this plays the
 * samples on the I2S amplifier (MAX98357, GPIO 4 DIN, 5 LRCLK, 6 BCLK) with
 * no work for the CPU between frames:
 *
 *  - PIO runs the BSP's audio_i2s program (bsp/display_cpu/audio/i2s_audio.c,
 *    from rpI2S.cpp), clocked for 11025 Hz with the BSP's integer divider
 *    (200 MHz / (64 x 283) = 11042 Hz, 0.16% fast, and with the Quake apps'
 *    250 MHz (Agent AB, clock_rp2040.c) 250 MHz / (64 x 354) = 11035 Hz,
 *    0.09% fast: id's soundtime follows the samples played, so nothing
 *    drifts). The divider is taken from clock_get_hz() at S_Init, after the
 *    clock is raised.
 *  - A DMA channel reads the ring of QOG_SND_RING signed 8-bit samples into
 *    the PIO's TX FIFO, one byte per frame. An 8-bit write to a FIFO register
 *    is replicated across the 32-bit word, so each byte b goes out as the
 *    16-bit sample b * 257 on both channels (the amplifier sums them).
 *  - A second DMA channel, chained from the first, writes the ring's address
 *    back to the first's READ_ADDR_TRIG, so the ring loops forever without an
 *    interrupt and without the ring having to be aligned to its size.
 *
 * The play position is the first channel's read address within the ring.
 * Which pass of the ring it is in comes from the timer, which runs from the
 * same crystal as the PIO clock, to the nearest pass (good to half a ring,
 * 93 ms). Samples arrive from the link (qog_snd_out_write) ahead of the
 * position and go into the ring. While the link waits, played samples that
 * nothing new has replaced are silenced, so a stalled server half leaves
 * silence, not the last 186 ms looping.
 *
 * Without QOG_SND_REMOTE: id's SNDDMA calls report no device and the engine
 * runs silent. */
#include "quakedef.h"

#ifdef QOG_SND_REMOTE

#include "qog_snd.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "fwog_display.h"

/* the BSP's audio_i2s program, transcribed unchanged from i2s_audio.c (which
   carries 4 KB of its own buffers, so it is not linked for these 16 bytes) */
static const uint16_t s_i2s_instr[8] = {
    0x6801u, /* 0: out    pins, 1         side 1 */
    0x1840u, /* 1: jmp    x--, 0          side 3 */
    0x6001u, /* 2: out    pins, 1         side 0 */
    0xf02eu, /* 3: set    x, 14           side 2 */
    0x6001u, /* 4: out    pins, 1         side 0 */
    0x1044u, /* 5: jmp    x--, 4          side 2 */
    0x6801u, /* 6: out    pins, 1         side 1 */
    0xf82eu, /* 7: set    x, 14           side 3 */
};
static const struct pio_program s_i2s_prog = { .instructions = s_i2s_instr, .length = 8, .origin = -1 };
#define I2S_X_INIT 14u   /* the bit counter the program expects on entry (the BSP's trap note) */

#define RING_MASK (QOG_SND_RING - 1)

static int8_t          s_ring[QOG_SND_RING] __attribute__((aligned(4)));
static const void     *s_ring_addr = s_ring;     /* the control channel's source */
static PIO             s_pio;
static int             s_sm = -1, s_offset;
static int             s_dma_data = -1, s_dma_ctrl = -1;
static uint64_t        s_t0_us;
static double          s_rate;                   /* frames per microsecond */
static unsigned        s_written;                /* the latest time delivered, plus one */
static unsigned        s_cleared;                /* times before this are silenced or replaced */
static int             s_on;

static int claim_pio(PIO pio) {
    if (!pio_can_add_program(pio, &s_i2s_prog)) return 0;
    int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) return 0;
    s_pio = pio;
    s_sm = sm;
    s_offset = pio_add_program(pio, &s_i2s_prog);
    return 1;
}

int qog_snd_out_init(void) {
    if (s_on) return 1;
    if (!claim_pio(pio0) && !claim_pio(pio1)) return 0;
    int a = dma_claim_unused_channel(false);
    int b = a >= 0 ? dma_claim_unused_channel(false) : -1;
    if (b < 0) {
        if (a >= 0) dma_channel_unclaim((uint)a);
        pio_remove_program(s_pio, &s_i2s_prog, (uint)s_offset);
        pio_sm_unclaim(s_pio, (uint)s_sm);
        return 0;
    }
    s_dma_data = a;
    s_dma_ctrl = b;

    /* the pins, as i2s_audio_init(): low outputs, then PIO's */
    gpio_init(PIN_I2S_LRCLK); gpio_put(PIN_I2S_LRCLK, 0); gpio_set_dir(PIN_I2S_LRCLK, GPIO_OUT);
    gpio_init(PIN_I2S_BCLK);  gpio_put(PIN_I2S_BCLK, 0);  gpio_set_dir(PIN_I2S_BCLK, GPIO_OUT);
    gpio_init(PIN_I2S_DIN);   gpio_put(PIN_I2S_DIN, 0);   gpio_set_dir(PIN_I2S_DIN, GPIO_OUT);
    pio_gpio_init(s_pio, PIN_I2S_LRCLK);
    pio_gpio_init(s_pio, PIN_I2S_BCLK);
    pio_gpio_init(s_pio, PIN_I2S_DIN);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, (uint)s_offset, (uint)s_offset + 7u);
    sm_config_set_sideset(&c, 2, false, false);
    sm_config_set_out_pins(&c, PIN_I2S_DIN, 1);
    sm_config_set_sideset_pins(&c, PIN_I2S_LRCLK);
    sm_config_set_out_shift(&c, false, true, 32);
    pio_sm_init(s_pio, (uint)s_sm, (uint)s_offset, &c);
    const uint pin_mask = (1u << PIN_I2S_DIN) | (3u << PIN_I2S_LRCLK);
    pio_sm_set_pindirs_with_mask(s_pio, (uint)s_sm, pin_mask, pin_mask);
    pio_sm_set_pins(s_pio, (uint)s_sm, 0);
    /* the BSP's integer divider (its trap 6): 64 PIO cycles a frame */
    uint32_t clk = clock_get_hz(clk_sys);
    uint32_t div = ((clk * 4u) / QOG_SND_RATE) >> 8u;
    pio_sm_set_clkdiv_int_frac(s_pio, (uint)s_sm, (uint16_t)div, 0);
    s_rate = (double)clk / (64.0 * div) / 1e6;

    memset(s_ring, 0, sizeof s_ring);
    volatile void *txf = (volatile void *)&s_pio->txf[s_sm];

    dma_channel_config cc = dma_channel_get_default_config((uint)s_dma_ctrl);
    channel_config_set_transfer_data_size(&cc, DMA_SIZE_32);
    channel_config_set_read_increment(&cc, false);
    channel_config_set_write_increment(&cc, false);
    dma_channel_configure((uint)s_dma_ctrl, &cc, &dma_hw->ch[s_dma_data].al3_read_addr_trig,
                          &s_ring_addr, 1, false);

    dma_channel_config dc = dma_channel_get_default_config((uint)s_dma_data);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(s_pio, (uint)s_sm, true));
    channel_config_set_chain_to(&dc, (uint)s_dma_ctrl);
    dma_channel_configure((uint)s_dma_data, &dc, txf, s_ring, QOG_SND_RING, true);

    /* start frame-aligned: restart, the bit counter, the entry (the BSP's trap 2 note) */
    pio_sm_restart(s_pio, (uint)s_sm);
    pio_sm_exec(s_pio, (uint)s_sm, pio_encode_set(pio_x, I2S_X_INIT));
    pio_sm_exec(s_pio, (uint)s_sm, pio_encode_jmp((uint)s_offset));
    s_t0_us = time_us_64();
    pio_sm_set_enabled(s_pio, (uint)s_sm, true);
    s_written = s_cleared = 0;
    s_on = 1;
    return 1;
}

/* stop the bit clock: the amplifier has no shutdown line, and a stopped
   state machine is the only true silence (the BSP's trap 2) */
void qog_snd_out_shutdown(void) {
    if (!s_on) return;
    s_on = 0;
    pio_sm_set_enabled(s_pio, (uint)s_sm, false);
    hw_clear_bits(&dma_hw->ch[s_dma_data].al1_ctrl, DMA_CH0_CTRL_TRIG_CHAIN_TO_BITS);
    dma_channel_abort((uint)s_dma_data);
    dma_channel_abort((uint)s_dma_ctrl);
    pio_sm_clear_fifos(s_pio, (uint)s_sm);
}

unsigned qog_snd_out_samples(void) {
    if (!s_on) return 0;
    uint32_t off = (uint32_t)((uintptr_t)dma_hw->ch[s_dma_data].read_addr - (uintptr_t)s_ring);
    if (off >= QOG_SND_RING) off = 0;        /* between a pass and its restart */
    double est = (double)(time_us_64() - s_t0_us) * s_rate;
    int64_t pass = (int64_t)((est - off) / QOG_SND_RING + 0.5);
    if (pass < 0) pass = 0;
    return (unsigned)(pass * QOG_SND_RING + off);
}

void qog_snd_out_submitted(void) {}

void qog_snd_out_write(unsigned t, const unsigned char *pcm, int n) {
    unsigned now = qog_snd_out_samples();
    for (int i = 0; i < n; i++) {
        unsigned tt = t + (unsigned)i;
        int ahead = (int)(tt - now);
        if (ahead < 0 || ahead >= QOG_SND_RING) continue;   /* played already, or past the ring */
        s_ring[tt & RING_MASK] = (int8_t)(pcm[i] ^ 0x80);  /* id's unsigned 8-bit to signed */
    }
    if ((int)(t + (unsigned)n - s_written) > 0) s_written = t + (unsigned)n;
}

void qog_snd_out_clear(void) {
    if (s_on) memset(s_ring, 0, sizeof s_ring);
}

void qog_snd_out_idle(void) {
    if (!s_on) return;
    unsigned now = qog_snd_out_samples();
    if ((int)(now - s_cleared) > QOG_SND_RING) s_cleared = now - QOG_SND_RING;
    for (; (int)(now - s_cleared) > 0; s_cleared++)
        if ((int)(s_written - (s_cleared + QOG_SND_RING)) <= 0)   /* its next use has not come */
            s_ring[s_cleared & RING_MASK] = 0;
}

#else

qboolean SNDDMA_Init(void) { return false; }
int SNDDMA_GetDMAPos(void) { return 0; }
void SNDDMA_Submit(void) {}
void SNDDMA_Shutdown(void) {}

#endif
