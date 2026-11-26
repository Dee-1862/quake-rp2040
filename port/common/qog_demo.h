/* Demo playback host, id's client and renderer play a recorded demo with no
 * server, no QuakeC, no console and no sound (shared by the PC build and the
 * board).
 *
 * The engine starts the parts Host_Init starts for a client (command buffer
 * and commands, cvars, packs, wad, models, palette and colormap, video,
 * 2D drawing, screen, renderer, status bar, client), and each frame runs the
 * client half of id's _Host_Frame, rand, the frame time, the command buffer,
 * CL_ReadFromServer (which reads the demo through CL_GetMessage), the screen
 * and CL_DecayLights. The screen is id's SCR_UpdateScreen for a client in
 * the game view, V_RenderView, the centre print and the status bar, then
 * V_UpdatePalette and VID_Update. While id's screen would show the console
 * (before the signon ends) or the loading plaque, nothing is drawn.
 *
 *   qog_demo_init_host (&parms).
 *   qog_demo_start (demos, n, seed). The loop of demos, as startdemos
 *   for (;;)
 *       qog_demo_frame (frametime). 1 when a frame was presented
 *
 * Standalone on the PC, -qogdemo (port/pc/demo_pc.c). On the board, the
 * app pair apps/qogdemo. */
#ifndef QOG_DEMO_H
#define QOG_DEMO_H

/* include quakedef.h first (it has no include guard) */

#define QOG_DEMO_MAX	8

/* The engine start for a demo client. Never returns on failure (Sys_Error). */
void qog_demo_init_host (quakeparms_t *parms);

/* Plays the demos in order and then from the first again, as id's demo loop
   after "startdemos" (CL_NextDemo queues the first "playdemo"). The random
   sequence restarts from seed here (port/common/qog_rand.c), so a run can
   be compared with the PC game's "qog_demoloop <seed> <names>" (port/pc/
   demo_pc.c), which does the same between two frames. */
void qog_demo_start (const char **names, int count, unsigned seed);

/* Skips to the next demo of the loop at the start of the next frame. */
void qog_demo_next (void);

/* One host frame. frametime is what Host_FilterTime would give, a fixed
   step (host_framerate) or the real time since the last frame, which is
   clamped to 0.001 .. 0.1 s as id does. Returns 1 if a frame was drawn and
   presented with VID_Update, 0 if not, -1 if a demo ended in this frame
   (id's Host_EndGame, which also queues the next demo of the loop). */
int qog_demo_frame (double frametime, qboolean fixed);

/* Demos started so far (counted at the end of the frame that started one). */
int qog_demo_starts (void);

/* The demo playing now (its name), or "" between demos. */
const char *qog_demo_name (void);

/* Peaks since the start, client entities in use, visible entities in one
   frame, active particles in one frame, static entities, models known,
   temp entities and beams active in one frame, low hunk used. */
typedef struct
{
	int		usedents;		/* entity numbers with a baseline, an update or a model */
	int		entities, visedicts, particles, statics, models, tempents, beams, dlights;
	int		surfs, edges;	/* most edge-drawing surfaces and edges in one frame */
	int		hunk_low;
	int		frames;		/* frames presented */
} qog_demo_peaks_t;

const qog_demo_peaks_t *qog_demo_peaks (void);
void qog_demo_reset_peaks (void);

/* Active particles now (walks the list). */
int qog_demo_particles (void);

#endif
