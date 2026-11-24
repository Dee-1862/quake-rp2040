/* Five buttons (+ optional tilt) -> game actions.
 *
 * Takes the fwog_buttons_t that fwog_power_poll() returned this iteration.
 * Never call fwog_buttons_poll() yourself: the BSP's 6 s red-hold power-off
 * depends on that single poll seeing every edge.
 *
 * Mapping (v3, the user's layout on the board):
 *   alone:          yellow=forward green=turn L blue=turn R red=next weapon (tap) gray=fire
 *   gray held +:    yellow=back green=jump blue=crouch/swim down red=prev weapon (tap)
 *   green + blue:   menu
 * Nothing that is held for long is on red: the BSP powers off on 6 s of red.
 * Gray is both FIRE and the modifier: held fire asserts only once gray has
 * been down QOG_FIRE_GRACE_MS with no other button, and a combo cancels it.
 * A short tap (released before that, or pressed and released between two
 * steps, which the board's ~140 ms game frames made the usual case) gives one
 * press and release of FIRE in the same step, which Quake turns into one shot.
 */
#ifndef QOG_INPUT_H
#define QOG_INPUT_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    QOG_ACT_FORWARD = 0,
    QOG_ACT_BACK,
    QOG_ACT_TURN_LEFT,
    QOG_ACT_TURN_RIGHT,
    QOG_ACT_FIRE,
    QOG_ACT_JUMP,
    QOG_ACT_DOWN,
    QOG_ACT_PREV_WEAPON,
    QOG_ACT_NEXT_WEAPON,
    QOG_ACT_MENU,
    QOG_ACT_COUNT
} qog_action_t;

#define QOG_ACT_BIT(a) (1u << (unsigned)(a))
#define QOG_FIRE_GRACE_MS 60u

typedef struct {
    uint32_t down;       /* action bits held this frame */
    uint32_t pressed;    /* action bits that started this frame */
    uint32_t released;   /* action bits that ended this frame */
    int16_t  tilt_x_mg;  /* calibrated, dead-zoned; 0 when tilt is off */
    int16_t  tilt_y_mg;
} qog_input_t;

typedef struct {
    uint32_t prev_actions;
    uint32_t green_since_ms;  /* when the fire button went down (it was green in v2) */
    bool     green_combo;     /* another button joined the fire button this hold */
    bool     menu_latched;    /* turn L + turn R seen; suppress turns until both up */
    bool     fire_prev;       /* the fire button was down at the last step */
    bool     fire_emitted;    /* FIRE was reported held during this press */
    uint8_t  seen;            /* buttons that went down since the last step; the
                                 caller ORs in every poll between steps */
} qog_input_state_t;

/* Pure, host-testable. `btn_down` is a bit per fwog_btn_id_t
 * (gray=0 yellow=1 green=2 blue=3 red=4). */
void qog_input_reset(qog_input_state_t *s);
qog_input_t qog_input_step(qog_input_state_t *s, uint8_t btn_down, uint32_t now_ms);

#ifndef HOST_TEST
/* Tilt: LIS3DH over I2C. Optional; call init once, then sample at ~50 Hz.
 * Zeroes against the board's attitude at init time. */
bool qog_tilt_init(void);
void qog_tilt_sample(qog_input_t *in);
#endif

#endif
