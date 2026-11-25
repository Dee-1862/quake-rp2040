/* QuakeOG (Agent AN): the board's PC-sampling profiler (qog_prof.h).
 *
 * Sampling: one hardware alarm per core, its TIMER_IRQ enabled only in that
 * core's NVIC, priority 0 so it also samples the other interrupts. The
 * handler is RAM resident. It picks the stack the interrupted code used from
 * EXC_RETURN (bit 2: process stack, where the game runs, or main stack) and
 * takes the stacked PC (word 6) and LR (word 5). The next alarm is the
 * period plus or minus an eighth, so a loop with the same period cannot hide
 * from the samples.
 *
 * Lookup: the function table in flash (prof_table_rp2040.c, written by
 * tools/prof_table.py) holds the sorted start and end of every function,
 * flash ones first, then the RAM resident ones, and a page index: for each
 * 2^shift bytes of flash text the last function starting at or before it.
 * The page index is copied to RAM, so a flash PC costs one or two table
 * reads (usually cached) and a RAM PC a binary search over the few RAM
 * functions. PCs in the boot ROM (0 to 0x3FFF, the float routines) count as
 * "[rom]" and once more against the function in the stacked LR.
 *
 * Counts: per core an open addressed table of (function, count) pairs. The
 * report pauses the sampling, takes each core's top QOG_PROF_TOP, prints
 * them and clears the tables, so every window starts from nothing.
 *
 * RAM: 2 KB for core 0's table, 1 KB for core 1's, up to 1 KB of page index.
 * The profiler build takes it from the hunk (apps/quake/CMakeLists.txt). */
#include <stdint.h>
#include <string.h>
#include "qog_prof.h"
#include "pico/stdlib.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"
#include "common/diag.h"
#if defined(QOG_PROF_CORE1) && !defined(QOG_SYS_MAIN)
#include "qog_video.h"
#define PROF_CORES 2
#else
#define PROF_CORES 1
#endif

#ifndef QOG_PROF_HZ
#define QOG_PROF_HZ 4000
#endif
#ifndef QOG_PROF_REPORT_S
#define QOG_PROF_REPORT_S 5
#endif
#ifndef QOG_PROF_TOP
#define QOG_PROF_TOP 20
#endif
#ifndef QOG_PROF_ROMTOP
#define QOG_PROF_ROMTOP 8
#endif
#define PROF_PAGES     512u
#define SLOTS0_LOG2    9           /* 512 pairs, 2 KB */
#define SLOTS1_LOG2    8           /* 256 pairs, 1 KB */
#define PROBES         16

#if QOG_PROF_HZ < 100 || QOG_PROF_HZ > 20000
#error "QOG_PROF_HZ must be 100 to 20000"
#endif
#if QOG_PROF_HZ * QOG_PROF_REPORT_S > 60000
#error "QOG_PROF_HZ * QOG_PROF_REPORT_S must stay below the 16-bit counts"
#endif

#define PROF_MAGIC   0x46525051u   /* "QPRF", written by tools/prof_table.py */
#define KEY_EMPTY    0xFFFFu
#define ID_ROM       0x7FFFu
#define ID_UNKNOWN   0x7FFEu
#define ROM_CALLER   0x8000u       /* key flag: a ROM sample, keyed by its caller */

typedef struct { uint16_t key, n; } prof_slot_t;

extern const uint32_t qog_prof_blob[];

static prof_slot_t s_tab0[1u << SLOTS0_LOG2];
#if PROF_CORES > 1
static prof_slot_t s_tab1[1u << SLOTS1_LOG2];
#endif
static uint16_t s_pages[PROF_PAGES + 1u];
static const uint32_t *s_starts, *s_ends, *s_name_off;
static const char *s_pool;
static uint32_t s_n, s_nflash, s_page_base, s_page_shift, s_npages;
static uint32_t s_alarm[2], s_rng[2] = { 0x9E3779B9u, 0x85EBCA6Bu };
static uint32_t s_total[2], s_lost[2];
static volatile uint32_t s_pause, s_busy1;
static uint32_t s_period_us, s_jitter_mask;
static int s_state;                 /* 0 not started, 1 running, -1 off */
static uint32_t s_next_us, s_window_us;

/* ---- interrupt, RAM resident -------------------------------------------- */

static uint32_t __not_in_flash_func(prof_find)(uint32_t pc) {
    if (pc < 0x4000u) return ID_ROM;
    uint32_t lo, hi;
    const uint32_t off = pc - s_page_base;
    if (off < (s_npages << s_page_shift)) {
        const uint32_t p = off >> s_page_shift;
        lo = s_pages[p];
        hi = s_pages[p + 1u] + 1u;
        if (hi > s_nflash) hi = s_nflash;
    } else if (pc >= 0x20000000u && pc < 0x20042000u) {
        lo = s_nflash;
        hi = s_n;
    } else {
        return ID_UNKNOWN;
    }
    if (lo >= hi || s_starts[lo] > pc) return ID_UNKNOWN;
    while (hi - lo > 1u) {
        const uint32_t mid = (lo + hi) >> 1;
        if (s_starts[mid] <= pc) lo = mid; else hi = mid;
    }
    return pc < s_ends[lo] ? lo : ID_UNKNOWN;
}

static void __not_in_flash_func(prof_add)(prof_slot_t *t, uint32_t log2, uint32_t key, uint32_t core) {
    const uint32_t mask = (1u << log2) - 1u;
    const uint32_t h = (key * 2654435761u) >> (32u - log2);
    for (uint32_t i = 0; i < PROBES; i++) {
        prof_slot_t *s = &t[(h + i) & mask];
        if (s->key == key) {
            if (s->n != 0xFFFFu) s->n++;
            return;
        }
        if (s->key == KEY_EMPTY) {
            s->key = (uint16_t)key;
            s->n = 1u;
            return;
        }
    }
    s_lost[core]++;
}

/* called by qog_prof_irq with the interrupted context's exception frame */
void __attribute__((used, noinline)) __not_in_flash_func(qog_prof_sample)(const uint32_t *frame) {
    const uint32_t core = sio_hw->cpuid;
    const uint32_t a = s_alarm[core];
    timer_hw->intr = 1u << a;
    uint32_t r = s_rng[core];
    r ^= r << 13; r ^= r >> 17; r ^= r << 5;
    s_rng[core] = r;
    timer_hw->alarm[a] = timer_hw->timerawl + s_period_us - (s_jitter_mask >> 1) + (r & s_jitter_mask);
    if (core) { s_busy1 = 1u; __dmb(); }
    if (!s_pause) {
#if PROF_CORES > 1
        prof_slot_t *t = core ? s_tab1 : s_tab0;
        const uint32_t log2 = core ? SLOTS1_LOG2 : SLOTS0_LOG2;
#else
        prof_slot_t *t = s_tab0;
        const uint32_t log2 = SLOTS0_LOG2;
#endif
        const uint32_t id = prof_find(frame[6]);
        prof_add(t, log2, id, core);
        if (id == ID_ROM) {
            uint32_t caller = prof_find(frame[5] & ~1u);
            if (caller == ID_ROM) caller = ID_UNKNOWN;   /* called from the ROM itself */
            prof_add(t, log2, ROM_CALLER | caller, core);
        }
        s_total[core]++;
    }
    if (core) { __dmb(); s_busy1 = 0u; }
}

/* EXC_RETURN bit 2 set: the interrupted code ran on the process stack */
static void __attribute__((naked, used, section(".time_critical.qog_prof_irq"))) qog_prof_irq(void) {
    __asm volatile (
        "movs r0, #4\n\t"
        "mov r1, lr\n\t"
        "tst r0, r1\n\t"
        "beq 1f\n\t"
        "mrs r0, psp\n\t"
        "b 2f\n"
        "1:\n\t"
        "mrs r0, msp\n"
        "2:\n\t"
        "ldr r1, 3f\n\t"
        "bx r1\n\t"
        ".align 2\n"
        "3:\n\t"
        ".word qog_prof_sample\n");
}

/* ---- set up --------------------------------------------------------------- */

static void prof_arm(uint32_t a) {
    hw_set_bits(&timer_hw->inte, 1u << a);
    timer_hw->alarm[a] = timer_hw->timerawl + s_period_us;
}

#if PROF_CORES > 1
/* run once on core 1 through the frame's job slot: its own NVIC */
static void prof_core1_job(void *arg) {
    (void)arg;
    const uint irq = hardware_alarm_get_irq_num(s_alarm[1]);
    irq_set_priority(irq, 0);
    irq_set_enabled(irq, true);
    prof_arm(s_alarm[1]);
}
#endif

static void prof_clear(void) {
    memset(s_tab0, 0xFF, sizeof s_tab0);
#if PROF_CORES > 1
    memset(s_tab1, 0xFF, sizeof s_tab1);
#endif
    for (int c = 0; c < 2; c++) s_total[c] = s_lost[c] = 0u;
}

static int prof_start(void) {
    const uint32_t *b = qog_prof_blob;
    __asm volatile ("" : "+r" (b));     /* the bytes were written after the link */
    if (b[0] != PROF_MAGIC) {
        DIAG("[prof] no function table (tools/prof_table.py did not patch this image), profiler off\n");
        return -1;
    }
    s_n = b[1];
    s_page_shift = b[2];
    s_page_base = b[3];
    s_npages = b[4];
    s_starts = (const uint32_t *)((const char *)b + b[5]);
    s_ends = (const uint32_t *)((const char *)b + b[6]);
    s_name_off = (const uint32_t *)((const char *)b + b[7]);
    const uint16_t *pages = (const uint16_t *)((const char *)b + b[8]);
    s_pool = (const char *)b + b[9];
    s_nflash = b[10];
    if (s_npages > PROF_PAGES || s_n >= ID_UNKNOWN || s_nflash > s_n) {
        DIAG("[prof] function table out of range (%u functions, %u pages), profiler off\n",
             (unsigned)s_n, (unsigned)s_npages);
        return -1;
    }
    memcpy(s_pages, pages, (s_npages + 1u) * sizeof s_pages[0]);
    prof_clear();

    s_period_us = 1000000u / QOG_PROF_HZ;
    s_jitter_mask = 1u;
    while (s_jitter_mask * 2u <= s_period_us / 4u) s_jitter_mask *= 2u;
    s_jitter_mask -= 1u;            /* period +- period / 8 at most */

    for (int c = 0; c < PROF_CORES; c++) {
        const int a = hardware_alarm_claim_unused(false);
        if (a < 0) {
            DIAG("[prof] no free timer alarm for core %d, profiler off\n", c);
            return -1;
        }
        s_alarm[c] = (uint32_t)a;
        irq_set_exclusive_handler(hardware_alarm_get_irq_num((uint)a), qog_prof_irq);
    }
    const uint irq0 = hardware_alarm_get_irq_num(s_alarm[0]);
    irq_set_priority(irq0, 0);
    irq_set_enabled(irq0, true);
    prof_arm(s_alarm[0]);
#if PROF_CORES > 1
    qog_video_core1_start(prof_core1_job, NULL);
    qog_video_core1_join();
#endif
    DIAG("[prof] on: %u functions (%u in RAM), %u Hz on %d core%s, alarms %u %u, report every %u s\n",
         (unsigned)s_n, (unsigned)(s_n - s_nflash), (unsigned)QOG_PROF_HZ, PROF_CORES,
         PROF_CORES > 1 ? "s" : "", (unsigned)s_alarm[0], (unsigned)s_alarm[PROF_CORES - 1],
         (unsigned)QOG_PROF_REPORT_S);
    return 1;
}

/* ---- report --------------------------------------------------------------- */

static const char *prof_name(uint32_t id) {
    if (id == ID_ROM) return "[rom]";
    if (id == ID_UNKNOWN) return "[unknown]";
    return s_pool + s_name_off[id];
}

/* the top `want` slots whose ROM_CALLER flag is `flag`, largest first */
static int prof_top(const prof_slot_t *t, uint32_t slots, uint32_t flag, int want,
                    uint16_t *key, uint16_t *n) {
    int have = 0;
    for (uint32_t i = 0; i < slots; i++) {
        if (t[i].key == KEY_EMPTY || (t[i].key & ROM_CALLER) != flag) continue;
        const uint16_t c = t[i].n;
        if (have == want && c <= n[have - 1]) continue;
        int j = have < want ? have++ : want - 1;
        while (j > 0 && n[j - 1] < c) { key[j] = key[j - 1]; n[j] = n[j - 1]; j--; }
        key[j] = t[i].key;
        n[j] = c;
    }
    return have;
}

static void prof_print(int core, const prof_slot_t *t, uint32_t slots, uint32_t total,
                       uint32_t lost, uint32_t window_us) {
    uint16_t key[QOG_PROF_TOP], n[QOG_PROF_TOP];
    uint32_t used = 0;
    for (uint32_t i = 0; i < slots; i++) used += t[i].key != KEY_EMPTY;
    DIAG("[prof] core %d: %u samples in %u.%u s, %u slots of %u used, %u lost\n",
         core, (unsigned)total, (unsigned)(window_us / 1000000u),
         (unsigned)(window_us / 100000u % 10u), (unsigned)used, (unsigned)slots, (unsigned)lost);
    if (!total) return;
    const int top = prof_top(t, slots, 0u, QOG_PROF_TOP, key, n);
    for (int i = 0; i < top; i++) {
        const uint32_t pm = (uint32_t)n[i] * 1000u / total;   /* tenths of a percent */
        const uint32_t addr = key[i] < s_n ? s_starts[key[i]] : 0u;
        DIAG("[prof] c%d %2d %3u.%u%% %5u %s 0x%08x\n", core, i + 1, (unsigned)(pm / 10u),
             (unsigned)(pm % 10u), (unsigned)n[i], prof_name(key[i]), (unsigned)addr);
    }
    const int rtop = prof_top(t, slots, ROM_CALLER, QOG_PROF_ROMTOP, key, n);
    for (int i = 0; i < rtop; i++) {
        const uint32_t pm = (uint32_t)n[i] * 1000u / total;
        const uint32_t id = key[i] & ~ROM_CALLER;
        const uint32_t addr = id < s_n ? s_starts[id] : 0u;
        DIAG("[prof] c%d rom %3u.%u%% %5u from %s 0x%08x\n", core, (unsigned)(pm / 10u),
             (unsigned)(pm % 10u), (unsigned)n[i], prof_name(id), (unsigned)addr);
    }
}

void qog_prof_poll(void) {
    const uint32_t now = time_us_32();
    if (s_state == 0) {
        s_state = prof_start();
        s_window_us = now;
        s_next_us = now + QOG_PROF_REPORT_S * 1000000u;
        return;
    }
    if (s_state < 0 || (int32_t)(now - s_next_us) < 0) return;
    s_pause = 1u;
    __dmb();
    while (s_busy1) tight_loop_contents();
    const uint32_t window = now - s_window_us;
    prof_print(0, s_tab0, 1u << SLOTS0_LOG2, s_total[0], s_lost[0], window);
#if PROF_CORES > 1
    prof_print(1, s_tab1, 1u << SLOTS1_LOG2, s_total[1], s_lost[1], window);
#endif
    prof_clear();
    s_window_us = time_us_32();
    s_next_us = s_window_us + QOG_PROF_REPORT_S * 1000000u;
    __dmb();
    s_pause = 0u;
}
