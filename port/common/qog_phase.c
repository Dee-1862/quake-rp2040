/* QuakeOG: render phase times and work counts (see engine/qog_phase.h). */
#ifdef QOG_PHASE_TIME
#include <stdio.h>
#include <string.h>
#include "qog_phase.h"

unsigned	qog_ph_time[QOG_PH_N];
unsigned	qog_ph_count[QOG_PC_N];
unsigned	qog_hp_time[QOG_HP_N];	/* QuakeOG (Agent V) */
unsigned	qog_hp_frames;

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

void qog_phase_board_report (void (*print) (char *fmt, ...), const char *who, unsigned elapsed_us)
{
	static const char	*const hp_names[QOG_HP_N] = { "frame", "input", "link", "read", "screen", "view", "hud",
											"present", "scanwait", "sound", "wait", "svframe", "phys", "send" };
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
	}
	memset (qog_ph_time, 0, sizeof qog_ph_time);
	memset (qog_ph_count, 0, sizeof qog_ph_count);
	memset (qog_hp_time, 0, sizeof qog_hp_time);
	qog_hp_frames = 0;
}
#endif
