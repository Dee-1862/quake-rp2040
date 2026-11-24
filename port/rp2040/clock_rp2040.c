/* QuakeOG (Agent AB): the Quake apps' faster clocks, set at run time.
 *
 * Why here and not in the BSP: the BSP's 200 MHz and flash CLKDIV 4 are also
 * the display bootloader's, and the display CPU has no BOOTSEL button, so a
 * bootloader that failed at a faster setting could never be reflashed. The
 * bootloader and every other app keep the BSP's settings. Only quake_display
 * and quake_main call qog_clock_boost(), right after board_init().
 *
 * 1. Voltage, then clock. 250 MHz is past the RP2040's rated 200 MHz at
 *    1.15 V (the BSP's fwog_vreg_mv_for_khz clamps there on purpose), so the
 *    core voltage goes up first (QOG_SYS_MV, 1.20 V, the lowest setting
 *    widely reported stable at 250 MHz), settles for 10 ms as the BSP's
 *    fwog_clocks_init does, then set_sys_clock_khz. clk_peri follows clk_sys
 *    (PICO_CLOCK_ADJUST_PERI_CLOCK_WITH_SYS_CLOCK in the board header).
 *    clk_ref, and with it the 1 MHz timer tick, does not change.
 *
 * 2. What board_init() already derived from 200 MHz is derived again:
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
 *    both chips: from RAM, interrupts off, core 1 not started yet. The
 *    W25Q128JV reads quad I/O (0xEB, continuous mode, 4 dummy clocks after
 *    the mode byte, as boot2_w25q080 sets it up) at up to 133 MHz, so clk_sys
 *    / 2 is used only when it is at most 133 MHz (125 MHz at 250 MHz). The RX
 *    sample delay stays 1 system clock, what the SDK's boot2_w25q080 writes
 *    for every divider including 2 (and what runs at 125 to 133 MHz on
 *    overclocked Picos); 2 is tried if 1 fails.
 *    Check: the first QOG_FLASH_CHECK_KB of the app's own image are hashed
 *    through the uncached alias at the old divider, then twice at the new
 *    one with interrupts off. Any difference puts the old divider and sample
 *    delay back before a single instruction is fetched from flash again.
 *
 * Not run on hardware yet. */
#include <stdint.h>
#include "qog_clock.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/structs/ssi.h"
#include "hardware/regs/addressmap.h"
#include "common/diag.h"
#ifdef QOG_SYS_MAIN
#include "fwog_main.h"
#include "fpga/fpga_clk.h"
#else
#include "fwog_display.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#endif

#ifndef QOG_SYS_KHZ
#define QOG_SYS_KHZ 0
#endif
#ifndef QOG_SYS_MV
#define QOG_SYS_MV 1200
#endif
#ifndef QOG_FLASH_FAST
#define QOG_FLASH_FAST 0
#endif
#ifndef QOG_FLASH_CHECK_KB
#define QOG_FLASH_CHECK_KB 128
#endif
#define FLASH_MAX_HZ 133000000u     /* W25Q128JV, quad I/O fast read */

#if QOG_SYS_KHZ && (QOG_SYS_MV < 1150 || QOG_SYS_MV > 1300)
#error "QOG_SYS_MV must be 1150 to 1300"
#endif

static uint32_t s_bsp_khz;          /* clk_sys before the boost */
static int      s_clock_state;      /* 0 off, 1 raised, -1 unreachable */
static int      s_flash_state;      /* 0 off, 1 fast, -1 check failed, -2 too fast for the part */
static uint32_t s_flash_div, s_flash_dly, s_old_div, s_old_dly;
static uint32_t s_reapplied;

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

/* the new divider, checked against the hash taken at the old one, and put
   back before returning to flash when the check fails */
static int __no_inline_not_in_flash_func(flash_try)(uint32_t div, uint32_t dly,
        const volatile uint32_t *p, uint32_t words, uint32_t want) {
    const uint32_t old_div = ssi_hw->baudr, old_dly = ssi_hw->rx_sample_dly;
    const uint32_t ints = save_and_disable_interrupts();
    ssi_set(div, dly);
    int ok = flash_hash(p, words) == want;
    if (ok) ok = flash_hash(p, words) == want;
    if (!ok) ssi_set(old_div, old_dly);
    restore_interrupts(ints);
    return ok;
}

static void flash_fast(void) {
    extern char __flash_binary_start[], __flash_binary_end[];
    const uint32_t sys = clock_get_hz(clk_sys);
    s_old_div = ssi_hw->baudr;
    s_old_dly = ssi_hw->rx_sample_dly;
    if (sys / 2u > FLASH_MAX_HZ) {
        s_flash_state = -2;
        return;
    }
    uint32_t bytes = (uint32_t)(__flash_binary_end - __flash_binary_start);
    if (bytes > QOG_FLASH_CHECK_KB * 1024u) bytes = QOG_FLASH_CHECK_KB * 1024u;
    const volatile uint32_t *p = (const volatile uint32_t *)
        ((uintptr_t)__flash_binary_start - XIP_BASE + XIP_NOCACHE_NOALLOC_BASE);
    const uint32_t words = bytes / 4u;
    const uint32_t want = flash_hash(p, words);   /* old divider, interrupts on */
    s_flash_state = -1;
    for (uint32_t dly = 1u; dly <= 2u; dly++) {
        if (flash_try(2u, dly, p, words, want)) {
            s_flash_state = 1;
            s_flash_div = 2u;
            s_flash_dly = dly;
            break;
        }
    }
}

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
        /* the FPGA's clock divider for the new clock first: at the old clock
           it gives less than 25 MHz, never more */
        (void)fwog_fpga_clk_start_div(fwog_fpga_clk_divider(QOG_SYS_KHZ * 1000u), 0u);
#endif
        vreg_set_voltage(vreg_for_mv(QOG_SYS_MV));
        sleep_ms(10);
        set_sys_clock_pll(vco, pd1, pd2);
#ifdef QOG_SYS_MAIN
        (void)fwog_fpga_clk_start();
#else
        /* board.c's backlight PWM: FWOG_BACKLIGHT_HZ 10 kHz, wrap 255 */
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
    qog_clock_report();
}

/* ---- report -------------------------------------------------------------- */

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

    const uint32_t div = ssi_hw->baudr ? ssi_hw->baudr : 1u;
    const unsigned mhz = (unsigned)(sys / div / 1000000u), tenth = (unsigned)(sys / div / 100000u % 10u);
    if (s_flash_state > 0)
        DIAG("[quake] flash %u.%u MHz ok (div %u from %u, rx delay %u, %u KB checked, reapplied %u)\n",
             mhz, tenth, (unsigned)div, (unsigned)s_old_div, (unsigned)s_flash_dly,
             (unsigned)QOG_FLASH_CHECK_KB, (unsigned)s_reapplied);
    else if (s_flash_state == -1)
        DIAG("[quake] flash %u.%u MHz fallback (div %u, rx delay %u): read check FAILED at div 2\n",
             mhz, tenth, (unsigned)div, (unsigned)s_old_dly);
    else if (s_flash_state == -2)
        DIAG("[quake] flash %u.%u MHz (div %u): div 2 would pass the part's 133 MHz, not tried\n",
             mhz, tenth, (unsigned)div);
    else
        DIAG("[quake] flash %u.%u MHz (div %u, QOG_FLASH_FAST off)\n", mhz, tenth, (unsigned)div);
}

void qog_clock_report_once(void) {
    static int done;
    if (done || time_us_32() < 6000000u) return;
    done = 1;
    qog_clock_report();
}
