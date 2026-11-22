/* RAM meter, runtime half: how much of Quake's dynamic memory is in use.
 *
 * Reads id's own allocator globals (zone.c) without changing zone.c:
 *   hunk low   level data: BSP, models, progs, edicts (grows up)
 *   hunk high  temporary and per-level buffers (grows down)
 *   cache      LRU cache that lives in the gap between low and high
 *   zone       small fixed heap for strings and small structs
 *
 * The static half (globals, BSS) is measured at build time with the ARM
 * compiler by tools/ram_meter.py, so it reflects the RP2040's sizes.
 *
 * Pointers are 32 bits on both the PC build (Win32) and the RP2040, so these
 * dynamic numbers carry over to the board. */
#ifndef QOG_MEM_H
#define QOG_MEM_H

typedef struct {
    int hunk_size;                       /* what the port handed to Memory_Init */
    int hunk_low, hunk_high, cache, zone_used, zone_size;
    int peak_low, peak_high, peak_cache, peak_zone, peak_total;
    int total;                           /* low + high + cache + zone_size */
} qog_mem_t;

/* Sample now and update peaks. Cheap; call once per frame. */
const qog_mem_t *qog_mem_sample(void);

/* Forget peaks, e.g. at a map change. */
void qog_mem_reset_peaks(void);

/* Registers the console command `qog_mem` (prints a report, `qog_mem reset`
 * clears peaks). Call after Host_Init. */
void qog_mem_register(void);

#endif
