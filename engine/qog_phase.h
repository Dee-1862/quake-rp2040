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

/* QuakeOG (Agent V): where a whole frame's time goes, outside the 3D view.
 * The client half and the single-process game fill the QOG_HP_ entries up
 * to QOG_HP_SOUND, the split server half the QOG_HP_SV ones. The board
 * prints them every 2 s (qog_phase_board_report, port/rp2040/sys_rp2040.c). */
enum
{
	QOG_HP_FRAME,		/* a Host_Frame that ran, from Host_FilterTime to its end */
	QOG_HP_INPUT,		/* keys, the command buffer and CL_SendCmd */
	QOG_HP_LINK,		/* split client: waiting for the server half's frame */
	QOG_HP_READ,		/* CL_ReadFromServer: parse, entities, particles, lights */
	QOG_HP_SCREEN,		/* SCR_UpdateScreen */
	QOG_HP_VIEW,		/* V_RenderView inside it (R_RenderView, HUD repaint, crosshair) */
	QOG_HP_HUD,			/* R_RenderView's status bar repaint (QOG_HUD_IN_ZBUFFER) */
	QOG_HP_PRESENT,		/* VID_Update: hand the frame to the LCD (and wait, unless QOG_VID_LOCKWAIT) */
	QOG_HP_SCANWAIT,	/* QOG_VID_LOCKWAIT: waiting for the last scanout before drawing */
	QOG_HP_SOUND,		/* S_Update and CL_DecayLights */
	QOG_HP_SVWAIT,		/* split server: waiting for the client's frame (Link_ServerWaitTick) */
	QOG_HP_SVFRAME,		/* split server: Host_ServerFrame */
	QOG_HP_SVPHYS,		/*   of which SV_RunClients and SV_Physics */
	QOG_HP_SVSEND,		/*   of which SV_SendClientMessages */
	QOG_HP_N
};

/* QuakeOG (Agent Z): measurement builds only (QOG_PHASE_XIP, needs
 * QOG_PHASE_TIME). The world front end's work per view, and on the board
 * the XIP cache's accesses and hits in each render phase (XIP_CTRL CTR_ACC
 * and CTR_HIT, both cores together). With QOG_PHASE_FPCOUNT the board also
 * counts the float and double helper calls the engine library makes, per
 * core (port/rp2040/qog_fpcount.c). */
enum
{
	QOG_WC_NODES,		/* R_RecursiveWorldNode calls */
	QOG_WC_LEAFS,		/*   of which visible leafs */
	QOG_WC_FACES,		/* R_RenderFace calls */
	QOG_WC_CLIPEDGE,	/* R_ClipEdge calls (with its recursion) */
	QOG_WC_EMITEDGE,	/* R_EmitEdge calls */
	QOG_WC_CACHED,		/* edges reused from the edge cache */
	QOG_WC_PROJECT,		/* points projected (R_EmitEdge, not from QOG_VERTCACHE) */
	QOG_WC_CLIPDOT,		/* plane distances computed in R_ClipEdge (not from QOG_VERTCACHE_CLIPDIST) */
	QOG_WC_N
};

#ifdef QOG_PHASE_TIME
extern unsigned	qog_ph_time[QOG_PH_N];
extern unsigned	qog_ph_count[QOG_PC_N];
extern unsigned	qog_hp_time[QOG_HP_N];
extern unsigned	qog_hp_frames;	/* frames that ran (QOG_HP_FRAME's count) */
unsigned qog_phase_clock (void);
float qog_phase_us (unsigned ticks);
/* Prints one summary (per frame means) through print and clears the sums. */
void qog_phase_report (void (*print) (char *fmt, ...));
/* QuakeOG (Agent V): the board's summary in integers, then clears every
   sum. who is "cl" (the client's frame and render lines) or "sv" (the
   server's line). elapsed_us is the time since the last call. */
void qog_phase_board_report (void (*print) (char *fmt, ...), const char *who, unsigned elapsed_us);
#define QOG_PH_COUNT(c, n)	(qog_ph_count[c] += (unsigned)(n))
#ifdef QOG_PHASE_XIP
/* Agent Z: a phase mark also holds the XIP counters and core 0's helper
   call counts, and each phase adds their change while it ran. */
typedef struct
{
	unsigned	t, acc, hit, fcalls, dcalls;
} qog_phmark_t;
extern unsigned	qog_ph_xacc[QOG_PH_N], qog_ph_xhit[QOG_PH_N];
extern unsigned	qog_ph_fcalls[QOG_PH_N], qog_ph_dcalls[QOG_PH_N];
extern unsigned	qog_wc_count[QOG_WC_N];
void qog_phase_mark (qog_phmark_t *m);
void qog_phase_add (int ph, const qog_phmark_t *m);
#define QOG_PH_T(v)			qog_phmark_t v; qog_phase_mark (&v)
#define QOG_PH_ADD(ph, v)	qog_phase_add (ph, &(v))
#define QOG_HP_ADD(ph, v)	(qog_hp_time[ph] += qog_phase_clock () - (v).t)
#define QOG_WC(c)			(qog_wc_count[c]++)
#else
#define QOG_PH_T(v)			unsigned v = qog_phase_clock ()
#define QOG_PH_ADD(ph, v)	(qog_ph_time[ph] += qog_phase_clock () - (v))
#define QOG_HP_ADD(ph, v)	(qog_hp_time[ph] += qog_phase_clock () - (v))
#define QOG_WC(c)
#endif
#else
#define QOG_PH_T(v)
#define QOG_PH_ADD(ph, v)
#define QOG_PH_COUNT(c, n)
#define QOG_HP_ADD(ph, v)
#define QOG_WC(c)
#endif

#endif
