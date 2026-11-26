/* QuakeOG, a PC-sampling profiler for the board (port/rp2040/prof_rp2040.c).
 *
 * Build option QOG_PROF (off by default, apps/quake/CMakeLists.txt). A
 * hardware timer alarm interrupts core 0 QOG_PROF_HZ times a second (4,000)
 * at the highest priority, and the handler reads the interrupted PC from the
 * exception frame (the process stack for the game, the main stack inside
 * another interrupt). With QOG_PROF_CORE1 a second alarm does the same on
 * core 1 of the display. Each PC is looked up in a function table that
 * tools/prof_table.py writes into the app's flash image after the link (the
 * ELF, .bin and .uf2 are patched in place, so the code's addresses do not
 * move), and counted per function. Samples in the boot ROM (the float
 * routines) are also counted against the caller found in the stacked LR.
 *
 * Every QOG_PROF_REPORT_S seconds (5) each core's top QOG_PROF_TOP (20)
 * functions and the top ROM callers are printed over USB, then the counts
 * start again. tools/prof_resolve.py sums the windows of a saved log. */
#ifndef QOG_PROF_H
#define QOG_PROF_H

/* once a frame from qog_clock_report_once, starts the profiler on its first
   call, then prints and clears the counts every QOG_PROF_REPORT_S seconds */
void qog_prof_poll(void);

#endif
