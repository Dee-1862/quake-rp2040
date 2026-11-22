#include "qog_aim.h"
#include "quakedef.h"

extern kbutton_t in_left, in_right;   /* cl_input.c globals, not in client.h */

static cvar_t aim_slow = {"qog_aim_slow", "30", true};
static cvar_t aim_fast = {"qog_aim_fast", "140", true};
static cvar_t aim_hold = {"qog_aim_hold", "0.15", true};
static cvar_t aim_ramp = {"qog_aim_ramp", "0.25", true};

static double s_turn_start = -1.0;

void qog_aim_register(void) {
    Cvar_RegisterVariable(&aim_slow);
    Cvar_RegisterVariable(&aim_fast);
    Cvar_RegisterVariable(&aim_hold);
    Cvar_RegisterVariable(&aim_ramp);
}

void qog_aim_apply_defaults(void) {
    /* Called after the first Host_Frame, once quake.rc and config.cfg have
       run, so a saved "crosshair 0" cannot win. Queuing it with Cbuf_AddText
       was fragile: it sat behind everything quake.rc stuffs in. Players can
       still type `crosshair 0` for the session; it just starts on. */
    Cvar_Set("crosshair", "1");
}

void qog_aim_update(double now) {
    const qboolean turning = (in_left.state & 1) || (in_right.state & 1);
    if (!turning) {
        s_turn_start = -1.0;
        cl_yawspeed.value = aim_fast.value;   /* full speed for anything else that reads it */
        return;
    }
    if (s_turn_start < 0.0) s_turn_start = now;
    const double t = now - s_turn_start;
    double speed = aim_slow.value;
    if (t > aim_hold.value) {
        const double k = aim_ramp.value > 0.0 ? (t - aim_hold.value) / aim_ramp.value : 1.0;
        speed = aim_slow.value + (aim_fast.value - aim_slow.value) * (k > 1.0 ? 1.0 : k);
    }
    /* Written straight into the value: CL_AdjustAngles reads .value, and
       Cvar_SetValue would churn the zone with a string every frame. */
    cl_yawspeed.value = (float)speed;
}
