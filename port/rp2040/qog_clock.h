/* QuakeOG (Agent AB): the Quake apps' faster clocks (port/rp2040/clock_rp2040.c).
 *
 * The BSP brings both chips up at 200 MHz with the flash at clk_sys / 4, and
 * the display bootloader keeps those settings. The Quake apps raise them at
 * run time, right after board_init(), through their own build options
 * (apps/quake/CMakeLists.txt):
 *
 *   QOG_SYS_KHZ     clk_sys in kHz (250000), 0 keeps the BSP's clock
 *   QOG_SYS_MV      core voltage in mV for it (1200), set before the clock
 *   QOG_FLASH_FAST  the QSPI flash at clk_sys / 2 (125 MHz at 250 MHz),
 *                   switched from RAM, checked by reading the app's own
 *                   code back, and put back to the old divider if the
 *                   check fails
 *
 * Only built into quake_display and quake_main (QOG_CLOCK_BOOST). */
#ifndef QOG_CLOCK_H
#define QOG_CLOCK_H

/* Raise the clock, re-derive what board_init() derived from the old one,
   then switch the flash divider. Call right after board_init(), before
   core 1 starts and before any driver takes a divider from clock_get_hz(). */
void qog_clock_boost(void);

/* Print the outcome over USB (also printed by qog_clock_boost, usually
   before USB is up, so the main loops print it again once, a few seconds
   after boot, with qog_clock_report_once). */
void qog_clock_report(void);
void qog_clock_report_once(void);

/* Main chip: the SDK's flash erase and program end by running boot2 again,
   which puts the divider back to 4. Called after the FAT volume's writes,
   it restores the divider the check accepted. Costs a register read when
   nothing changed. */
void qog_flash_fast_reapply(void);

#endif
