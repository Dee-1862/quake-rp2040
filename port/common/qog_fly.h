/* Renderer-only fly-through of a map (shared by the PC build and the board).
 *
 * No server, no client, no QuakeC and no sound, the world model is loaded
 * with id's Mod_ForName (so a baked map is read in place from the image),
 * every light style is 'm', and a camera moves along a closed spline
 * through spots taken from the map's entity lump (info_player_start, then
 * items, weapons and monsters, spots whose eye is in solid skipped, as
 * port/pc/tour_pc.c does). Each frame is drawn by id's R_RenderView.
 *
 * Standalone use (the board app apps/qogfly and the PC's -qogfly).
 *
 *   qog_fly_init_host (&parms). memory, packs, palette, video, R_Init
 *   qog_fly_load ("e1m1"). World model, R_NewMap, light styles, path
 *   for (;;) {
 *       qog_fly_camera (t, feet, angles).
 *       qog_fly_render (feet, angles, t).
 *       qog_fly_overlay (...). Text in the status bar rows
 *       VID_Update (NULL).
 *   }
 *
 * Inside the full PC game (port/pc/fly_pc.c) only qog_fly_use_world,
 * qog_fly_setup_view, qog_fly_camera and qog_fly_render are used, on the
 * world the game loaded, so the two can be compared frame by frame. */
#ifndef QOG_FLY_H
#define QOG_FLY_H

/* include quakedef.h first (it has no include guard) */

#define QOG_FLY_MAX_POINTS	128
#define QOG_FLY_EYE			22		/* player view height (view_ofs, cl.viewheight) */

/* Renderer-only engine start, what Host_Init does for the renderer and no
   more. Never returns on failure (Sys_Error). */
void qog_fly_init_host (quakeparms_t *parms);

/* Standalone, load maps/<name>.bsp as cl.worldmodel and get it ready to draw. */
void qog_fly_load (const char *name);

/* Use cl.worldmodel as it is (already loaded by the game or qog_fly_load).
   light styles to 'm', path from its entity lump. Returns the point count. */
int qog_fly_use_world (void);

/* r_refdef for a 320x152 view above a 48-row status bar, fov 90, the way
   SCR_CalcRefdef sets it with viewsize 100. */
void qog_fly_setup_view (void);

/* Every light style 'm' (normal light). */
void qog_fly_lightstyles_normal (void);

/* Path, number of points, the feet origin of point i, the time the camera
   passes point i, the whole loop's length in seconds. */
int qog_fly_numpoints (void);
void qog_fly_point (int i, vec3_t feet);
float qog_fly_point_time (int i);
float qog_fly_loop_time (void);

/* Camera at time t (seconds, wraps around the loop), feet origin, view
   angles, and the index of the point it last passed. */
int qog_fly_camera (double t, vec3_t feet, vec3_t angles);

/* View angles at point i (the spline's direction there). */
void qog_fly_point_angles (int i, vec3_t angles);

/* Draw the world, the eye is feet + 22 + 1/32 on each axis, exactly as
   V_CalcRefdef places a standing, still player. cl.time = time. */
void qog_fly_render (const vec3_t feet, const vec3_t angles, double time);

/* FNV-1a hash of the 3D view rows of the frame buffer (0 to 151). */
unsigned qog_fly_hash_view (void);

/* Text in the status bar rows (below the view). */
void qog_fly_overlay (const char *line1, const char *line2);

#endif
