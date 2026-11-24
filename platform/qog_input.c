#include "qog_input.h"

/* fwog_btn_id_t order, repeated so this file is host-testable without the BSP. */
enum { B_GRAY = 1u << 0, B_YELLOW = 1u << 1, B_GREEN = 1u << 2, B_BLUE = 1u << 3, B_RED = 1u << 4 };

/* Buttons by role, mapping v3 (qog_input.h). */
#define B_FIRE   B_GRAY      /* fire, and the modifier */
#define B_FWD    B_YELLOW    /* forward, with fire held back */
#define B_LEFT   B_GREEN     /* turn left, with fire held jump */
#define B_RIGHT  B_BLUE      /* turn right, with fire held down */
#define B_WEAP   B_RED       /* next weapon (tap), with fire held previous */

void qog_input_reset(qog_input_state_t *s) {
    s->prev_actions = 0;
    s->green_since_ms = 0;
    s->green_combo = false;
    s->menu_latched = false;
    s->fire_prev = false;
    s->fire_emitted = false;
    s->seen = 0;
}

qog_input_t qog_input_step(qog_input_state_t *s, uint8_t b, uint32_t now_ms) {
    uint32_t a = 0;
    const bool fire = (b & B_FIRE) != 0;
    const uint8_t others = b & (uint8_t)~B_FIRE;
    const uint8_t seen = (uint8_t)(s->seen | b);
    const bool was_combo = s->green_combo;
    bool tap = false;

    s->seen = 0;

    /* A tap: the fire button went down and came up again without ever being
       reported as held fire and without a combo. Either it was down at the
       last step and is up now, or it went down and up between two steps
       (only the polls in between saw it). */
    if (!fire && !s->fire_emitted && !was_combo &&
        (s->fire_prev || ((seen & B_FIRE) && !(seen & (uint8_t)~B_FIRE))))
        tap = true;

    /* Track the fire button's hold and whether it has become a combo. */
    if (!fire) {
        s->green_combo = false;
        s->green_since_ms = now_ms;
        s->fire_emitted = false;
    } else if (others) {
        s->green_combo = true;
    }

    /* Menu chord wins over turning, and latches until both are released so
       letting go of one does not leak a turn. */
    if ((b & (B_LEFT | B_RIGHT)) == (B_LEFT | B_RIGHT)) s->menu_latched = true;
    if (!(b & (B_LEFT | B_RIGHT))) s->menu_latched = false;

    if (s->menu_latched) {
        a |= QOG_ACT_BIT(QOG_ACT_MENU);
    } else if (fire) {
        /* Red is tap-only everywhere: the BSP powers off on 6 s of red
           LEVEL, combo or not (docs/lessons.md L6). */
        if (b & B_FWD)   a |= QOG_ACT_BIT(QOG_ACT_BACK);
        if (b & B_LEFT)  a |= QOG_ACT_BIT(QOG_ACT_JUMP);
        if (b & B_RIGHT) a |= QOG_ACT_BIT(QOG_ACT_DOWN);
        if (b & B_WEAP)  a |= QOG_ACT_BIT(QOG_ACT_PREV_WEAPON);
        if (!s->green_combo && (now_ms - s->green_since_ms) >= QOG_FIRE_GRACE_MS) {
            a |= QOG_ACT_BIT(QOG_ACT_FIRE);
            s->fire_emitted = true;
        }
    } else {
        if (b & B_FWD)   a |= QOG_ACT_BIT(QOG_ACT_FORWARD);
        if (b & B_LEFT)  a |= QOG_ACT_BIT(QOG_ACT_TURN_LEFT);
        if (b & B_RIGHT) a |= QOG_ACT_BIT(QOG_ACT_TURN_RIGHT);
        if (b & B_WEAP)  a |= QOG_ACT_BIT(QOG_ACT_NEXT_WEAPON);
    }
    s->fire_prev = fire;

    qog_input_t in = {0};
    in.down = a;
    in.pressed = a & ~s->prev_actions;
    in.released = s->prev_actions & ~a;
    if (tap) {
        /* one shot: down and up in the same step, in that order (the caller
           handles pressed before released); Quake's button keeps the
           "pressed this frame" bit, so the server still sees one attack */
        in.pressed |= QOG_ACT_BIT(QOG_ACT_FIRE);
        in.released |= QOG_ACT_BIT(QOG_ACT_FIRE);
    }
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
