/* Demo playback host. See qog_demo.h.
 *
 * Copyright (C) 2026 QuakeOG contributors. GPL v2 or later, like id's code
 * it calls. */
#include <setjmp.h>
#include <string.h>
#include "quakedef.h"
#include "r_local.h"
#include "qog_rand.h"
#include "qog_demo.h"

extern particle_t	*active_particles, *particles;
extern cvar_t	scr_fov;
extern qboolean	scr_initialized;
extern float	scr_disabled_time;
extern float	oldscreensize, oldfov;
extern int		hunk_low_used;
extern int		host_hunklevel;
extern double	oldrealtime;
extern jmp_buf	host_abortserver;
extern int		clearconsole, clearnotify;
extern int		num_temp_entities;
extern int		r_qog_peaksurfs, r_qog_peakedges;	// r_main.c, R_EdgeDrawing
extern cvar_t	sv_gravity;		// sv_phys.c, read by R_DrawParticles
float CalcFov (float fov_x, float width, float height);	// screen.c
void SCR_DrawRam (void);
void SCR_DrawNet (void);
void SCR_DrawTurtle (void);
void SCR_DrawPause (void);
void SCR_CheckDrawCenterString (void);
void SCR_EraseCenterString (void);
void CL_DecayLights (void);	// cl_main.c
void Mod_Init (void);		// model.c

static qog_demo_peaks_t	demo_peaks;
static int		demo_skip;
static int		demo_starts;
static qboolean	demo_wasplaying;

/* ---- engine start ------------------------------------------------------ */

void qog_demo_init_host (quakeparms_t *parms)
{
	host_parms = *parms;
	com_argc = parms->argc;
	com_argv = parms->argv;

	// Host_Init's order, without the server, QuakeC, network, console,
	// keys, menus and sound (S_ functions do nothing until S_Init runs)
	Memory_Init (parms->membase, parms->memsize);
	Cbuf_Init ();
	Cmd_Init ();
	V_Init ();
	Chase_Init ();
	COM_Init (parms->basedir);
	W_LoadWadFile ("gfx.wad");
	Mod_Init ();
	R_InitTextures ();

	// palette and colormap read in place when the pack is mapped
	host_basepal = COM_FlashFile ("gfx/palette.lmp", NULL);
	if (!host_basepal)
		host_basepal = (byte *)COM_LoadHunkFile ("gfx/palette.lmp");
	host_colormap = COM_FlashFile ("gfx/colormap.lmp", NULL);
	if (!host_colormap)
		host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp");
	if (!host_basepal || !host_colormap)
		Sys_Error ("qog_demo: no gfx/palette.lmp or gfx/colormap.lmp");

	VID_Init (host_basepal);
	Draw_Init ();
	SCR_Init ();
	R_Init ();
	Sbar_Init ();
	CL_Init ();
	// SV_Init registers it in the game, and the client's particles fall
	// by it (r_part.c)
	Cvar_RegisterVariable (&sv_gravity);

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();
	host_initialized = true;
	Sys_Printf ("qog_demo: engine started, hunk low %d\n", hunk_low_used);
}

void qog_demo_start (const char **names, int count, unsigned seed)
{
	int		i;

	if (count > MAX_DEMOS)
		count = MAX_DEMOS;
	memset (cls.demos, 0, sizeof(cls.demos));
	for (i = 0 ; i < count ; i++)
		Q_strncpy (cls.demos[i], (char *)names[i], sizeof(cls.demos[0]) - 1);
	// as "startdemos", the loop starts with the first demo, whose
	// "playdemo" runs in the next frame's command buffer
	qog_srand (seed);
	cls.demonum = 0;
	CL_NextDemo ();
}

void qog_demo_next (void)
{
	demo_skip = 1;
}

const char *qog_demo_name (void)
{
	static char	name[16];
	int			n;

	if (!cls.demoplayback || cls.demonum < 1)
		return "";
	n = cls.demonum - 1;	// CL_NextDemo has moved on to the next one
	Q_strncpy (name, cls.demos[n], sizeof(name) - 1);
	return name;
}

/* ---- the screen -------------------------------------------------------- */

/* SCR_CalcRefdef (static in screen.c), unchanged. */
static void demo_calc_refdef (void)
{
	vrect_t		vrect;
	float		size;

	scr_fullupdate = 0;		// force a background redraw
	vid.recalc_refdef = 0;
	Sbar_Changed ();

	if (scr_viewsize.value < 30)
		Cvar_Set ("viewsize","30");
	if (scr_viewsize.value > 120)
		Cvar_Set ("viewsize","120");
#ifdef QOG_MAX_VIEWSIZE
	if (scr_viewsize.value > QOG_MAX_VIEWSIZE)
		Cvar_SetValue ("viewsize", QOG_MAX_VIEWSIZE);
#endif
	if (scr_fov.value < 10)
		Cvar_Set ("fov","10");
	if (scr_fov.value > 170)
		Cvar_Set ("fov","170");

	r_refdef.fov_x = scr_fov.value;
	r_refdef.fov_y = CalcFov (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

	if (cl.intermission)
		size = 120;
	else
		size = scr_viewsize.value;
#ifdef QOG_MAX_VIEWSIZE
	if (size > QOG_MAX_VIEWSIZE)
		size = QOG_MAX_VIEWSIZE;
#endif
	if (size >= 120)
		sb_lines = 0;
	else if (size >= 110)
		sb_lines = 24;
	else
		sb_lines = 24+16+8;

	vrect.x = 0;
	vrect.y = 0;
	vrect.width = vid.width;
	vrect.height = vid.height;
	R_SetVrect (&vrect, &scr_vrect, sb_lines);
	R_ViewChanged (&vrect, sb_lines, vid.aspect);
}

/* id's SCR_UpdateScreen for a client with no console and no menu. Where
   id's would draw the console (forced up until the signon ends, with
   scr_conspeed high enough to be gone in one frame once it ends) nothing is
   drawn, and the console's tile clear runs on the first frame after, as
   SCR_SetUpToDrawConsole does. The notify lines are not drawn (id's with
   con_notifytime below 0). Returns true if the frame was presented. */
static qboolean demo_update_screen (void)
{
	static float	oldscr_viewsize;
	static float	oldlcd_x;
	vrect_t		vrect;

	if (scr_skipupdate || block_drawing)
		return false;

	scr_copytop = 0;
	scr_copyeverything = 0;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
			return false;
	}

	if (!scr_initialized)
		return false;

	if (!cl.worldmodel || cls.signon != SIGNONS)
	{	// id's console is forced up, SCR_DrawConsole clears clearconsole
		clearconsole = 0;
		return false;
	}

	if (scr_viewsize.value != oldscr_viewsize)
	{
		oldscr_viewsize = scr_viewsize.value;
		vid.recalc_refdef = 1;
	}
	if (oldfov != scr_fov.value)
	{
		oldfov = scr_fov.value;
		vid.recalc_refdef = true;
	}
	if (oldlcd_x != lcd_x.value)
	{
		oldlcd_x = lcd_x.value;
		vid.recalc_refdef = true;
	}
	if (oldscreensize != scr_viewsize.value)
	{
		oldscreensize = scr_viewsize.value;
		vid.recalc_refdef = true;
	}
	if (vid.recalc_refdef)
		demo_calc_refdef ();

	D_EnableBackBufferAccess ();

	if (scr_fullupdate++ < vid.numpages)
	{	// clear the entire screen
		scr_copyeverything = 1;
		Draw_TileClear (0,0,vid.width,vid.height);
		Sbar_Changed ();
	}

	pconupdate = NULL;

	// SCR_SetUpToDrawConsole with no console lines
	if (clearconsole++ < vid.numpages)
	{
		scr_copytop = 1;
		Draw_TileClear (0, 0, vid.width, vid.height);
		Sbar_Changed ();
	}
	else
		clearnotify++;		// no notify lines to clear
	SCR_EraseCenterString ();

	D_DisableBackBufferAccess ();
	VID_LockBuffer ();
	V_RenderView ();
	VID_UnlockBuffer ();
	D_EnableBackBufferAccess ();

	if (cl.intermission == 1 && key_dest == key_game)
	{
		Sbar_IntermissionOverlay ();
	}
	else if (cl.intermission == 2 && key_dest == key_game)
	{
		Sbar_FinaleOverlay ();
		SCR_CheckDrawCenterString ();
	}
	else if (cl.intermission == 3 && key_dest == key_game)
	{
		SCR_CheckDrawCenterString ();
	}
	else
	{
		SCR_DrawRam ();
		SCR_DrawNet ();
		SCR_DrawTurtle ();
		SCR_DrawPause ();
		SCR_CheckDrawCenterString ();
		Sbar_Draw ();
	}

	D_DisableBackBufferAccess ();
	V_UpdatePalette ();

	if (scr_copyeverything)
	{
		vrect.x = 0;
		vrect.y = 0;
		vrect.width = vid.width;
		vrect.height = vid.height;
	}
	else if (scr_copytop)
	{
		vrect.x = 0;
		vrect.y = 0;
		vrect.width = vid.width;
		vrect.height = vid.height - sb_lines;
	}
	else
	{
		vrect.x = scr_vrect.x;
		vrect.y = scr_vrect.y;
		vrect.width = scr_vrect.width;
		vrect.height = scr_vrect.height;
	}
	vrect.pnext = 0;
	VID_Update (&vrect);
	return true;
}

/* ---- frames ------------------------------------------------------------ */

int qog_demo_particles (void)
{
	particle_t	*p;
	int			n = 0;

	for (p = active_particles ; p ; )
	{
		n++;
#ifdef QOG_PARTICLE_PACKED	// the link is an index + 1 (r_part.c)
		p = p->next ? particles + p->next - 1 : NULL;
#else
		p = p->next;
#endif
	}
	return n;
}

static void demo_note_peaks (void)
{
	int		i, n;

#define PEAK(f, v)	do { if ((v) > demo_peaks.f) demo_peaks.f = (v); } while (0)
	PEAK (entities, cl.num_entities);
	for (i = 0, n = 0 ; i < cl.num_entities ; i++)	// entity numbers ever given a model or an update
	{
		entity_t	*e = CL_ENTITY_IFANY (i);

		if (e && (e->baseline.modelindex || e->msgtime || e->model))
			n++;
	}
	PEAK (usedents, n);
	PEAK (visedicts, cl_numvisedicts);
	PEAK (particles, qog_demo_particles ());
	PEAK (statics, cl.num_statics);
	PEAK (tempents, num_temp_entities);
	PEAK (hunk_low, hunk_low_used);
	PEAK (surfs, r_qog_peaksurfs);
	PEAK (edges, r_qog_peakedges);
	for (i = 1, n = 0 ; i < MAX_MODELS && cl.model_precache[i] ; i++)
		n++;
	PEAK (models, n);
	for (i = 0, n = 0 ; i < MAX_BEAMS ; i++)
		if (cl_beams[i].model && cl_beams[i].endtime >= cl.time)
			n++;
	PEAK (beams, n);
	for (i = 0, n = 0 ; i < MAX_DLIGHTS ; i++)
		if (cl_dlights[i].die >= cl.time && cl_dlights[i].radius > 0)
			n++;
	PEAK (dlights, n);
#undef PEAK
}

int qog_demo_starts (void)
{
	return demo_starts;
}

const qog_demo_peaks_t *qog_demo_peaks (void)
{
	return &demo_peaks;
}

void qog_demo_reset_peaks (void)
{
	memset (&demo_peaks, 0, sizeof(demo_peaks));
	r_qog_peaksurfs = r_qog_peakedges = 0;
}

/* The client half of id's _Host_Frame. */
int qog_demo_frame (double frametime, qboolean fixed)
{
	int		drawn;

	if (setjmp (host_abortserver))
	{	// Host_EndGame at the end of a demo, id's frame stops there too
		demo_wasplaying = false;
		return -1;
	}

	// keep the random time dependent (the engine's rand is qog_rand, a
	// define this file is not built with)
	qog_rand ();

	// Host_FilterTime without its frame rate cap (the caller paces frames)
	realtime += frametime;
	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;
	if (fixed)
		host_frametime = (float)frametime;	// as host_framerate, a float cvar
	else
	{
		if (host_frametime > 0.1)
			host_frametime = 0.1;
		if (host_frametime < 0.001)
			host_frametime = 0.001;
	}

	if (demo_skip)
	{
		demo_skip = 0;
		if (cls.demoplayback)
		{
			CL_Disconnect ();
			CL_NextDemo ();
		}
	}

	Cbuf_Execute ();

	// CL_SendCmd, in a demo the client sends nothing and only empties its
	// reliable message (the view angles it adjusts are replaced by the
	// demo's in CL_RelinkEntities)
	if (cls.state == ca_connected && cls.demoplayback)
		SZ_Clear (&cls.message);

	if (cls.state == ca_connected)
		CL_ReadFromServer ();

	drawn = demo_update_screen ();

	// S_Update does nothing without sound
	if (cls.signon == SIGNONS)
		CL_DecayLights ();

	host_framecount++;
	if (cls.demoplayback && !demo_wasplaying)
		demo_starts++;
	demo_wasplaying = cls.demoplayback;
	demo_note_peaks ();
	demo_peaks.frames += drawn;
	return drawn;
}
