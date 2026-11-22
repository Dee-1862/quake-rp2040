#include "qog_mem.h"
#include "quakedef.h"

/* Private to zone.c, mirrored here so zone.c stays unmodified. If zone.c's
   structs ever change, change these with them. */
typedef struct memblock_s {
    int size, tag, id;
    struct memblock_s *next, *prev;
    int pad;
} memblock_t;
typedef struct {
    int size;
    memblock_t blocklist;
    memblock_t *rover;
} memzone_t;
typedef struct cache_system_s {
    int size;
    cache_user_t *user;
    char name[16];
    struct cache_system_s *prev, *next;
    struct cache_system_s *lru_prev, *lru_next;
} cache_system_t;

extern int hunk_size, hunk_low_used, hunk_high_used;
extern memzone_t *mainzone;
extern cache_system_t cache_head;

static qog_mem_t s_m;

static int cache_bytes(void) {
    int n = 0;
    for (cache_system_t *c = cache_head.next; c && c != &cache_head; c = c->next) n += c->size;
    return n;
}

static int zone_bytes(void) {
    if (!mainzone) return 0;
    int n = 0;
    for (memblock_t *b = mainzone->blocklist.next; b && b != &mainzone->blocklist; b = b->next)
        if (b->tag) n += b->size;
    return n;
}

#define PEAK(p, v) do { if ((v) > (p)) (p) = (v); } while (0)

const qog_mem_t *qog_mem_sample(void) {
    s_m.hunk_size = hunk_size;
    s_m.hunk_low = hunk_low_used;
    s_m.hunk_high = hunk_high_used;
    s_m.cache = cache_bytes();
    s_m.zone_used = zone_bytes();
    s_m.zone_size = mainzone ? mainzone->size : 0;
    s_m.total = s_m.hunk_low + s_m.hunk_high + s_m.cache + s_m.zone_size;
    PEAK(s_m.peak_low, s_m.hunk_low);
    PEAK(s_m.peak_high, s_m.hunk_high);
    PEAK(s_m.peak_cache, s_m.cache);
    PEAK(s_m.peak_zone, s_m.zone_used);
    PEAK(s_m.peak_total, s_m.total);
    return &s_m;
}

void qog_mem_reset_peaks(void) {
    s_m.peak_low = s_m.peak_high = s_m.peak_cache = s_m.peak_zone = s_m.peak_total = 0;
}

static void kb_line(const char *what, int now, int peak) {
    Con_Printf("  %-10s %7.1f KB   peak %7.1f KB\n", what, now / 1024.0, peak / 1024.0);
}

static void cmd_qog_mem(void) {
    if (Cmd_Argc() > 1 && !Q_strcmp(Cmd_Argv(1), "reset")) {
        qog_mem_reset_peaks();
        Con_Printf("qog_mem: peaks cleared\n");
        return;
    }
    const qog_mem_t *m = qog_mem_sample();
    Con_Printf("Dynamic RAM (hunk given %d KB):\n", m->hunk_size / 1024);
    kb_line("hunk low", m->hunk_low, m->peak_low);
    kb_line("hunk high", m->hunk_high, m->peak_high);
    kb_line("cache", m->cache, m->peak_cache);
    kb_line("zone used", m->zone_used, m->peak_zone);
    Con_Printf("  zone size  %7.1f KB (fixed)\n", m->zone_size / 1024.0);
    kb_line("TOTAL", m->total, m->peak_total);
    Con_Printf("Static RAM: see build/ram-report.md (tools/ram_meter.py)\n");
}

/* Scripted runs: `quit` can open the "are you sure" menu and wait forever. */
static void cmd_qog_quit(void) { Sys_Quit(); }

void qog_mem_register(void) {
    Cmd_AddCommand("qog_mem", cmd_qog_mem);
    Cmd_AddCommand("qog_quit", cmd_qog_quit);
}
