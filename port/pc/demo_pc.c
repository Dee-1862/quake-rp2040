/* Demo playback host on the PC (development and test tool). See
 * port/common/qog_demo.h.
 *
 *   quakeog_pc.exe -qogdemo [-demos demo1 demo2 ...] [-demodt 0.05]
 *                  [-demoseed 0] [-demoframes 100000] [-demohash]
 *                  [-demodump <first> <last>] [-demoshots 0] [-demohunk]
 *                  [-demomanifest <file>]
 *       The host the board app apps/qogdemo runs: id's client and renderer
 *       with no server, QuakeC, console or sound. Plays the demos once in
 *       order (frame time -demodt, fixed as host_framerate), the random
 *       sequence restarting from -demoseed as the loop starts.
 *       -demohash prints "fh <frame> <hash>" for every frame presented
 *       (qog_framehash, port/pc/vid_pc.c), -demodump also saves the
 *       presented frames first to last as fd_<n>, -demoshots n writes a shot
 *       o_demo_<n> every n presented frames. At each demo's end it prints
 *       the peaks (entities, particles, ...) and the hunk, with -demohunk
 *       also the hunk blocks of the last demo. -demomanifest writes the
 *       files the run opened (qog_manifest, port/pc/manifest_pc.c), for
 *       tools/mkboard.py. Then it exits.
 *
 *   qog_demoloop <seed> <demo> [<demo> ...]   (console)
 *       After the current frame: restarts the shared random sequence from
 *       seed (port/common/qog_rand.c) and starts a demo loop of these demos
 *       as "startdemos" would, so the next frame runs "playdemo" just as the
 *       host's first frame does. The game then presents the same frames as
 *       -qogdemo -demos <demos> -demoseed <seed>, apart from the console and
 *       the loading plaque, which the host does not draw.
 *
 *   qog_quitafter <frames>                    (console)
 *       Quits after that many more host frames, so a script can end and
 *       leave the command buffer to the demo (text a demo stuffs, such as
 *       "bf", would otherwise wait behind the script's waits).
 *
 *   qog_srand <seed>                          (console)
 *       Restarts the shared random sequence now. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quakedef.h"
#include "qog_demo.h"
#include "qog_rand.h"
#include "qog_mem.h"

extern int	hunk_low_used, hunk_high_used;
void Hunk_Print (qboolean all);

static int demo_argi (const char *name, int def)
{
	int	i = COM_CheckParm ((char *)name);
	return (i && i + 1 < com_argc) ? Q_atoi (com_argv[i + 1]) : def;
}

static float demo_argf (const char *name, float def)
{
	int	i = COM_CheckParm ((char *)name);
	return (i && i + 1 < com_argc) ? Q_atof (com_argv[i + 1]) : def;
}

static void demo_print_peaks (const char *what)
{
	const qog_demo_peaks_t	*p = qog_demo_peaks ();
	const qog_mem_t			*m = qog_mem_sample ();

	Sys_Printf ("demo peaks %s: frames %d entities %d visedicts %d particles %d statics %d models %d "
				"tempents %d beams %d dlights %d hunk low %d (peak %d) high %d\n", what, p->frames,
				p->entities, p->visedicts, p->particles, p->statics, p->models, p->tempents, p->beams,
				p->dlights, p->hunk_low, m->peak_low, m->peak_high);
}

void qog_demo_pc_main (quakeparms_t *parms)
{
	static const char	*defaults[] = { "demo1", "demo2", "demo3" };
	const char	*names[QOG_DEMO_MAX];
	int			count = 0, i, frames, shots, started = 0, presented = 0;
	float		dt;
	char		last[16] = "";

	qog_demo_init_host (parms);

	i = COM_CheckParm ("-demos");
	if (i)
		for (i++ ; i < com_argc && count < QOG_DEMO_MAX && com_argv[i][0] != '-' && com_argv[i][0] != '+' ; i++)
			names[count++] = com_argv[i];
	if (!count)
		for (count = 0 ; count < 3 ; count++)
			names[count] = defaults[count];
	frames = demo_argi ("-demoframes", 100000);
	dt = demo_argf ("-demodt", 0.05f);
	shots = demo_argi ("-demoshots", 0);
	if (COM_CheckParm ("-demohash"))
		Cmd_ExecuteString ("qog_framehash 1", src_command);
	i = COM_CheckParm ("-demodump");
	if (i && i + 2 < com_argc)
		Cmd_ExecuteString (va ("qog_framehash 1 %s %s", com_argv[i + 1], com_argv[i + 2]), src_command);

	qog_demo_start (names, count, (unsigned)demo_argi ("-demoseed", 0));
	for (i = 0 ; i < frames ; i++)
	{
		int		drawn = qog_demo_frame (dt, true);

		if (drawn < 0 && started >= count)
			break;		// the last demo has ended
		if (qog_demo_starts () != started)
		{
			if (started)
			{
				demo_print_peaks (last);
				qog_demo_reset_peaks ();
			}
			started = qog_demo_starts ();
			Q_strncpy (last, (char *)qog_demo_name (), sizeof(last) - 1);
			Sys_Printf ("demo %s started at frame %d, hunk low %d\n", last, i, hunk_low_used);
		}
		if (drawn > 0)
		{
			if (shots > 0 && presented % shots == 0)
				Cmd_ExecuteString (va ("qog_shot o_demo_%05d", presented), src_command);
			presented++;
		}
	}
	if (last[0])
		demo_print_peaks (last);
	if (COM_CheckParm ("-demohunk"))
		Hunk_Print (true);
	Sys_Printf ("demo: done, %d frames, %d presented, hunk low %d high %d\n", i, presented,
				hunk_low_used, hunk_high_used);
	i = COM_CheckParm ("-demomanifest");
	if (i && i + 1 < com_argc)
		Cmd_ExecuteString (va ("qog_manifest %s", com_argv[i + 1]), src_command);
	exit (0);
}

/* ---- inside the full game --------------------------------------------- */

static void cmd_qog_srand (void)
{
	qog_srand (Cmd_Argc () > 1 ? (unsigned)Q_atoi (Cmd_Argv (1)) : 0);
}

static int		demo_quitafter;
static int		demo_loop_pending;
static unsigned	demo_loop_seed;
static char		demo_loop_names[QOG_DEMO_MAX][16];
static int		demo_loop_count;

static void cmd_qog_quitafter (void)
{
	demo_quitafter = Cmd_Argc () > 1 ? Q_atoi (Cmd_Argv (1)) : 0;
}

static void cmd_qog_demoloop (void)
{
	int		i;

	if (Cmd_Argc () < 3)
	{
		Con_Printf ("qog_demoloop <seed> <demo> [<demo> ...]\n");
		return;
	}
	demo_loop_seed = (unsigned)Q_atoi (Cmd_Argv (1));
	for (demo_loop_count = 0, i = 2 ; i < Cmd_Argc () && demo_loop_count < QOG_DEMO_MAX ; i++)
		Q_strncpy (demo_loop_names[demo_loop_count++], Cmd_Argv (i), sizeof(demo_loop_names[0]) - 1);
	demo_loop_pending = 1;
}

/* Called from the PC main loop after every Host_Frame. */
void qog_demo_frame_pc (void)
{
	static int	lastframe;

	if (demo_loop_pending)
	{	// between two frames, as the host's qog_demo_start
		const char	*names[QOG_DEMO_MAX];
		int			i;

		demo_loop_pending = 0;
		for (i = 0 ; i < demo_loop_count ; i++)
			names[i] = demo_loop_names[i];
		qog_demo_start (names, demo_loop_count, demo_loop_seed);
	}
	if (demo_quitafter <= 0 || host_framecount == lastframe)
		return;
	lastframe = host_framecount;
	if (--demo_quitafter == 0)
		Cbuf_AddText ("qog_quit\n");
}

void qog_demo_register (void)
{
	Cmd_AddCommand ("qog_srand", cmd_qog_srand);
	Cmd_AddCommand ("qog_quitafter", cmd_qog_quitafter);
	Cmd_AddCommand ("qog_demoloop", cmd_qog_demoloop);
}
