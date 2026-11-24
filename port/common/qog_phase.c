/* QuakeOG: render phase times and work counts (see engine/qog_phase.h). */
#ifdef QOG_PHASE_TIME
#include <stdio.h>
#include <string.h>
#include "qog_phase.h"

unsigned	qog_ph_time[QOG_PH_N];
unsigned	qog_ph_count[QOG_PC_N];
unsigned	qog_hp_time[QOG_HP_N];	/* QuakeOG (Agent V) */
unsigned	qog_hp_frames;
unsigned	qog_c1_stat[QOG_C1_N];	/* QuakeOG (Agent AM) */
#ifdef QOG_PHASE_XIP
unsigned	qog_ph_xacc[QOG_PH_N], qog_ph_xhit[QOG_PH_N];	/* QuakeOG (Agent Z) */
unsigned	qog_ph_fcalls[QOG_PH_N], qog_ph_dcalls[QOG_PH_N];
unsigned	qog_wc_count[QOG_WC_N];
#endif

#if defined(_WIN32)
#include <windows.h>
static double	ph_us_per_tick;

unsigned qog_phase_clock (void)
{
	LARGE_INTEGER	c;

	QueryPerformanceCounter (&c);
	return (unsigned)c.QuadPart;
}

float qog_phase_us (unsigned ticks)
{
	if (!ph_us_per_tick)
	{
		LARGE_INTEGER	f;

		QueryPerformanceFrequency (&f);
		ph_us_per_tick = 1e6 / (double)f.QuadPart;
	}
	return (float)(ticks * ph_us_per_tick);
}
#else
#include "hardware/structs/timer.h"

unsigned qog_phase_clock (void)
{
	return timer_hw->timerawl;	/* 1 MHz */
}

float qog_phase_us (unsigned ticks)
{
	return (float)ticks;
}
#endif

#ifdef QOG_PHASE_XIP
/* QuakeOG (Agent Z): phase marks with the XIP cache counters and core 0's
   helper call counts (engine/qog_phase.h). On the board they run from RAM,
   so taking a mark adds no XIP accesses of its own. */
#if defined(_WIN32)
#define QOG_PH_RAMFUNC(f)	f
#else
#include "hardware/structs/xip_ctrl.h"
#define QOG_PH_RAMFUNC(f)	__attribute__((section (".time_critical." #f), noinline)) f
#endif
#ifdef QOG_PHASE_FPCOUNT
#include "qog_fpcount.h"
#endif

void QOG_PH_RAMFUNC (qog_phase_mark) (qog_phmark_t *m)
{
	m->t = qog_phase_clock ();
#if defined(_WIN32)
	m->acc = m->hit = 0;
#else
	m->acc = xip_ctrl_hw->ctr_acc;
	m->hit = xip_ctrl_hw->ctr_hit;
#endif
#ifdef QOG_PHASE_FPCOUNT
	m->fcalls = qog_fpc[0][QOG_FPC_FTOTAL];
	m->dcalls = qog_fpc[0][QOG_FPC_DTOTAL];
#else
	m->fcalls = m->dcalls = 0;
#endif
}

void QOG_PH_RAMFUNC (qog_phase_add) (int ph, const qog_phmark_t *m)
{
	qog_phmark_t	now;

	qog_phase_mark (&now);
	qog_ph_time[ph] += now.t - m->t;
	qog_ph_xacc[ph] += now.acc - m->acc;
	qog_ph_xhit[ph] += now.hit - m->hit;
	qog_ph_fcalls[ph] += now.fcalls - m->fcalls;
	qog_ph_dcalls[ph] += now.dcalls - m->dcalls;
}

static void qog_phase_clear_xip (void)
{
	memset (qog_ph_xacc, 0, sizeof qog_ph_xacc);
	memset (qog_ph_xhit, 0, sizeof qog_ph_xhit);
	memset (qog_ph_fcalls, 0, sizeof qog_ph_fcalls);
	memset (qog_ph_dcalls, 0, sizeof qog_ph_dcalls);
	memset (qog_wc_count, 0, sizeof qog_wc_count);
#if !defined(_WIN32)
	// the hardware counters saturate at 2^32, so they restart every report
	// (no phase is open between frames, where the report runs)
	xip_ctrl_hw->ctr_acc = 0;
	xip_ctrl_hw->ctr_hit = 0;
#endif
}
#endif

void qog_phase_report (void (*print) (char *fmt, ...))
{
	static const char	*names[QOG_PH_N] = { "setup", "world", "bmodels", "scan", "lmap", "grad",
										 "spans", "zspans", "sky", "turb", "models", "vmodel",
										 "parts", "total" };
	unsigned	n = qog_ph_count[QOG_PC_FRAMES];
	char		line[400];
	int			i, k = 0;
	float		ms[QOG_PH_N];

	if (!n)
		return;
	for (i = 0 ; i < QOG_PH_N ; i++)
		ms[i] = qog_phase_us (qog_ph_time[i]) * 1e-3f / n;
	k += snprintf (line + k, sizeof line - k, "[phase] frames %u ms", n);
	for (i = 0 ; i < QOG_PH_N ; i++)
		k += snprintf (line + k, sizeof line - k, " %s %.2f", names[i], ms[i]);
	k += snprintf (line + k, sizeof line - k, " edgescan %.2f\n",
				   ms[QOG_PH_SCAN] - ms[QOG_PH_LIGHTMAP] - ms[QOG_PH_GRADIENTS] - ms[QOG_PH_SPANS] -
				   ms[QOG_PH_ZSPANS] - ms[QOG_PH_SKY] - ms[QOG_PH_TURB]);
	print ("%s", line);
	print ("[phase] per frame: litsurfs %u lmcells %u litspans %u litpx %u anypx %u runs %u zpx %u skypx %u turbpx %u\n",
		   qog_ph_count[QOG_PC_LITSURFS] / n, qog_ph_count[QOG_PC_LMCELLS] / n,
		   qog_ph_count[QOG_PC_LITSPANS] / n, qog_ph_count[QOG_PC_LITPX] / n,
		   qog_ph_count[QOG_PC_LITANYPX] / n, qog_ph_count[QOG_PC_RUNS] / n,
		   qog_ph_count[QOG_PC_ZPX] / n, qog_ph_count[QOG_PC_SKYPX] / n, qog_ph_count[QOG_PC_TURBPX] / n);
#ifdef QOG_PHASE_XIP
	print ("[phase] world per frame: nodes %u leafs %u faces %u clipedge %u emitedge %u cached %u project %u clipdot %u rotate %u rottrig %u\n",
		   qog_wc_count[QOG_WC_NODES] / n, qog_wc_count[QOG_WC_LEAFS] / n, qog_wc_count[QOG_WC_FACES] / n,
		   qog_wc_count[QOG_WC_CLIPEDGE] / n, qog_wc_count[QOG_WC_EMITEDGE] / n, qog_wc_count[QOG_WC_CACHED] / n,
		   qog_wc_count[QOG_WC_PROJECT] / n, qog_wc_count[QOG_WC_CLIPDOT] / n,
		   qog_wc_count[QOG_WC_ROTATE] / n, qog_wc_count[QOG_WC_ROTTRIG] / n);
	qog_phase_clear_xip ();
#endif
	if (qog_c1_stat[QOG_C1_SURFS])	/* QuakeOG (Agent AM), QOG_DRAW_CORE1 */
		print ("[phase] core1 pipeline per frame: surfs %u by core1 %u early %u, pixels %u early %u, core1 ms draw %.2f wait %.2f\n",
			   qog_c1_stat[QOG_C1_SURFS] / n, qog_c1_stat[QOG_C1_C1SURFS] / n, qog_c1_stat[QOG_C1_EARLY] / n,
			   qog_c1_stat[QOG_C1_PX] / n, qog_c1_stat[QOG_C1_EARLYPX] / n,
			   qog_phase_us (qog_c1_stat[QOG_C1_DRAWT]) * 1e-3f / n, qog_phase_us (qog_c1_stat[QOG_C1_WAITT]) * 1e-3f / n);
	memset (qog_c1_stat, 0, sizeof qog_c1_stat);
	memset (qog_ph_time, 0, sizeof qog_ph_time);
	memset (qog_ph_count, 0, sizeof qog_ph_count);
}

/* QuakeOG (Agent V): the board's report (engine/qog_phase.h). Integers only,
   so it needs no float printf: each time is the mean per frame in ms with
   one decimal. Sums over 2 s stay far below 2^32 us. */
static unsigned ph_us (unsigned ticks)
{
#if defined(_WIN32)
	return (unsigned)qog_phase_us (ticks);
#else
	return ticks;
#endif
}

/* " name 12.3" for the mean of sum over n frames */
static int ph_put (char *out, int room, const char *name, unsigned sum, unsigned n)
{
	unsigned	us = ph_us (sum) / n;
	int			k = snprintf (out, room, " %s %u.%u", name, us / 1000u, (us % 1000u) / 100u);

	return k < 0 ? 0 : (k < room ? k : room - 1);
}

#ifdef QOG_PHASE_XIP
/* QuakeOG (Agent Z): the measurement lines, per view means over the report
   (engine/qog_phase.h, docs/changes.md). edgescan is scan less the surface
   drawing inside it, as in the time line. */
static unsigned ph_edgescan (const unsigned *a)
{
	return a[QOG_PH_SCAN] - a[QOG_PH_LIGHTMAP] - a[QOG_PH_GRADIENTS] - a[QOG_PH_SPANS]
		 - a[QOG_PH_ZSPANS] - a[QOG_PH_SKY] - a[QOG_PH_TURB];
}

static int ph_put2 (char *out, int room, const char *name, unsigned a, unsigned b, unsigned n)
{
	int	k = snprintf (out, room, " %s %u/%u", name, a / n, b / n);

	return k < 0 ? 0 : (k < room ? k : room - 1);
}

static void qog_phase_board_xip (void (*print) (char *fmt, ...), const char *who, unsigned r)
{
	static const char	*const names[QOG_PH_N] = { "setup", "world", "bmodels", "scan", "lmap", "grad",
											"spans", "zspans", "sky", "turb", "models", "vmodel",
											"parts", "total" };
	unsigned	miss[QOG_PH_N];
	char		line[400];
	int			i, k;

	for (i = 0 ; i < QOG_PH_N ; i++)
		miss[i] = qog_ph_xacc[i] - qog_ph_xhit[i];
	// XIP cache accesses/misses (both cores) per view in each phase
	k = snprintf (line, sizeof line, "[time %s] xip acc/miss", who);
	for (i = 0 ; i < QOG_PH_N ; i++)
		if (i != QOG_PH_SCAN)
			k += ph_put2 (line + k, (int)sizeof line - k, names[i], qog_ph_xacc[i], miss[i], r);
	k += ph_put2 (line + k, (int)sizeof line - k, "edgescan", ph_edgescan (qog_ph_xacc), ph_edgescan (miss), r);
	print ("%s\n", line);
	print ("[time %s] work per view nodes %u leafs %u faces %u clipedge %u emitedge %u cached %u project %u clipdot %u rotate %u rottrig %u\n",
		   who, qog_wc_count[QOG_WC_NODES] / r, qog_wc_count[QOG_WC_LEAFS] / r, qog_wc_count[QOG_WC_FACES] / r,
		   qog_wc_count[QOG_WC_CLIPEDGE] / r, qog_wc_count[QOG_WC_EMITEDGE] / r, qog_wc_count[QOG_WC_CACHED] / r,
		   qog_wc_count[QOG_WC_PROJECT] / r, qog_wc_count[QOG_WC_CLIPDOT] / r,
		   qog_wc_count[QOG_WC_ROTATE] / r, qog_wc_count[QOG_WC_ROTTRIG] / r);
#ifdef QOG_PHASE_FPCOUNT
	// core 0's float/double helper calls per view in each phase
	k = snprintf (line, sizeof line, "[time %s] fp f/d", who);
	for (i = 0 ; i < QOG_PH_N ; i++)
		if (i != QOG_PH_SCAN)
			k += ph_put2 (line + k, (int)sizeof line - k, names[i], qog_ph_fcalls[i], qog_ph_dcalls[i], r);
	k += ph_put2 (line + k, (int)sizeof line - k, "edgescan", ph_edgescan (qog_ph_fcalls), ph_edgescan (qog_ph_dcalls), r);
	print ("%s\n", line);
	qog_fpcount_report (print, who, r, qog_ph_fcalls, qog_ph_dcalls);
#endif
}
#endif

/* QuakeOG (Agent AM): where the display's core 1 spends a frame. scan is
   the LCD scanouts, jobs every job core 0 handed it (Agent V's span halves,
   Agent Z's alias vertices, the surface pipeline), busy both less the
   pipeline job's waiting, idle the frame less busy. Per view: pipe is the
   pipeline job's surface drawing and wait its waiting, c0wait core 0's
   waiting for core 1's last surface at a flush (inside edgescan), surfs the lit world
   surfaces drawn through the pipeline, c1 those core 1 drew, early those
   handed on during the scan. The port gives the scanout and job times
   (vid_rp2040.c), the server half has none. */
#if !defined(_WIN32)
__attribute__((weak)) void Sys_QogCore1Times (unsigned *scan_us, unsigned *job_us)
{
	*scan_us = *job_us = 0;
}
#else
static void Sys_QogCore1Times (unsigned *scan_us, unsigned *job_us)
{
	*scan_us = *job_us = 0;
}
#endif

static void qog_phase_board_core1 (void (*print) (char *fmt, ...), const char *who,
								   unsigned n, unsigned r, unsigned elapsed_us)
{
	static unsigned	last_scan, last_job;
	unsigned		scan, job, dscan, djob, busy, wait, frame;
	char			line[240];
	int				k;

	Sys_QogCore1Times (&scan, &job);
	dscan = scan - last_scan;
	djob = job - last_job;
	last_scan = scan;
	last_job = job;
	wait = ph_us (qog_c1_stat[QOG_C1_WAITT]);
	if (who[0] == 's' || !n || (!djob && !dscan && !qog_c1_stat[QOG_C1_SURFS]))
	{
		memset (qog_c1_stat, 0, sizeof qog_c1_stat);
		return;
	}
	busy = dscan + (djob > wait ? djob - wait : 0);
	frame = elapsed_us;
	k = snprintf (line, sizeof line, "[time %s] core1 per frame ms", who);
	k += ph_put (line + k, (int)sizeof line - k, "scan", dscan, n);
	k += ph_put (line + k, (int)sizeof line - k, "jobs", djob, n);
	k += ph_put (line + k, (int)sizeof line - k, "busy", busy, n);
	k += ph_put (line + k, (int)sizeof line - k, "idle", frame > busy ? frame - busy : 0, n);
	if (r)
	{
		k += snprintf (line + k, sizeof line - k, ", per view");
		k += ph_put (line + k, (int)sizeof line - k, "pipe", qog_c1_stat[QOG_C1_DRAWT], r);
		k += ph_put (line + k, (int)sizeof line - k, "wait", qog_c1_stat[QOG_C1_WAITT], r);
		k += ph_put (line + k, (int)sizeof line - k, "c0wait", qog_c1_stat[QOG_C1_C0WAIT], r);
		k += snprintf (line + k, sizeof line - k, " surfs %u c1 %u early %u",
					   qog_c1_stat[QOG_C1_SURFS] / r, qog_c1_stat[QOG_C1_C1SURFS] / r,
					   qog_c1_stat[QOG_C1_EARLY] / r);
	}
	print ("%s\n", line);
	memset (qog_c1_stat, 0, sizeof qog_c1_stat);
}

void qog_phase_board_report (void (*print) (char *fmt, ...), const char *who, unsigned elapsed_us)
{
	static const char	*const hp_names[QOG_HP_N] = { "frame", "input", "link", "read", "screen", "view", "hud",
											"present", "scanwait", "race", "sound", "wait", "svframe", "phys", "send" };
	static const char	*const ph_names[QOG_PH_N] = { "setup", "world", "bmodels", "scan", "lmap", "grad",
											"spans", "zspans", "sky", "turb", "models", "vmodel",
											"parts", "total" };
	unsigned	n = qog_hp_frames, r = qog_ph_count[QOG_PC_FRAMES];
	unsigned	fps10 = elapsed_us ? (unsigned)(((unsigned long long)n * 10000000u) / elapsed_us) : 0;
	char		line[320];
	int			i, k;

	if (n)
	{
		k = snprintf (line, sizeof line, "[time %s] frames %u fps %u.%u ms", who, n, fps10 / 10u, fps10 % 10u);
		if (who[0] == 's')
		{
			for (i = QOG_HP_SVWAIT ; i <= QOG_HP_SVSEND ; i++)
				k += ph_put (line + k, (int)sizeof line - k, hp_names[i], qog_hp_time[i], n);
		}
		else
		{
			for (i = QOG_HP_FRAME ; i <= QOG_HP_SOUND ; i++)
				k += ph_put (line + k, (int)sizeof line - k, hp_names[i], qog_hp_time[i], n);
		}
		print ("%s\n", line);
	}
	if (r)
	{
		unsigned	edges = qog_ph_time[QOG_PH_SCAN] - qog_ph_time[QOG_PH_LIGHTMAP] - qog_ph_time[QOG_PH_GRADIENTS]
						- qog_ph_time[QOG_PH_SPANS] - qog_ph_time[QOG_PH_ZSPANS] - qog_ph_time[QOG_PH_SKY]
						- qog_ph_time[QOG_PH_TURB];

		k = snprintf (line, sizeof line, "[time %s] views %u ms", who, r);
		for (i = 0 ; i < QOG_PH_N ; i++)
			if (i != QOG_PH_SCAN)
				k += ph_put (line + k, (int)sizeof line - k, ph_names[i], qog_ph_time[i], r);
		k += ph_put (line + k, (int)sizeof line - k, "edgescan", edges, r);
		print ("%s\n", line);
		print ("[time %s] per view litsurfs %u lmcells %u litspans %u litpx %u runs %u zpx %u skypx %u turbpx %u\n",
			   who, qog_ph_count[QOG_PC_LITSURFS] / r, qog_ph_count[QOG_PC_LMCELLS] / r,
			   qog_ph_count[QOG_PC_LITSPANS] / r, qog_ph_count[QOG_PC_LITPX] / r,
			   qog_ph_count[QOG_PC_RUNS] / r, qog_ph_count[QOG_PC_ZPX] / r,
			   qog_ph_count[QOG_PC_SKYPX] / r, qog_ph_count[QOG_PC_TURBPX] / r);
#ifdef QOG_PHASE_XIP
		qog_phase_board_xip (print, who, r);
#endif
	}
	qog_phase_board_core1 (print, who, n, r, elapsed_us);
#ifdef QOG_PHASE_XIP
	qog_phase_clear_xip ();
#endif
	memset (qog_ph_time, 0, sizeof qog_ph_time);
	memset (qog_ph_count, 0, sizeof qog_ph_count);
	memset (qog_hp_time, 0, sizeof qog_hp_time);
	qog_hp_frames = 0;
}
#endif
