/* QuakeOG: render phase times and work counts (see engine/qog_phase.h). */
#ifdef QOG_PHASE_TIME
#include <stdio.h>
#include <string.h>
#include "qog_phase.h"

unsigned	qog_ph_time[QOG_PH_N];
unsigned	qog_ph_count[QOG_PC_N];

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
#endif
