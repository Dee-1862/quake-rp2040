/* QuakeOG display app, Phase 2: platform test.
 *
 * Exercises everything Quake will need from the board, and measures it:
 *   - 8-bit frame -> palette -> scaled RGB565 -> LCD, fed by core 1
 *   - buttons (through fwog_power_poll, so the 6 s red hold still works)
 *     and tilt, mapped to game actions
 *   - the flash data region: header check at boot, full CRC if green is
 *     held at boot, and a still image from flash if the data image has
 *     lumps "testimg" (W*H bytes) and "testpal" (768 bytes).
 * Once a second it prints timings over the USB console (fw console).
 *
 * Change QOG_TEST_W/H to try 240x150 or 160x100; output is always 320x200.
 */
#include <string.h>
#include "fwog_display.h"
#include "pico/stdlib.h"
#include "qog_data.h"
#include "qog_input.h"
#include "qog_video.h"

#ifndef QOG_TEST_W
#define QOG_TEST_W 320
#endif
#ifndef QOG_TEST_H
#define QOG_TEST_H 200
#endif
#define OUT_H 200

FWOG_POWER_DEFAULT();

static uint8_t s_fb[QOG_TEST_W * QOG_TEST_H] __attribute__((aligned(4)));

/* 3x5 digits for the on-screen counter, one row per 3 bits. */
static const uint16_t k_digits[10] = {
    0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF,
};

static void draw_digit(int x, int y, int d, uint8_t c, int scale) {
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 3; col++)
            if (k_digits[d] & (1u << (14 - (row * 3 + col))))
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        const int px = x + col * scale + sx, py = y + row * scale + sy;
                        if (px >= 0 && px < QOG_TEST_W && py >= 0 && py < QOG_TEST_H)
                            s_fb[py * QOG_TEST_W + px] = c;
                    }
}

static void draw_number(int x, int y, unsigned v, uint8_t c, int scale) {
    char buf[10];
    int n = 0;
    do { buf[n++] = (char)(v % 10u); v /= 10u; } while (v && n < 10);
    while (n--) { draw_digit(x, y, buf[n], c, scale); x += 4 * scale; }
}

static void fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= QOG_TEST_H) continue;
        int x0 = x < 0 ? 0 : x, x1 = (x + w) > QOG_TEST_W ? QOG_TEST_W : x + w;
        if (x1 > x0) memset(&s_fb[j * QOG_TEST_W + x0], c, (size_t)(x1 - x0));
    }
}

/* A palette with a hue wheel in 0..191, greys in 192..255. */
static void make_test_palette(uint8_t rgb[768]) {
    for (int i = 0; i < 192; i++) {
        const int seg = i / 32, t = (i % 32) * 8;
        uint8_t r = 0, g = 0, b = 0;
        switch (seg) {
        case 0: r = 255; g = (uint8_t)t; break;
        case 1: r = (uint8_t)(255 - t); g = 255; break;
        case 2: g = 255; b = (uint8_t)t; break;
        case 3: g = (uint8_t)(255 - t); b = 255; break;
        case 4: r = (uint8_t)t; b = 255; break;
        default: r = 255; b = (uint8_t)(255 - t); break;
        }
        rgb[3 * i] = r; rgb[3 * i + 1] = g; rgb[3 * i + 2] = b;
    }
    for (int i = 192; i < 256; i++) {
        const uint8_t v = (uint8_t)((i - 192) * 4 + 3);
        rgb[3 * i] = rgb[3 * i + 1] = rgb[3 * i + 2] = v;
    }
}

int main(void) {
    board_init();
    const bool leds_ok = ws2812_init(pio0, 0u);   /* power-hold countdown */

    /* Data region first: a green hold at boot asks for the full CRC. */
    qog_data_status_t ds = qog_data_open();
    uint32_t verify_us = 0;
    if (ds == QOG_DATA_OK && !gpio_get(PIN_BTN_GREEN)) ds = qog_data_verify(&verify_us);

    const qog_video_config_t vc = { QOG_TEST_W, QOG_TEST_H, OUT_H };
    const bool video_ok = qog_video_init(&vc);
    /* Off by default: pressing an edge button tips the board, and at 40 fps
       that drift looked like the buttons misbehaving. -DQOG_TEST_TILT=ON. */
#if QOG_TEST_TILT
    const bool tilt_ok = qog_tilt_init();
#else
    const bool tilt_ok = false;
#endif

    uint8_t pal[768];
    make_test_palette(pal);
    const uint8_t *still = NULL;
    uint32_t sz = 0;
    if (ds == QOG_DATA_OK) {
        const uint8_t *img = qog_data_find("testimg", &sz);
        uint32_t psz = 0;
        const uint8_t *p = qog_data_find("testpal", &psz);
        if (img && sz == sizeof s_fb && p && psz == 768u) {
            still = img;
            memcpy(pal, p, 768);
        }
    }
    qog_video_set_palette_rgb(pal);

    qog_input_state_t ist;
    qog_input_reset(&ist);
    int box_x = QOG_TEST_W / 2, box_y = QOG_TEST_H / 2;
    uint32_t frame = 0, fps = 0, frames_this_sec = 0, render_us = 0, copy_us = 0;
    absolute_time_t next_report = make_timeout_time_ms(1000);
    absolute_time_t next_tilt = get_absolute_time();

    while (true) {
        const uint32_t now = to_ms_since_boot(get_absolute_time());
        const fwog_power_t pw = fwog_power_poll(now);
        qog_input_t in = qog_input_step(&ist, pw.buttons.down, now);
        if (tilt_ok && time_reached(next_tilt)) {
            next_tilt = make_timeout_time_ms(20);
            qog_tilt_sample(&in);
        }

        if (in.down & QOG_ACT_BIT(QOG_ACT_FORWARD))    box_y -= 3;
        if (in.down & QOG_ACT_BIT(QOG_ACT_BACK))       box_y += 3;
        if (in.down & QOG_ACT_BIT(QOG_ACT_TURN_LEFT))  box_x -= 3;
        if (in.down & QOG_ACT_BIT(QOG_ACT_TURN_RIGHT)) box_x += 3;
        box_x += in.tilt_x_mg / 64;
        box_y -= in.tilt_y_mg / 64;
        if (box_x < 0) box_x = 0;
        if (box_x > QOG_TEST_W - 1) box_x = QOG_TEST_W - 1;
        if (box_y < 0) box_y = 0;
        if (box_y > QOG_TEST_H - 1) box_y = QOG_TEST_H - 1;

        if (!video_ok) {
            if (time_reached(next_report)) {
                next_report = make_timeout_time_ms(1000);
                DIAG("[qog] VIDEO INIT FAILED btn=%02x act=%03lx data=%s tilt=%d\n",
                     pw.buttons.down, (unsigned long)in.down, qog_data_status_text(ds), (int)tilt_ok);
            }
            sleep_ms(2);
            continue;
        }

        /* Single buffer: wait for core 1 to finish reading it. */
        qog_video_wait();
        const uint64_t t0 = time_us_64();
        if (still) {
            memcpy(s_fb, still, sizeof s_fb);   /* also measures XIP read speed */
            copy_us = (uint32_t)(time_us_64() - t0);
        } else {
            /* Scrolling diagonal hue bands: touches every pixel, like a renderer. */
            for (int y = 0; y < QOG_TEST_H; y++) {
                uint8_t *row = &s_fb[y * QOG_TEST_W];
                const uint32_t base = (uint32_t)(y + (int)frame * 2);
                for (int x = 0; x < QOG_TEST_W; x++) row[x] = (uint8_t)((base + (uint32_t)x) % 192u);
            }
        }
        /* Make every action visible: weapon taps cycle the box colour,
           fire = red, menu = grey, jump hops the box, crouch shrinks it. */
        /* Greys (192..255 in the test palette) so the box never blends into
           the hue bands; outlined in black below. */
        static const uint8_t k_weapon_c[4] = { 255, 236, 216, 196 };
        static unsigned weapon;
        if (in.pressed & QOG_ACT_BIT(QOG_ACT_NEXT_WEAPON)) weapon = (weapon + 1u) & 3u;
        if (in.pressed & QOG_ACT_BIT(QOG_ACT_PREV_WEAPON)) weapon = (weapon + 3u) & 3u;
        uint8_t box_c = k_weapon_c[weapon];
        if (in.down & QOG_ACT_BIT(QOG_ACT_FIRE)) box_c = 0;
        if (in.down & QOG_ACT_BIT(QOG_ACT_MENU)) box_c = 220;
        const int hop = (in.down & QOG_ACT_BIT(QOG_ACT_JUMP)) ? 20 : 0;
        const int half = (in.down & QOG_ACT_BIT(QOG_ACT_DOWN)) ? 4 : 8;
        fill_rect(box_x - half - 1, box_y - half - hop - 1, 2 * half + 2, 2 * half + 2, 192);
        fill_rect(box_x - half, box_y - half - hop, 2 * half, 2 * half, box_c);
        fill_rect(0, 0, 60, 14, 192);
        draw_number(2, 2, fps, 255, 2);
        render_us = (uint32_t)(time_us_64() - t0);

        qog_video_present(s_fb);
        frame++;
        frames_this_sec++;

        if (time_reached(next_report)) {
            next_report = make_timeout_time_ms(1000);
            fps = frames_this_sec;
            frames_this_sec = 0;
            const qog_video_stats_t vs = qog_video_stats();
            DIAG("[qog] %ux%u fps=%u render=%uus scan=%uus wait=%uus copy=%uus "
                 "btn=%02x act=%03lx tilt=%d,%d data=%s verify=%uus leds=%d\n",
                 QOG_TEST_W, QOG_TEST_H, (unsigned)fps, (unsigned)render_us,
                 (unsigned)vs.last_scan_us, (unsigned)vs.last_wait_us, (unsigned)copy_us,
                 pw.buttons.down, (unsigned long)in.down, in.tilt_x_mg, in.tilt_y_mg,
                 qog_data_status_text(ds), (unsigned)verify_us, (int)leds_ok);
        }
    }
}
