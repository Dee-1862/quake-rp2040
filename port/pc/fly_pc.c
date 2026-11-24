/* Fly-through on the PC (development and test tool). See port/common/qog_fly.h.
 *
 *   quakeog_pc.exe -qogfly [-flymap e1m1] [-flyframes 200] [-flydt 0.1]
 *                  [-flyshots 0]
 *       The renderer-only host the board app apps/qogfly runs: no server,
 *       client or QuakeC. Draws -flyframes frames, the camera -flydt
 *       seconds further each frame (cl.time too), prints
 *       "fly <frame> <view hash> <point>" for each and the hunk use, writes
 *       a shot n_fly_<frame> every -flyshots frames (0 none), then exits.
 *
 *   qog_fly [frames] [dt] [shots]      (console, with a map loaded)
 *       The same frames drawn on the world the full game loaded, with
 *       r_drawentities 0 and every light style 'm'. Prints the same lines,
 *       shots n_flyg_<frame>. Equal lines mean the renderer-only host draws
 *       the world exactly as the game's own state does.
 *
 *   qog_fly_ref                        (console, with a map loaded)
 *       Puts the player at every path point in turn (noclip, god, notarget,
 *       not solid so it picks nothing up, not on ground), looking the way the path does there (angles as the
 *       protocol rounds them), with r_drawentities 0, r_drawviewmodel 0,
 *       crosshair 0 and light styles 'm'. After the game has drawn the
 *       spot through its normal pipeline (V_RenderView), the fly host's
 *       camera draws the same spot at the same cl.time, and the two 3D views
 *       are compared: "flyref <point> <game hash> <fly hash> same|DIFF
 *       <pixels>". Quits when done. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quakedef.h"
#include "qog_fly.h"
#include "qog_mem.h"
#include "qog_phase.h"

extern mleaf_t	*r_viewleaf;

extern byte	*hunk_base;
extern int	hunk_low_used, hunk_high_used;

static int fly_argi (const char *name, int def)
{
	int	i = COM_CheckParm ((char *)name);
	return (i && i + 1 < com_argc) ? Q_atoi (com_argv[i + 1]) : def;
}

static float fly_argf (const char *name, float def)
{
	int	i = COM_CheckParm ((char *)name);
	return (i && i + 1 < com_argc) ? Q_atof (com_argv[i + 1]) : def;
}

/* One deterministic frame: camera at k * dt, cl.time the same. */
static void fly_frame (int k, float dt, int shots, const char *shotname)
{
	vec3_t		feet, ang;
	int			pt = qog_fly_camera (k * dt, feet, ang);
	unsigned	h;

	qog_fly_render (feet, ang, k * dt);
	h = qog_fly_hash_view ();
	Sys_Printf ("fly %d %08x %d%s\n", k, h, pt, r_viewleaf->contents == CONTENTS_SOLID ? " solid" : "");
	if (shots > 0 && k % shots == 0)
		Cmd_ExecuteString (va ("qog_shot %s_%04d", shotname, k), src_command);
}

void qog_fly_pc_main (quakeparms_t *parms)
{
	int		i, frames, shots;
	float	dt;
	char	*map = "e1m1";

	qog_fly_init_host (parms);
	i = COM_CheckParm ("-flymap");
	if (i && i + 1 < com_argc)
		map = com_argv[i + 1];
	frames = fly_argi ("-flyframes", 200);
	dt = fly_argf ("-flydt", 0.1f);
	shots = fly_argi ("-flyshots", 0);
	Sys_Printf ("fly: init done, hunk low %d high %d of %d\n", hunk_low_used, hunk_high_used, parms->memsize);
	qog_fly_load (map);
	Sys_Printf ("fly: %s loaded, hunk low %d high %d, %d points, loop %.1f s\n", map,
				hunk_low_used, hunk_high_used, qog_fly_numpoints (), qog_fly_loop_time ());
	if (COM_CheckParm ("-flyhunk"))
	{
		extern void Hunk_Print (qboolean all);
		Hunk_Print (true);
	}
	for (i = 0 ; i < qog_fly_numpoints () ; i++)
	{
		vec3_t	p;
		qog_fly_point (i, p);
		Sys_Printf ("flypoint %d %.0f %.0f %.0f t %.2f\n", i, p[0], p[1], p[2], qog_fly_point_time (i));
	}
	for (i = 0 ; i < frames ; i++)
	{
		fly_frame (i, dt, shots, "n_fly");
		qog_fly_overlay (va ("frame %d", i), va ("hunk %d KB", (hunk_low_used + hunk_high_used) / 1024));
		VID_Update (NULL);
	}
#ifdef QOG_PHASE_TIME
	qog_phase_report (Sys_Printf);
#endif
	{
		const qog_mem_t	*m = qog_mem_sample ();
		Sys_Printf ("fly: done, hunk peak low %d high %d, zone %d of %d\n", m->peak_low, m->peak_high,
					m->zone_used, m->zone_size);
	}
	exit (0);
}

/* ---- inside the full game --------------------------------------------- */

static void fly_noentities (void)
{
	Cvar_Set ("r_drawentities", "0");
	Cvar_Set ("r_drawviewmodel", "0");
	Cvar_Set ("crosshair", "0");
}

#ifdef QOG_PHASE_TIME
/* `qog_phase` prints where render time went since the last call, per frame */
static void cmd_qog_phase (void)
{
	qog_phase_report (Con_Printf);
}

#endif
static void cmd_qog_fly (void)
{
	int		frames = Cmd_Argc () > 1 ? Q_atoi (Cmd_Argv (1)) : 200;
	float	dt = Cmd_Argc () > 2 ? Q_atof (Cmd_Argv (2)) : 0.1f;
	int		shots = Cmd_Argc () > 3 ? Q_atoi (Cmd_Argv (3)) : 0;
	double	saved = cl.time;
	int		i;

	if (!cl.worldmodel || cls.signon != SIGNONS)
	{
		Con_Printf ("qog_fly: no map loaded\n");
		return;
	}
	fly_noentities ();
	qog_fly_use_world ();
	qog_fly_setup_view ();
	for (i = 0 ; i < frames ; i++)
		fly_frame (i, dt, shots, "n_flyg");
	cl.time = saved;
}

#define REF_HOLD	3		/* frames the player stays put before the compare */

static int		ref_active, ref_point, ref_frame, ref_same, ref_diffshots;
static byte		ref_game[320 * 200];

static float ref_angle (float a)
{
	// what the client reads back: MSG_WriteAngle then MSG_ReadAngle
	return (signed char)((((int)a) * 256 / 360) & 255) * (360.0 / 256);
}

static void cmd_qog_fly_ref (void)
{
	if (!sv.active || cls.signon != SIGNONS || !cl.worldmodel)
	{
		Con_Printf ("qog_fly_ref: no map loaded\n");
		return;
	}
	fly_noentities ();
	qog_fly_use_world ();
	ref_active = 1;
	ref_point = ref_frame = ref_same = ref_diffshots = 0;
}

/* Called from the PC main loop after every Host_Frame. */
void qog_fly_frame (void)
{
	vec3_t		feet, ang, q;
	unsigned	hg, hf;
	int			i, j, n, diff;

	if (!ref_active)
		return;
	n = qog_fly_numpoints ();
	if (ref_point >= n)
	{
		Sys_Printf ("flyref: %d points, %d identical\n", n, ref_same);
		ref_active = 0;
		Cbuf_AddText ("qog_quit\n");
		return;
	}
	qog_fly_lightstyles_normal ();	// before the next frame draws
	qog_fly_point (ref_point, feet);
	qog_fly_point_angles (ref_point, ang);

	if (ref_frame < REF_HOLD)
	{
		VectorCopy (feet, sv_player->v.origin);
		VectorCopy (ang, sv_player->v.angles);
		EV_SETVECTOR(sv_player, v_angle, ang);
		EV_SETFLOAT(sv_player, fixangle, 1);
		VectorCopy (vec3_origin, sv_player->v.velocity);
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		sv_player->v.solid = SOLID_NOT;		// touches no items or triggers on the way
		sv_player->v.flags = ((int)sv_player->v.flags | FL_GODMODE | FL_NOTARGET) & ~FL_ONGROUND;
		SV_LinkEdict (sv_player, false);
		ref_frame++;
		return;
	}

	// the frame on screen is the game's own view of the spot
	hg = qog_fly_hash_view ();
	memcpy (ref_game, vid.buffer, vid.rowbytes * (vid.height - sb_lines));
	for (j = 0 ; j < 3 ; j++)
		q[j] = ref_angle (ang[j]);
	qog_fly_setup_view ();
	qog_fly_render (feet, q, cl.time);
	hf = qog_fly_hash_view ();
	diff = 0;
	for (i = 0 ; i < (int)(vid.rowbytes * (vid.height - sb_lines)) ; i++)
		diff += ref_game[i] != vid.buffer[i];
	if ((diff && ref_diffshots < 4) || ref_point < 3)
	{
		Cmd_ExecuteString (va ("qog_shot n_ref_%03d_fly", ref_point), src_command);
		memcpy (vid.buffer, ref_game, vid.rowbytes * (vid.height - sb_lines));
		Cmd_ExecuteString (va ("qog_shot n_ref_%03d_game", ref_point), src_command);
		ref_diffshots += diff != 0;
	}
	Sys_Printf ("flyref %d %08x %08x %s %d  cl.time %.3f  pos %.0f %.0f %.0f  ang %.2f %.2f  game ang %.2f %.2f\n",
				ref_point, hg, hf, diff ? "DIFF" : "same", diff, cl.time, feet[0], feet[1], feet[2],
				q[0], q[1], cl.viewangles[0], cl.viewangles[1]);
	ref_same += !diff;
	ref_point++;
	ref_frame = 0;
}

void qog_fly_register (void)
{
#ifdef QOG_PHASE_TIME
	Cmd_AddCommand ("qog_phase", cmd_qog_phase);
#endif
	Cmd_AddCommand ("qog_fly", cmd_qog_fly);
	Cmd_AddCommand ("qog_fly_ref", cmd_qog_fly_ref);
}
