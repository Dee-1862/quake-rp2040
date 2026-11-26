/* QuakeOG, the profiler's function table, a fixed size hole in
 * the app's flash image (port/rp2040/prof_rp2040.c).
 *
 * The link leaves it holding only the placeholder magic 'QPR0'. The app's
 * POST_BUILD step (tools/prof_table.py) then reads the linked ELF's
 * function symbols and writes the table into these bytes of the .elf, the
 * .bin and the .uf2. The array's size never changes between the two, so no
 * code or data moves. It is in its own file so the compiler of the profiler
 * cannot fold its placeholder contents into the code that reads it. */
#include <stdint.h>

#ifndef QOG_PROF_TABLE_KB
#define QOG_PROF_TABLE_KB 96
#endif

__attribute__((used, aligned(4), section(".rodata.qog_prof_blob")))
const uint32_t qog_prof_blob[QOG_PROF_TABLE_KB * 256] = { 0x30525051u /* "QPR0" */, QOG_PROF_TABLE_KB * 1024u };
