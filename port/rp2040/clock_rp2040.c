/* QuakeOG, the Quake apps' faster clocks, set at run time.
 *
 * Why here and not in the BSP, the BSP's 200 MHz and flash CLKDIV 4 are also
 * the display bootloader's, and the display CPU has no BOOTSEL button, so a
 * bootloader that failed at a faster setting could never be reflashed. The
 * bootloader and every other app keep the BSP's settings. Only quake_display
 * and quake_main call qog_clock_boost(), right after board_init().
 *
 * 1. Voltage, then clock (QOG_SYS_KHZ 300000 at QOG_SYS_MV 1250 since earlier work
 *    , 250000 at 1200 before). The core voltage goes up first and settles
 *    for 10 ms as the BSP's fwog_clocks_init does, then set_sys_clock_pll
 *    (300 MHz is VCO 1500 MHz over 5 and 1). clk_peri follows clk_sys
 *    (PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK in the board header), so
 *    the link UART stays exactly 12.5 Mbaud (300 / 16 / 12.5 = 1.5, IBRD 1
 *    FBRD 32) and the LCD SPI drops to 50 MHz (300 / 6, the panel's 62.5
 *    would need 300 / 4.8). clk_ref, the 1 MHz timer tick, PLL_USB, USB and
 *    the ADC do not change. docs/changes.md has the whole table.
 *
 * 2. What board_init() already derived from 200 MHz is derived again.
 *    display  the backlight PWM divider (board.c, 10 kHz with wrap 255) and
 *             the I2C1 rate (charger, RTC, accelerometer, expander)
 *    main     the FPGA clock on GPOUT1 (fpga_clk.c, at most 25 MHz). Its
 *             divider is raised for the new clock before the switch, so the
 *             gateware never sees more than 25 MHz, then set again after.
 *    Everything else (link UART, LCD SPI, the speaker's PIO, the WS2812 PIO)
 *    is started later and takes its divider from clock_get_hz() then.
 *
 * 3. The flash divider. The display app runs after the bootloader, whose XIP
 *    setup stays in force (the app's boot2 is skipped), and main's flash
 *    writes run boot2 again, so the divider is switched here at run time on
 *    both chips, from RAM, interrupts off, core 1 not started yet. The
 *    W25Q128JV reads quad I/O (0xEB, continuous mode, 4 dummy clocks) at up
 *    to 133 MHz. clk_sys / 2 is tried when it is at most QOG_FLASH_MAX_KHZ
 *    (150 MHz by default, so 300 MHz tries 150 MHz, 13% above the rating.
 *    133000 keeps the part's rating and gives div 4 at 300 MHz).
 *    Earlier work, every RX sample delay from 1 to QOG_FLASH_DLY_MAX (3) is
 *    checked, and the middle of the run of delays that pass is kept, so the
 *    sample point sits away from both edges of the data eye (on the board at
 *    125 MHz main once needed delay 2 where the display took 1).
 *    Check, the app's own image (up to QOG_FLASH_CHECK_KB) and 16 blocks of
 *    4 KB spread over the data region are read in 16 KB chunks through the
 *    uncached alias, each chunk first at the old divider with interrupts on,
 *    then twice at every candidate setting with interrupts off, and the old
 *    divider and delay go back before the RAM routine returns. Only when the
 *    whole check is done is the chosen setting written for good.
 *    Afterwards one 4 KB block of the app is read again every QOG_TEMP_S
 *    seconds at the live setting and compared with its hash at the old
 *    divider. Three differences in a row put the old divider back (core 1
 *    locked out on the display), and the DIAG line says so.
 *
 * 4. Every QOG_TEMP_S seconds a DIAG line gives the die temperature (ADC
 *    channel 4), the clock, the flash setting and the recheck counts.
 *
 * Not run on hardware at 300 MHz yet. */
#include <stdint.h>
#include "qog_clock.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/ssi.h"
#include "hardware/regs/addressmap.h"
#include "common/diag.h"
#include "qog_flashmap.h"
#ifdef QOG_SYS_MAIN
#include "fwog_main.h"
#include "fpga/fpga_clk.h"
#else
#include "fwog_display.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "pico/multicore.h"
#endif
#ifdef QOG_PROF
#include "qog_prof.h"
#endif

#ifndef QOG_SYS_KHZ
#define QOG_SYS_KHZ 0
#endif
#ifndef QOG_SYS_MV
#define QOG_SYS_MV 1250
#endif
#ifndef QOG_FLASH_FAST
#define QOG_FLASH_FAST 0
#endif
#ifndef QOG_FLASH_MAX_KHZ
#define QOG_FLASH_MAX_KHZ 150000
#endif
#ifndef QOG_FLASH_CHECK_KB
#define QOG_FLASH_CHECK_KB 1024
#endif
#ifndef QOG_FLASH_DLY_MAX
#define QOG_FLASH_DLY_MAX 3
#endif
#ifndef QOG_TEMP_S
#define QOG_TEMP_S 5
#endif

#define CHUNK_WORDS    4096u       /* 16 KB a check step, about 2 ms with interrupts off */
#define DATA_BLOCKS    16u         /* 4 KB samples spread over the data region */
#define DATA_WORDS     1024u
#define RECHECK_BLOCKS 16u         /* 4 KB app blocks read again at run time */
#define RECHECK_WORDS  1024u

#if QOG_SYS_KHZ && (QOG_SYS_MV < 1150 || QOG_SYS_MV > 1300)
#error "QOG_SYS_MV must be 1150 to 1300"
#endif
#if QOG_FLASH_DLY_MAX < 1 || QOG_FLASH_DLY_MAX > 6
#error "QOG_FLASH_DLY_MAX must be 1 to 6"
#endif

static uint32_t s_bsp_khz;          /* clk_sys before the boost */
static int      s_clock_state;      /* 0 off, 1 raised, -1 unreachable */
static int      s_flash_state;      /* 0 off, 1 fast, -1 check failed, -2 above QOG_FLASH_MAX_KHZ, -3 recheck failed */
static uint32_t s_flash_div, s_flash_dly, s_old_div, s_old_dly;
static uint32_t s_pass_mask;        /* bit d, RX delay d passed the whole check */
static uint32_t s_checked_kb;
static uint32_t s_reapplied;
#if QOG_FLASH_FAST
static uint32_t s_recheck_ref[RECHECK_BLOCKS];
#endif
static uint32_t s_recheck_ok, s_recheck_bad;

/* ---- flash, RAM resident ------------------------------------------------ */

/* FNV-1a over 32-bit words, read through the uncached alias so every word
   comes from the flash at the divider being tested */
static uint32_t __no_inline_not_in_flash_func(flash_hash)(const volatile uint32_t *p, uint32_t words) {
    uint32_t h = 2166136261u;
    while (words--) h = (h ^ *p++) * 16777619u;
    return h;
}

/* nothing may fetch from flash between the two writes of SSIENR */
static void __no_inline_not_in_flash_func(ssi_set)(uint32_t div, uint32_t dly) {
    ssi_hw->ssienr = 0;
    ssi_hw->baudr = div;
    ssi_hw->rx_sample_dly = dly;
    ssi_hw->ssienr = 1;
}

/* one chunk read twice at a candidate setting, compared with its hash at the
   old one. The old divider and delay are always back before returning to
   flash, whether the chunk matched or not. */
static int __no_inline_not_in_flash_func(flash_try)(uint32_t div, uint32_t dly,
        const volatile uint32_t *p, uint32_t words, uint32_t want) {
    const uint32_t old_div = ssi_hw->baudr, old_dly = ssi_hw->rx_sample_dly;
    const uint32_t ints = save_and_disable_interrupts();
    ssi_set(div, dly);
    int ok = flash_hash(p, words) == want;
    if (ok) ok = flash_hash(p, words) == want;
    ssi_set(old_div, old_dly);
    restore_interrupts(ints);
    return ok;
}

#if QOG_FLASH_FAST
static const volatile uint32_t *nocache(uintptr_t xip_addr) {
    return (const volatile uint32_t *)(xip_addr - XIP_BASE + XIP_NOCACHE_NOALLOC_BASE);
}

static void app_range(uintptr_t *lo, uint32_t *bytes) {
    extern char __flash_binary_start[], __flash_binary_end[];
    *lo = (uintptr_t)__flash_binary_start;
    *bytes = (uint32_t)(__flash_binary_end - __flash_binary_start) & ~3u;
}

/* one region in chunks, every delay still in the mask tried on each chunk */
static uint32_t check_region(uintptr_t addr, uint32_t words, uint32_t mask) {
    while (words && mask) {
        const uint32_t n = words < CHUNK_WORDS ? words : CHUNK_WORDS;
        const volatile uint32_t *p = nocache(addr);
        const uint32_t want = flash_hash(p, n);           /* old divider, interrupts on */
        for (uint32_t dly = 1u; dly <= QOG_FLASH_DLY_MAX; dly++)
            if ((mask & (1u << dly)) && !flash_try(2u, dly, p, n, want))
                mask &= ~(1u << dly);
        addr += n * 4u;
        words -= n;
    }
    return mask;
}

/* the middle of the longest run of passing delays, the later one of two */
static uint32_t pick_delay(uint32_t mask) {
    uint32_t best_lo = 0u, best_len = 0u;
    for (uint32_t d = 1u; d <= QOG_FLASH_DLY_MAX; d++) {
        if (!(mask & (1u << d))) continue;
        uint32_t len = 0u;
        while (d + len <= QOG_FLASH_DLY_MAX && (mask & (1u << (d + len)))) len++;
        if (len > best_len) { best_lo = d; best_len = len; }
        d += len;
    }
    return best_len ? best_lo + best_len / 2u : 0u;
}

static void flash_fast(void) {
    const uint32_t sys = clock_get_hz(clk_sys);
    s_old_div = ssi_hw->baudr;
    s_old_dly = ssi_hw->rx_sample_dly;

    /* the run-time recheck's references, at the old divider */
    uintptr_t lo;
    uint32_t bytes;
    app_range(&lo, &bytes);
    const uint32_t step = (bytes / RECHECK_BLOCKS) & ~3u;
    for (uint32_t i = 0; i < RECHECK_BLOCKS; i++)
        s_recheck_ref[i] = flash_hash(nocache(lo + i * step), RECHECK_WORDS);

    if (sys / 2u > QOG_FLASH_MAX_KHZ * 1000u) {
        s_flash_state = -2;
        return;
    }
    if (bytes > QOG_FLASH_CHECK_KB * 1024u) bytes = QOG_FLASH_CHECK_KB * 1024u;
    uint32_t mask = 0u;
    for (uint32_t d = 1u; d <= QOG_FLASH_DLY_MAX; d++) mask |= 1u << d;
    mask = check_region(lo, bytes / 4u, mask);
    s_checked_kb = bytes / 1024u;
#ifdef QOG_SYS_MAIN
    const uintptr_t data = QOG_MAIN_DATA_XIP_ADDR;
    const uint32_t data_step = (QOG_MAIN_DATA_MAX_SIZE / DATA_BLOCKS) & ~0xFFFu;
#else
    const uintptr_t data = QOG_DATA_XIP_ADDR;
    const uint32_t data_step = (QOG_DATA_MAX_SIZE / DATA_BLOCKS) & ~0xFFFu;
#endif
    for (uint32_t i = 0; i < DATA_BLOCKS && mask; i++) {
        mask = check_region(data + data_step * i, DATA_WORDS, mask);
        s_checked_kb += DATA_WORDS * 4u / 1024u;
    }
    s_pass_mask = mask;
    const uint32_t dly = pick_delay(mask);
    if (!dly) {
        s_flash_state = -1;
        return;
    }
    const uint32_t ints = save_and_disable_interrupts();
    ssi_set(2u, dly);
    restore_interrupts(ints);
    s_flash_state = 1;
    s_flash_div = 2u;
    s_flash_dly = dly;
}

/* one app block read at the live setting against its hash at the old
   divider. Three misses in a row put the old divider back. */
static void flash_recheck(void) {
    static uint32_t next;
    if (s_flash_state != 1) return;
    uintptr_t lo;
    uint32_t bytes;
    app_range(&lo, &bytes);
    const uint32_t i = next++ % RECHECK_BLOCKS;
    const volatile uint32_t *p = nocache(lo + i * ((bytes / RECHECK_BLOCKS) & ~3u));
    int bad = 0;
    while (bad < 3 && flash_hash(p, RECHECK_WORDS) != s_recheck_ref[i]) bad++;
    if (bad == 0) { s_recheck_ok++; return; }
    s_recheck_bad++;
    if (bad < 3) return;            /* one or two misreads, counted, setting kept */
#ifndef QOG_SYS_MAIN
    /* core 1 runs from flash too, it waits in RAM while the SSI is off */
    const bool lock = multicore_lockout_victim_is_initialized(1);
    if (lock) multicore_lockout_start_blocking();
#endif
    const uint32_t ints = save_and_disable_interrupts();
    ssi_set(s_old_div, s_old_dly);
    restore_interrupts(ints);
#ifndef QOG_SYS_MAIN
    if (lock) multicore_lockout_end_blocking();
#endif
    s_flash_state = -3;
    DIAG("[quake] flash recheck FAILED on block %u, back to div %u rx delay %u\n",
         (unsigned)i, (unsigned)s_old_div, (unsigned)s_old_dly);
}
#endif

void qog_flash_fast_reapply(void) {
    if (s_flash_state != 1) return;
    if (ssi_hw->baudr == s_flash_div && ssi_hw->rx_sample_dly == s_flash_dly) return;
    const uint32_t ints = save_and_disable_interrupts();
    ssi_set(s_flash_div, s_flash_dly);
    restore_interrupts(ints);
    s_reapplied++;
}

/* ---- clock --------------------------------------------------------------- */

static enum vreg_voltage vreg_for_mv(unsigned mv) {
    if (mv >= 1300u) return VREG_VOLTAGE_1_30;
    if (mv >= 1250u) return VREG_VOLTAGE_1_25;
    if (mv >= 1200u) return VREG_VOLTAGE_1_20;
    return VREG_VOLTAGE_1_15;
}

void qog_clock_boost(void) {
    s_bsp_khz = clock_get_hz(clk_sys) / 1000u;
#if QOG_SYS_KHZ
    uint vco, pd1, pd2;
    if (!check_sys_clock_khz(QOG_SYS_KHZ, &vco, &pd1, &pd2)) {
        s_clock_state = -1;
    } else if (QOG_SYS_KHZ != s_bsp_khz) {
#ifdef QOG_SYS_MAIN
        /* the FPGA's clock divider for the new clock first, at the old clock
           it gives less than 25 MHz, never more */
        (void)fwog_fpga_clk_start_div(fwog_fpga_clk_divider(QOG_SYS_KHZ * 1000u), 0u);
#endif
        vreg_set_voltage(vreg_for_mv(QOG_SYS_MV));
        sleep_ms(10);
        set_sys_clock_pll(vco, pd1, pd2);
#ifdef QOG_SYS_MAIN
        (void)fwog_fpga_clk_start();
#else
        /* board.c's backlight PWM, FWOG_BACKLIGHT_HZ 10 kHz, wrap 255 */
        pwm_set_clkdiv(pwm_gpio_to_slice_num(PIN_LCD_BL),
                       (float)clock_get_hz(clk_sys) / (10000.0f * 256.0f));
        i2c_set_baudrate(FWOG_I2C, FWOG_I2C_HZ);
#endif
        s_clock_state = 1;
    }
#endif
#if QOG_FLASH_FAST
    flash_fast();
#endif
#if QOG_TEMP_S
    /* the die's temperature sensor, ADC channel 4, clk_adc stays on PLL_USB's
       48 MHz. Nothing else in either app uses the ADC. */
    adc_init();
    adc_set_temp_sensor_enabled(true);
#endif
    qog_clock_report();
}

/* ---- report -------------------------------------------------------------- */

static const char *delays_text(uint32_t mask, char *buf) {
    char *o = buf;
    for (uint32_t d = 1u; d <= QOG_FLASH_DLY_MAX; d++)
        if (mask & (1u << d)) { if (o != buf) *o++ = ','; *o++ = (char)('0' + d); }
    if (o == buf) { *o++ = 'n'; *o++ = 'o'; *o++ = 'n'; *o++ = 'e'; }
    *o = 0;
    return buf;
}

void qog_clock_report(void) {
    const uint32_t sys = clock_get_hz(clk_sys);
    if (s_clock_state > 0)
        DIAG("[quake] clock %u kHz at %u mV (BSP %u kHz), clk_peri %u kHz\n",
             (unsigned)(sys / 1000u), (unsigned)QOG_SYS_MV, (unsigned)s_bsp_khz,
             (unsigned)(clock_get_hz(clk_peri) / 1000u));
    else if (s_clock_state < 0)
        DIAG("[quake] clock %u kHz unreachable, staying at %u kHz\n",
             (unsigned)QOG_SYS_KHZ, (unsigned)(sys / 1000u));
    else
        DIAG("[quake] clock %u kHz (BSP setting)\n", (unsigned)(sys / 1000u));

    char dl[16];
    const uint32_t div = ssi_hw->baudr ? ssi_hw->baudr : 1u;
    const unsigned mhz = (unsigned)(sys / div / 1000000u), tenth = (unsigned)(sys / div / 100000u % 10u);
    if (s_flash_state > 0)
        DIAG("[quake] flash %u.%u MHz ok (div %u from %u, rx delay %u, delays passing %s of 1 to %u, %u KB checked, reapplied %u)\n",
             mhz, tenth, (unsigned)div, (unsigned)s_old_div, (unsigned)s_flash_dly,
             delays_text(s_pass_mask, dl), (unsigned)QOG_FLASH_DLY_MAX,
             (unsigned)s_checked_kb, (unsigned)s_reapplied);
    else if (s_flash_state == -1)
        DIAG("[quake] flash %u.%u MHz fallback (div %u, rx delay %u): read check FAILED at div 2 with every rx delay 1 to %u\n",
             mhz, tenth, (unsigned)div, (unsigned)ssi_hw->rx_sample_dly, (unsigned)QOG_FLASH_DLY_MAX);
    else if (s_flash_state == -2)
        DIAG("[quake] flash %u.%u MHz (div %u): div 2 would pass QOG_FLASH_MAX_KHZ %u, not tried\n",
             mhz, tenth, (unsigned)div, (unsigned)QOG_FLASH_MAX_KHZ);
    else if (s_flash_state == -3)
        DIAG("[quake] flash %u.%u MHz (div %u, rx delay %u): fell back after a FAILED recheck\n",
             mhz, tenth, (unsigned)div, (unsigned)ssi_hw->rx_sample_dly);
    else
        DIAG("[quake] flash %u.%u MHz (div %u, QOG_FLASH_FAST off)\n", mhz, tenth, (unsigned)div);
}

#if QOG_TEMP_S
/* 27 - (V - 0.706) / 0.001721 with V = raw * 3.3 / 4096, in tenths of a
   degree, from the sum of 8 readings */
static int temp_tenths(void) {
    const uint saved = adc_get_selected_input();
    adc_select_input(4);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    adc_select_input(saved);
    const int32_t uv = (int32_t)((uint64_t)sum * 3300000u / 32768u);   /* average, microvolts */
    return 270 - (uv - 706000) * 10 / 1721;
}
#endif

/* called once a frame by both main loops, the clock and flash report once,
   about 6 s after boot when USB is up, then every QOG_TEMP_S seconds the
   temperature line and one flash recheck, and the profiler's report */
void qog_clock_report_once(void) {
    static int done;
    static uint32_t next_us;
    const uint32_t now = time_us_32();
    if (!done) {
        if (now < 6000000u) return;
        done = 1;
        next_us = now;
        qog_clock_report();
    }
#ifdef QOG_PROF
    qog_prof_poll();
#endif
#if QOG_TEMP_S
    if ((int32_t)(now - next_us) < 0) return;
    next_us = now + QOG_TEMP_S * 1000000u;
#if QOG_FLASH_FAST
    flash_recheck();
#endif
    const int t = temp_tenths();
    const uint32_t sys = clock_get_hz(clk_sys), div = ssi_hw->baudr ? ssi_hw->baudr : 1u;
    DIAG("[quake] temp %s%d.%u C, clock %u MHz %u mV, flash %u MHz div %u dly %u, recheck ok %u bad %u\n",
         t < 0 && t > -10 ? "-" : "", t / 10, (unsigned)(t < 0 ? -t : t) % 10u,
         (unsigned)(sys / 1000000u), (unsigned)QOG_SYS_MV,
         (unsigned)(sys / div / 1000000u), (unsigned)div, (unsigned)ssi_hw->rx_sample_dly,
         (unsigned)s_recheck_ok, (unsigned)s_recheck_bad);
#endif
}
