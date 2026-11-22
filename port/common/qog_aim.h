/* Aiming help for button-only play (no mouse, no analog stick).
 *
 *   Crosshair on by default: id shipped it off, assuming mouse players.
 *   Fine aim: a turn starts slow so a tap nudges the view a few degrees,
 *   then ramps to full speed for big turns. Tunable from the console:
 *     qog_aim_slow  30    degrees/s for the first part of a turn
 *     qog_aim_fast  140   degrees/s once ramped up (id's cl_yawspeed default)
 *     qog_aim_hold  0.15  seconds at slow speed before the ramp starts
 *     qog_aim_ramp  0.25  seconds to go from slow to fast
 *   Set qog_aim_slow equal to qog_aim_fast to turn fine aim off.
 *
 * Works through id's own cvars and button state, so the engine is unchanged.
 */
#ifndef QOG_AIM_H
#define QOG_AIM_H

/* Register the cvars. Call during Host_Init (e.g. from VID_Init). */
void qog_aim_register(void);

/* Apply defaults that must win over config.cfg. Call once, right after the
 * first Host_Frame (that frame runs quake.rc and config.cfg). */
void qog_aim_apply_defaults(void);

/* Call once per frame, before Host_Frame. */
void qog_aim_update(double now);

#endif
