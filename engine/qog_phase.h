/* QuakeOG: where a frame's render time goes (QOG_PHASE_TIME).
 *
 * The renderer adds the time of each phase to qog_ph_time[] and a few work
 * counts to qog_ph_count[]. Hosts print and clear them: the board's
 * fly-through every 2 s over USB (apps/qogfly), the PC with `qog_phase`
 * (console) and at the end of `-qogfly`. Off in every shipped build. Without
 * QOG_PHASE_TIME the macros are empty and nothing is linked.
 *
 * The clock is the RP2040's 1 MHz timer on the board (one load, unbiased
 * over many short intervals) and QueryPerformanceCounter on the PC.
 * qog_phase_us() converts ticks to microseconds. */
#ifndef QOG_PHASE_H
#define QOG_PHASE_H

enum
{
	QOG_PH_SETUP,		/* R_SetupFrame and R_MarkLeaves */
	QOG_PH_WORLD,		/* R_RenderWorld: BSP walk, clipping, edge emission */
	QOG_PH_BMODELS,		/* R_DrawBEntitiesOnList: brush entity edges */
	QOG_PH_SCAN,		/* R_ScanEdges, including all of D_DrawSurfaces */
	QOG_PH_LIGHTMAP,	/* D_SetupLitSurface: R_BuildLightMap and surface setup */
	QOG_PH_GRADIENTS,	/* D_CalcGradients and the mip level */
	QOG_PH_SPANS,		/* D_DrawSpans8Lit */
	QOG_PH_ZSPANS,		/* D_DrawZSpans */
	QOG_PH_SKY,			/* D_DrawSkyScans8 */
	QOG_PH_TURB,		/* Turbulent8 (water, slime, lava, teleports) */
	QOG_PH_ENTITIES,	/* R_DrawEntitiesOnList: alias models and sprites */
	QOG_PH_VIEWMODEL,	/* R_DrawViewModel */
	QOG_PH_PARTICLES,	/* R_DrawParticles */
	QOG_PH_TOTAL,		/* R_RenderView_ */
	QOG_PH_N
};

enum
{
	QOG_PC_FRAMES,		/* R_RenderView_ calls */
	QOG_PC_LITSURFS,	/* lit surfaces drawn (one light grid each) */
	QOG_PC_LMCELLS,		/* light grid cells built */
	QOG_PC_LITSPANS,	/* lit spans */
	QOG_PC_LITPX,		/* lit pixels */
	QOG_PC_LITANYPX,	/* lit pixels on the any-size path (not a power of two) */
	QOG_PC_RUNS,		/* lit runs of up to 8 pixels (one float divide each) */
	QOG_PC_ZPX,			/* z span pixels */
	QOG_PC_SKYPX,		/* sky pixels */
	QOG_PC_TURBPX,		/* turbulent pixels */
	QOG_PC_N
};

#ifdef QOG_PHASE_TIME
extern unsigned	qog_ph_time[QOG_PH_N];
extern unsigned	qog_ph_count[QOG_PC_N];
unsigned qog_phase_clock (void);
float qog_phase_us (unsigned ticks);
/* Prints one summary (per frame means) through print and clears the sums. */
void qog_phase_report (void (*print) (char *fmt, ...));
#define QOG_PH_T(v)			unsigned v = qog_phase_clock ()
#define QOG_PH_ADD(ph, v)	(qog_ph_time[ph] += qog_phase_clock () - (v))
#define QOG_PH_COUNT(c, n)	(qog_ph_count[c] += (unsigned)(n))
#else
#define QOG_PH_T(v)
#define QOG_PH_ADD(ph, v)
#define QOG_PH_COUNT(c, n)
#endif

#endif
