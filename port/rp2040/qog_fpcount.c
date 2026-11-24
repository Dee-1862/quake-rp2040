/* QuakeOG (Agent Z): float and double helper call counts for measurement
 * builds of the board client (QOG_PHASE_FPCOUNT, port/common/qog_fpcount.h).
 * The stubs in qog_fpcount.S count, this file times one helper of each
 * group on core 0 and prints the counts. Built outside the engine library,
 * so its own helper calls go straight to the SDK and are not counted. */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "qog_fpcount.h"

volatile unsigned	qog_fpc[2][32];

static const char	*const fpc_names[QOG_FPC_NGROUPS] = {
	"fadd", "fsub", "fmul", "fdiv", "fcmp", "f2i", "i2f", "f2d", "fmath",
	"dadd", "dsub", "dmul", "ddiv", "dcmp", "d2i", "d2f", "i2d", "dmath",
	"idiv", "l64"
};

/* the helpers by their ABI names (the linker's --wrap sends these calls to
   the SDK's routines, as it does the engine's) */
extern float	__aeabi_fadd (float, float), __aeabi_fsub (float, float);
extern float	__aeabi_fmul (float, float), __aeabi_fdiv (float, float);
extern int		__aeabi_fcmplt (float, float), __aeabi_f2iz (float);
extern float	__aeabi_i2f (int);
extern double	__aeabi_f2d (float);
extern double	__aeabi_dadd (double, double), __aeabi_dsub (double, double);
extern double	__aeabi_dmul (double, double), __aeabi_ddiv (double, double);
extern int		__aeabi_dcmplt (double, double), __aeabi_d2iz (double);
extern float	__aeabi_d2f (double);
extern double	__aeabi_i2d (int);
extern int		__aeabi_idiv (int, int);
extern long long	__aeabi_lmul (long long, long long);
extern float	sqrtf (float);
extern double	ceil (double);

static unsigned	fpc_ns[QOG_FPC_NGROUPS];	/* ns per call, call and loop included */

#define FPC_N	2000

/* ns per call of one helper, FPC_N calls in a loop, the fastest of three
   runs (interrupts stay on, USB needs them) */
#define FPC_TIME(group, expr) \
	do { \
		unsigned	best = ~0u; \
		for (r = 0 ; r < 3 ; r++) { \
			uint32_t	t0 = time_us_32 (); \
			for (i = 0 ; i < FPC_N ; i++) { expr; } \
			t0 = time_us_32 () - t0; \
			if (t0 < best) best = t0; \
		} \
		fpc_ns[group] = best * 1000u / FPC_N; \
	} while (0)

static void qog_fpcount_calibrate (void)
{
	volatile float		vf = 1.2345f, wf = 0.987f, xf = 123.75f, kf = 1.0001f;
	volatile double		vd = 1.2345, wd = 0.987, xd = 98.7, kd = 1.0001;
	volatile int		vi = 12345, di = 37, sink;
	volatile long long	vl = 123456789, kl = 3;
	int					i, r;

	FPC_TIME (QOG_FPC_FADD, vf = __aeabi_fadd (vf, wf));
	FPC_TIME (QOG_FPC_FSUB, vf = __aeabi_fsub (vf, wf));
	FPC_TIME (QOG_FPC_FMUL, vf = __aeabi_fmul (wf, kf));
	FPC_TIME (QOG_FPC_FDIV, vf = __aeabi_fdiv (wf, vf));
	FPC_TIME (QOG_FPC_FCMP, sink = __aeabi_fcmplt (vf, wf));
	FPC_TIME (QOG_FPC_F2I, sink = __aeabi_f2iz (xf));
	FPC_TIME (QOG_FPC_I2F, vf = __aeabi_i2f (vi));
	FPC_TIME (QOG_FPC_F2D, vd = __aeabi_f2d (vf));
	FPC_TIME (QOG_FPC_FMATH, vf = sqrtf (xf));
	FPC_TIME (QOG_FPC_DADD, vd = __aeabi_dadd (vd, wd));
	FPC_TIME (QOG_FPC_DSUB, vd = __aeabi_dsub (vd, wd));
	FPC_TIME (QOG_FPC_DMUL, vd = __aeabi_dmul (wd, kd));
	FPC_TIME (QOG_FPC_DDIV, vd = __aeabi_ddiv (wd, vd));
	FPC_TIME (QOG_FPC_DCMP, sink = __aeabi_dcmplt (vd, wd));
	FPC_TIME (QOG_FPC_D2I, sink = __aeabi_d2iz (xd));
	FPC_TIME (QOG_FPC_D2F, vf = __aeabi_d2f (vd));
	FPC_TIME (QOG_FPC_I2D, vd = __aeabi_i2d (vi));
	FPC_TIME (QOG_FPC_DMATH, vd = ceil (xd));
	FPC_TIME (QOG_FPC_IDIV, sink = __aeabi_idiv (vi, di));
	FPC_TIME (QOG_FPC_L64, vl = __aeabi_lmul (vl, kl));
	(void)sink;
}

static int fpc_put (char *out, int room, const char *fmt, const char *name, unsigned v)
{
	int	k = snprintf (out, room, fmt, name, v);

	return k < 0 ? 0 : (k < room ? k : room - 1);
}

void qog_fpcount_report (void (*print) (char *fmt, ...), const char *who, unsigned views,
						 const unsigned *fcalls, const unsigned *dcalls)
{
	static const char	*const ph_names[] = { "setup", "world", "bmodels", "scan", "lmap", "grad",
											  "spans", "zspans", "sky", "turb", "models", "vmodel",
											  "parts", "total" };
	unsigned	c[2][32];
	unsigned	fsum = 0, dsum = 0, fns = 0, dns = 0;
	unsigned long long	f_ns_total = 0, d_ns_total = 0;
	char		line[400];
	int			i, k;

	if (!fpc_ns[0])
		qog_fpcount_calibrate ();
	memcpy (c, (const void *)qog_fpc, sizeof c);
	memset ((void *)qog_fpc, 0, sizeof c);
	if (!views)
		return;

	// calls per view on core 0 by group, then core 1's totals
	k = snprintf (line, sizeof line, "[time %s] fp calls per view core0", who);
	for (i = 0 ; i < QOG_FPC_NGROUPS ; i++)
		k += fpc_put (line + k, (int)sizeof line - k, " %s %u", fpc_names[i], c[0][i] / views);
	k += fpc_put (line + k, (int)sizeof line - k, " %s %u", "core1 f", c[1][QOG_FPC_FTOTAL] / views);
	k += fpc_put (line + k, (int)sizeof line - k, " %s %u", "d", c[1][QOG_FPC_DTOTAL] / views);
	print ("%s\n", line);

	// measured cost of each group, and core 0's helper time per view
	for (i = 0 ; i < QOG_FPC_NGROUPS ; i++)
	{
		unsigned long long	t = (unsigned long long)c[0][i] * fpc_ns[i];

		if (i <= QOG_FPC_FMATH)
			f_ns_total += t, fsum += c[0][i];
		else if (i <= QOG_FPC_DMATH)
			d_ns_total += t, dsum += c[0][i];
	}
	fns = fsum ? (unsigned)(f_ns_total / fsum) : 0;	// mean ns of core 0's float calls
	dns = dsum ? (unsigned)(d_ns_total / dsum) : 0;
	k = snprintf (line, sizeof line, "[time %s] fp ns per call", who);
	for (i = 0 ; i < QOG_FPC_NGROUPS ; i++)
		k += fpc_put (line + k, (int)sizeof line - k, " %s %u", fpc_names[i], fpc_ns[i]);
	k += fpc_put (line + k, (int)sizeof line - k, " %s %u", "meanf", fns);
	k += fpc_put (line + k, (int)sizeof line - k, " %s %u", "meand", dns);
	print ("%s\n", line);

	// estimated helper ms per view, phase by phase (f calls at the mean float
	// cost plus d calls at the mean double cost), tenths of a ms
	k = snprintf (line, sizeof line, "[time %s] fp est ms core0 all %u.%u", who,
				  (unsigned)((f_ns_total + d_ns_total) / views / 1000000u),
				  (unsigned)((f_ns_total + d_ns_total) / views / 100000u % 10u));
	for (i = 0 ; i < (int)(sizeof ph_names / sizeof ph_names[0]) ; i++)
	{
		unsigned long long	ns;
		unsigned			tenths;

		if (i == 3)	// scan holds the surface phases, see the time line's edgescan
			continue;
		ns = (unsigned long long)fcalls[i] * fns + (unsigned long long)dcalls[i] * dns;
		tenths = (unsigned)(ns / views / 100000u);
		k += snprintf (line + k, sizeof line - k > 0 ? sizeof line - k : 0, " %s %u.%u",
					   ph_names[i], tenths / 10u, tenths % 10u);
		if (k >= (int)sizeof line)
			k = (int)sizeof line - 1;
	}
	print ("%s\n", line);
}
