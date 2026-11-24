/* QuakeOG: the display client's power poll (FWOG_POWER_CUSTOM).
 *
 * The BSP's fwog_power_poll() powers the board off after red has been held
 * for FWOG_SHIP_HOLD_MS (6 s), counting red whatever else is held. In the
 * game red is the combo button (red + yellow walks backwards, which can last
 * longer than 6 s), so this poll feeds the same hold machine red only while it
 * is the ONLY button down. Everything else is the BSP's: its debounced button
 * poll, its hold machine and power-off call, and the same red LED countdown
 * on the WS2812 bar, restored when the hold is let go.
 *
 * Call once per main-loop iteration, as fwog_power_poll would be. */
#include "fwog_display.h"
#include "power/power_poll.h"
#include "power/ship_mode.h"
#include "leds/ws2812_driver.h"
#include "common/diag.h"
#include "qog_power.h"

static fwog_ship_state_t s_ship;
static uint8_t  s_saved[FWOG_LED_COUNT][3];
static bool     s_saved_ok;
static uint32_t s_next_draw;

static void countdown_draw(unsigned progress) {
    unsigned lit = (progress * FWOG_LED_COUNT + 99u) / 100u;
    if (lit < 1u) lit = 1u;
    for (unsigned i = 0u; i < FWOG_LED_COUNT; i++)
        ws2812_set_color(i, i < lit ? 64u : 0u, 0u, 0u);
    ws2812_process();
}

static void leds_save(void) {
    for (unsigned i = 0u; i < FWOG_LED_COUNT; i++)
        if (!ws2812_get_color(i, &s_saved[i][0], &s_saved[i][1], &s_saved[i][2]))
            s_saved[i][0] = s_saved[i][1] = s_saved[i][2] = 0u;
    s_saved_ok = true;
}

static void leds_restore(void) {
    if (!s_saved_ok) return;
    for (unsigned i = 0u; i < FWOG_LED_COUNT; i++)
        ws2812_set_color(i, s_saved[i][0], s_saved[i][1], s_saved[i][2]);
    ws2812_process();
    s_saved_ok = false;
}

fwog_power_t qog_power_poll(uint32_t now_ms) {
    const fwog_buttons_t b = fwog_buttons_poll(now_ms);
    const uint32_t red_bit = FWOG_BTN_BIT(FWOG_BTN_RED);
    /* red counts toward power off only while nothing else is held */
    const bool red_alone = b.down == red_bit;

    const fwog_ship_phase_t was = s_ship.phase;
    const fwog_ship_phase_t ev = fwog_ship_step(&s_ship, red_alone, now_ms);

    if (ws2812_ready()) {
        if (was == FWOG_SHIP_IDLE && ev == FWOG_SHIP_ARMING) {
            leds_save();
            s_next_draw = now_ms;
        } else if (was != FWOG_SHIP_IDLE && ev == FWOG_SHIP_IDLE) {
            leds_restore();
        }
        if (s_ship.phase != FWOG_SHIP_IDLE && (int32_t)(now_ms - s_next_draw) >= 0) {
            s_next_draw = now_ms + 50u;
            countdown_draw(fwog_ship_progress(&s_ship, now_ms));
        }
    }

    /* the step's return value, never the phase field (see ship_mode.h) */
    if (ev == FWOG_SHIP_FIRED) {
        DIAG("OK ship entering (red held alone %u ms)\n", FWOG_SHIP_HOLD_MS);
        if (!fwog_ship_enter())
            DIAG("ERR ship charger-write-failed\n");
    }

    fwog_power_t out;
    out.buttons  = b;
    out.progress = fwog_ship_progress(&s_ship, now_ms);
    out.armed    = s_ship.phase != FWOG_SHIP_IDLE;
    return out;
}
