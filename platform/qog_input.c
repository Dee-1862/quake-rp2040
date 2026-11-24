#include "qog_input.h"

/* fwog_btn_id_t order, repeated so this file is host-testable without the BSP. */
enum { B_GRAY = 1u << 0, B_YELLOW = 1u << 1, B_GREEN = 1u << 2, B_BLUE = 1u << 3, B_RED = 1u << 4 };

/* Buttons by role, mapping v4 (qog_input.h). */
#define B_FIRE   B_GRAY      /* fire only */
#define B_FWD    B_YELLOW    /* forward, with red held back */
#define B_LEFT   B_GREEN     /* turn left, with red held jump */
#define B_RIGHT  B_BLUE      /* turn right, with red held down */
#define B_MOD    B_RED       /* the combo button; tapped alone, next weapon */

void qog_input_reset(qog_input_state_t *s) {
    s->prev_actions = 0;
    s->green_since_ms = 0;
    s->green_combo = false;
    s->menu_latched = false;
    s->fire_prev = false;
    s->fire_emitted = false;
    s->red_prev = false;
    s->red_combo = false;
    s->red_since_ms = 0;
    s->seen = 0;
}

qog_input_t qog_input_step(qog_input_state_t *s, uint8_t b, uint32_t now_ms) {
    uint32_t a = 0;
    const bool fire = (b & B_FIRE) != 0;
    const bool red = (b & B_MOD) != 0;
    const uint8_t seen = (uint8_t)(s->seen | b);
    uint32_t taps = 0;

    s->seen = 0;

    /* Fire tapped between two steps: down and up again where only the polls
       in between saw it. One shot, unless red was part of it (a combo). */
    if (!fire && !s->fire_prev && (seen & B_FIRE) && !(seen & B_MOD))
        taps |= QOG_ACT_BIT(QOG_ACT_FIRE);

    /* Red: the combo button. Its own action, next weapon, fires on release
       when no other button joined it and it was not a long hold. */
    if (red) {
        if (!s->red_prev) { s->red_since_ms = now_ms; s->red_combo = false; }
        if (b & (uint8_t)~B_MOD) s->red_combo = true;
    } else {
        bool tapped = s->red_prev && !s->red_combo && (now_ms - s->red_since_ms) < QOG_RED_TAP_MS;
        /* or a red tap that came and went between two steps */
        if (!s->red_prev && (seen & B_MOD) && !(seen & (uint8_t)~B_MOD)) tapped = true;
        if (tapped) taps |= QOG_ACT_BIT(QOG_ACT_NEXT_WEAPON);
        s->red_combo = false;
    }

    /* Menu chord wins over turning, and latches until both are released so
       letting go of one does not leak a turn. */
    if ((b & (B_LEFT | B_RIGHT)) == (B_LEFT | B_RIGHT)) s->menu_latched = true;
    if (!(b & (B_LEFT | B_RIGHT))) s->menu_latched = false;

    if (s->menu_latched) {
        a |= QOG_ACT_BIT(QOG_ACT_MENU);
    } else if (red) {
        if (b & B_FWD)   a |= QOG_ACT_BIT(QOG_ACT_BACK);
        if (b & B_LEFT)  a |= QOG_ACT_BIT(QOG_ACT_JUMP);
        if (b & B_RIGHT) a |= QOG_ACT_BIT(QOG_ACT_DOWN);
        if (b & B_FIRE)  a |= QOG_ACT_BIT(QOG_ACT_PREV_WEAPON);
    } else {
        if (b & B_FWD)   a |= QOG_ACT_BIT(QOG_ACT_FORWARD);
        if (b & B_LEFT)  a |= QOG_ACT_BIT(QOG_ACT_TURN_LEFT);
        if (b & B_RIGHT) a |= QOG_ACT_BIT(QOG_ACT_TURN_RIGHT);
        if (b & B_FIRE)  a |= QOG_ACT_BIT(QOG_ACT_FIRE);
    }
    s->fire_prev = fire;
    s->red_prev = red;

    qog_input_t in = {0};
    in.down = a;
    in.pressed = a & ~s->prev_actions;
    in.released = s->prev_actions & ~a;
    /* A tap is down and up in the same step, in that order (callers handle
       pressed before released). Quake keeps the button's "pressed this
       frame" bit, so the server still sees one attack or one impulse. */
    taps &= ~a;
    in.pressed |= taps;
    in.released |= taps;
    s->prev_actions = a;
    return in;
}

#ifndef HOST_TEST
#include "fwog_display.h"

#define TILT_DEADZONE_MG 80
static bool s_tilt_ok;
static int32_t s_zero_x, s_zero_y;

bool qog_tilt_init(void) {
    lis3dh_init();
    s_tilt_ok = lis3dh_configure(LIS3DH_RANGE_2G);
    if (!s_tilt_ok) {
        DIAG("[qog_input] LIS3DH not found; tilt off\n");
        return false;
    }
    /* lis3dh_process() returns false until a fresh sample is ready (100 Hz),
       and a single early try left the zero unset on the board. Average 8. */
    lis3dh_sample_t smp;
    lis3dh_motion_t mot;
    int32_t sx = 0, sy = 0, n = 0;
    const absolute_time_t until = make_timeout_time_ms(300);
    while (n < 8 && !time_reached(until)) {
        if (lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &smp, &mot)) {
            sx += lis3dh_raw_to_mg(smp.x, LIS3DH_RANGE_2G);
            sy += lis3dh_raw_to_mg(smp.y, LIS3DH_RANGE_2G);
            n++;
        }
        sleep_ms(5);
    }
    if (n) { s_zero_x = sx / n; s_zero_y = sy / n; }
    DIAG("[qog_input] tilt zero from %d samples: %ld,%ld mg\n", (int)n, (long)s_zero_x, (long)s_zero_y);
    return true;
}

static int16_t deadzone(int32_t v) {
    if (v > TILT_DEADZONE_MG) return (int16_t)(v - TILT_DEADZONE_MG);
    if (v < -TILT_DEADZONE_MG) return (int16_t)(v + TILT_DEADZONE_MG);
    return 0;
}

void qog_tilt_sample(qog_input_t *in) {
    if (!s_tilt_ok) return;
    lis3dh_sample_t smp;
    lis3dh_motion_t mot;
    if (!lis3dh_process(LIS3DH_MOVE_THRESHOLD_DEFAULT, &smp, &mot)) return;
    in->tilt_x_mg = deadzone(lis3dh_raw_to_mg(smp.x, LIS3DH_RANGE_2G) - s_zero_x);
    in->tilt_y_mg = deadzone(lis3dh_raw_to_mg(smp.y, LIS3DH_RANGE_2G) - s_zero_y);
}
#endif
