/* QuakeOG, float and double helper call counts, measurement
 * builds of the board client only (QOG_PHASE_FPCOUNT, apps/quake option
 * QOG_BOARD_FPCOUNT).
 *
 * The build renames the engine library's calls to the compiler's helpers
 * (__aeabi_fmul, __aeabi_dadd, ceil and the rest) to counting stubs in RAM
 * (port/rp2040/qog_fpcount.S), which add one to a counter of the calling
 * core and go on to the SDK's routine. Each core has 32 counters, one per
 * group below, and a total of its float and of its double calls. */
#ifndef QOG_FPCOUNT_H
#define QOG_FPCOUNT_H

enum
{
	QOG_FPC_FADD, QOG_FPC_FSUB, QOG_FPC_FMUL, QOG_FPC_FDIV,
	QOG_FPC_FCMP,		/* every float compare */
	QOG_FPC_F2I,		/* float to int and to unsigned */
	QOG_FPC_I2F,		/* int and unsigned to float */
	QOG_FPC_F2D,		/* float to double */
	QOG_FPC_FMATH,		/* sqrtf, expf, logf, atan2f */
	QOG_FPC_DADD, QOG_FPC_DSUB, QOG_FPC_DMUL, QOG_FPC_DDIV,
	QOG_FPC_DCMP,		/* every double compare */
	QOG_FPC_D2I,		/* double to int and to unsigned */
	QOG_FPC_D2F,		/* double to float */
	QOG_FPC_I2D,		/* int to double */
	QOG_FPC_DMATH,		/* ceil, floor, sqrt, sin, cos, tan, pow, exp, log, atan, atan2 */
	QOG_FPC_IDIV,		/* 32-bit integer divides (the SIO divider) */
	QOG_FPC_L64,		/* 64-bit multiply and divide */
	QOG_FPC_NGROUPS,
	QOG_FPC_ITOTAL = 29,	/* integer groups */
	QOG_FPC_FTOTAL = 30,	/* float groups */
	QOG_FPC_DTOTAL = 31		/* double groups */
};

extern volatile unsigned	qog_fpc[2][32];	/* [core][group] */

/* Prints the per view helper calls of both cores and the measured cost of
   each group (timed once on core 0), and core 0's estimated float time in
   each render phase from its f/d calls there. */
void qog_fpcount_report (void (*print) (char *fmt, ...), const char *who, unsigned views,
						 const unsigned *fcalls, const unsigned *dcalls);

#endif
