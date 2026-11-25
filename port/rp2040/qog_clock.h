/* QuakeOG (Agents AB and AN): the Quake apps' faster clocks (port/rp2040/clock_rp2040.c).
 *
 * The BSP brings both chips up at 200 MHz with the flash at clk_sys / 4, and
 * the display bootloader keeps those settings. The Quake apps raise them at
 * run time, right after board_init(), through their own build options
 * (apps/quake/CMakeLists.txt):
 *
 *   QOG_SYS_KHZ        clk_sys in kHz (300000), 0 keeps the BSP's clock
 *   QOG_SYS_MV         core voltage in mV for it (1250), set before the clock
 *   QOG_FLASH_FAST     the QSPI flash at clk_sys / 2 (150 MHz at 300 MHz),
 *                      switched from RAM, checked by reading the app and a
 *                      sample of the data region back at every RX sample
 *                      delay, and left at the old divider if none passes
 *   QOG_FLASH_MAX_KHZ  the fastest flash clock tried (150000), 133000 keeps
 *                      the W25Q128JV's rating
 *   QOG_TEMP_S         seconds between the temperature lines (5), 0 for none
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
   after boot, with qog_clock_report_once). qog_clock_report_once runs every
   frame: after that first report it prints the temperature line every
   QOG_TEMP_S seconds, rereads one flash block, and runs the profiler's
   report when QOG_PROF is on. */
void qog_clock_report(void);
void qog_clock_report_once(void);

/* Main chip: the SDK's flash erase and program end by running boot2 again,
   which puts the divider back to 4. Called after the FAT volume's writes,
   it restores the divider the check accepted. Costs a register read when
   nothing changed. */
void qog_flash_fast_reapply(void);

#endif
