/* 8-bit palettized frame -> ST7789 over SPI1, fed by core 1.
 *
 * The game draws into an 8-bit buffer it owns (src_w x src_h). present()
 * hands it to core 1, which walks it one line at a time: palette lookup to
 * RGB565, horizontal/vertical nearest-neighbour scale to 320 x out_h, and DMA
 * of that line to the panel while it builds the next one. There is never a
 * 16-bit frame buffer, so the only RAM cost here is two 640-byte lines.
 *
 * Bypasses st7789_blit() on purpose: that call is synchronous and byte-swaps
 * on the CPU (wiliOGbsp lcd/st7789.c). Here the SPI runs in 16-bit frame
 * mode so RGB565 goes out in native order with no swap and no CPU in the
 * loop. The BSP driver still owns panel init, window setup and fills.
 *
 * Ownership rule: while a scanout is in flight the game must not write the
 * buffer it passed to present(). With one buffer, call qog_video_wait()
 * before drawing the next frame. With two, alternate them: present(A),
 * draw B, present(B) -- which waits for A to finish -- draw A, ...
 */
#ifndef QOG_VIDEO_H
#define QOG_VIDEO_H
#include <stdbool.h>
#include <stdint.h>

#define QOG_LCD_W 320u
#define QOG_LCD_H 240u

typedef struct {
    uint16_t src_w, src_h;   /* game buffer size, src_w <= 320 */
    uint16_t out_h;          /* panel lines used, <= 240, centered; width is always 320 */
} qog_video_config_t;

typedef struct {
    uint32_t frames;          /* scanouts completed */
    uint32_t last_scan_us;    /* duration of the last scanout */
    uint32_t last_wait_us;    /* how long the last present()/wait() blocked core 0 */
} qog_video_stats_t;

/* Bring the panel up (blocking, ~150 ms), clear it, start core 1.
 * Returns false if the panel driver never reported ready. Call once. */
bool qog_video_init(const qog_video_config_t *cfg);

/* Palette changes take effect at the start of the next scanout, so a frame
 * never shows two palettes. */
void qog_video_set_palette_rgb(const uint8_t rgb[768]);
void qog_video_set_palette565(const uint16_t pal[256]);

/* Wait for any scanout in flight, then start scanning `fb`. Returns as soon
 * as core 1 has it. */
void qog_video_present(const uint8_t *fb);

/* Block until no scanout is in flight. */
void qog_video_wait(void);
bool qog_video_busy(void);

/* QuakeOG (Agent AK): present only the first `rows` panel lines (the rest of
 * the panel keeps what it shows), when src_h == out_h, else the whole frame.
 * qog_video_wait_rows returns once the scanout in flight has read the frame
 * buffer's first `rows` rows (each row is read once, into a line buffer, so
 * the game may draw over them), or when no scanout runs. */
void qog_video_present_rows(const uint8_t *fb, uint32_t rows);
/* QuakeOG (Agent AO): as present_rows, and the frame's first `packed` rows
 * are a Turbo view: row r holds src_w / 2 pixels at fb + r * src_w / 2, and
 * each pixel goes to the panel twice side by side (no CPU on core 0). Only
 * for a 320 wide frame whose rows and lines are one to one. */
void qog_video_present_packed(const uint8_t *fb, uint32_t rows, uint32_t packed);
void qog_video_wait_rows(uint32_t rows);

/* Optional work for core 1 to do while its line DMA runs (sound mixing in
 * Phase 5). Must return quickly: one line is ~100 us on the wire. */
void qog_video_set_core1_hook(void (*fn)(void));

/* QuakeOG (Agent V): run fn(arg) on core 1, which takes it between
 * scanouts (at once when none is in flight), and wait for it to finish.
 * One job at a time: join before the next start. Core 1 runs on the SDK's
 * 2 KB core 1 stack, so the job must be shallow. */
void qog_video_core1_start(void (*fn)(void *), void *arg);
void qog_video_core1_join(void);
/* QuakeOG (Agent AM): core 1's time in scanouts and in jobs since boot, us */
void qog_video_core1_times(uint32_t *scan_us, uint32_t *job_us);

qog_video_stats_t qog_video_stats(void);

/* Pure, host-testable: fill map[0..out_n) with the source index for each
 * output index, nearest-neighbour. */
void qog_video_build_map(uint16_t *map, uint16_t out_n, uint16_t src_n);

#endif
