/* QuakeOG (Agent AS): work counts for the board cost model (QOG_BOARDMODEL).
 *
 *   quakeog_pc -bmlog <file> ... +timedemo demo3
 *   quakeog_pc -bmlog <file> -bmall ... (every frame of a level, for scenes other than a timedemo)
 *
 * While a timedemo runs, every VID_Update writes one line to <file>:
 * - the frame, the rows the last VID_Update sent and how many of them held
 *   a packed Turbo view (the scanout this frame's view raced), the rows and
 *   packed rows this frame sends;
 * - up to four byte ranges [lo, hi) of the frame the view handed to
 *   VID_QogScanFree, in order (the board waits there until the scanout has
 *   read them; the PC's own race state depends on the PC's speed, so the
 *   model works the waits out itself);
 * - console lines printed and centre prints started in the frame (on the
 *   board a notify line or centre print makes the whole view wait for the
 *   scanout, for as long as it shows in board time);
 * - this frame's change of every work count the renderer keeps
 *   (qog_phase.h, QOG_LMAP_STATS, QOG_ALIAS_STATS, qog_bmodel.h);
 * - the PC's own phase times in microseconds.
 * The first line names the columns. tools/boardmodel.py turns the lines
 * into board milliseconds. Measurement builds only (tools/boardbench.ps1). */
#ifdef QOG_BOARDMODEL
#include <stdio.h>
#include <string.h>
#include "quakedef.h"
#include "qog_phase.h"
#include "qog_bmodel.h"
#include "r_qogalias.h"

unsigned	qog_bm_count[QOG_BM_N];
#ifdef QOG_DRAW_CORE1
/* core 1's copy of the drawing counts in its own sums (engine/d_core1.h) */
unsigned	c1_qog_bm_count[QOG_BM_N];
unsigned	c1_qog_ph_count[QOG_PC_N];
extern unsigned	c1_qog_lms[16];			/* d_core1s.c, r_surf.c compiled for core 1 */
#define BM_C1(a)	(a)
#else
#define BM_C1(a)	NULL
#endif
#ifdef QOG_AQ_STATS
#include "qog_aqstats.h"
static const char	*const bm_aqname[AQ_N] = { "views", "bents", "bboxrej", "bnotop", "bclipped", "boneleaf",
	"bsurfs", "bfront", "bpoly", "bpolyedge", "bpolysplit", "bfaces", "bmark", "bsplit", "brotang", "bclipedge",
	"bemit", "bproj", "dsurf", "dsub", "dsubsame", "leadz", "nodeax", "nodedot", "cullflt", "fgrad", "fprojflt",
	"fclipvert", "scanlines", "scanedges", "spansgen", "efrags", "bpolyax", "leadzfb", "bboxfb", "bpolyfb",
	"bfrfull", "bfrdist" };
#endif
#ifdef QOG_DRAW_CORE1
static const char	*const bm_c1name[QOG_C1_N] = { "drawt", "waitt", "c1surfs", "surfs", "early", "c0wait",
	"px", "earlypx" };
#endif

extern unsigned	qog_lms[16];			/* r_surf.c, QOG_LMAP_STATS */
extern cvar_t	r_fast;					/* r_main.c */
extern int		con_current;			/* console.c */
extern float	scr_centertime_start;	/* screen.c */

static const char	*const bm_phname[QOG_PH_N] = { "setup", "world", "bmodels", "scan", "lmap", "grad",
	"spans", "zspans", "sky", "turb", "models", "vmodel", "parts", "total" };
static const char	*const bm_pcname[QOG_PC_N] = { "frames", "litsurfs", "lmcells", "litspans", "litpx",
	"anypx", "runs", "zpx", "skypx", "turbpx" };
static const char	*const bm_wcname[QOG_WC_N] = { "nodes", "leafs", "faces", "clipedge", "emitedge",
	"cached", "project", "clipdot", "rotate", "rottrig", "clipfill" };
static const char	*const bm_lmsname[16] = { "lms_views", "lms_dlights", "lms_builds", "lms_distinct",
	"lms_repeat", "lms_lux", "lms_stylelux", "lms_dlbuilds", "lms_pairs", "lms_pairsin", "lms_pairlux",
	"lms_lit", "lms_rowslit", "lms_marknodes", "lms_marks", "lms_treenodes" };
static const char	*const bm_asname[QAS_N] = { "check", "drawn", "trivial", "subdivm", "verts", "tris",
	"back", "smooth", "zero", "tiny", "spans", "pix", "pixw", "subtri", "subcall", "subpix", "cliptri",
	"parts", "partvis", "partpix", "tinytri", "subleaf", "smalltri" };
static const char	*const bm_bmname[QOG_BM_N] = { "dlpair_int", "dlcell_int", "dlpair_flt", "dlcell_flt",
	"dllit_flt", "reachup", "reachup_step", "grad_fast", "grad_idf", "grad_idd", "scanlines", "scanedges",
	"surfdrawn", "bmsurf", "bmfaces", "bmemit", "bmclip", "bmmark", "bmcheck", "bmsplit", "bpoly",
	"bpolyedge", "bmsplitint" };

/* every count, as pointers, so one loop takes the differences */
#define BM_MAXCOL	160
#define BM_RANGES	4
static unsigned		*bm_col[BM_MAXCOL], *bm_col2[BM_MAXCOL];	/* core 0's sum and core 1's, if any */
static char			bm_colname[BM_MAXCOL][24];
static unsigned		bm_last[BM_MAXCOL];
static unsigned		bm_lasttime[QOG_PH_N];
static int			bm_ncol;
static FILE			*bm_log;
static int			bm_tried, bm_lastrows = 200, bm_lastpacked, bm_was, bm_all;
static int			bm_range[BM_RANGES][2], bm_nrange, bm_lastcon;
static float		bm_lastcenter;
static unsigned		bm_bmmark[4];

static void bm_add2 (unsigned *p, unsigned *p1, const char *prefix, const char *name)
{
	if (bm_ncol >= BM_MAXCOL)
		Sys_Error ("boardmodel: too many columns");
	bm_col[bm_ncol] = p;
	bm_col2[bm_ncol] = p1;
	snprintf (bm_colname[bm_ncol], sizeof bm_colname[0], "%s%s", prefix, name);
	bm_ncol++;
}

#define bm_add(p, prefix, name)	bm_add2 (p, NULL, prefix, name)

static void bm_open (void)
{
	int		i;

	bm_tried = 1;
	bm_all = COM_CheckParm ("-bmall") != 0;	// every frame of a level, not only a timedemo's
	i = COM_CheckParm ("-bmlog");
	if (!i || i + 1 >= com_argc)
		return;
	bm_log = fopen (com_argv[i + 1], "w");
	if (!bm_log)
	{
		Con_Printf ("boardmodel: cannot write %s\n", com_argv[i + 1]);
		return;
	}
	for (i = 0 ; i < QOG_PC_N ; i++)
		bm_add2 (&qog_ph_count[i], BM_C1 (&c1_qog_ph_count[i]), "pc_", bm_pcname[i]);
	for (i = 0 ; i < QOG_WC_N ; i++)
		bm_add (&qog_wc_count[i], "wc_", bm_wcname[i]);
	for (i = 0 ; i < 16 ; i++)
		bm_add2 (&qog_lms[i], BM_C1 (&c1_qog_lms[i]), "", bm_lmsname[i]);
	for (i = 0 ; i < QAS_N ; i++)
		bm_add (&qog_astats[0][i], "am_", bm_asname[i]);
	for (i = 0 ; i < QAS_N ; i++)
		bm_add (&qog_astats[1][i], "vm_", bm_asname[i]);
	for (i = 0 ; i < QOG_BM_N ; i++)
		bm_add2 (&qog_bm_count[i], BM_C1 (&c1_qog_bm_count[i]), "bm_", bm_bmname[i]);
#ifdef QOG_AQ_STATS
	for (i = 0 ; i < AQ_N ; i++)
		bm_add (&qog_aq[i], "aq_", bm_aqname[i]);
#endif
#ifdef QOG_DRAW_CORE1
	for (i = 0 ; i < QOG_C1_N ; i++)
		bm_add (&qog_c1_stat[i], "c1_", bm_c1name[i]);
#endif
	// the compile flags that change what a count costs (tools/boardmodel.py FEATURE_SCALE)
	fprintf (bm_log, "#features"
#ifdef QOG_DLIGHT_AXIAL
		" QOG_DLIGHT_AXIAL"
#endif
#ifdef QOG_DLIGHT_INT
		" QOG_DLIGHT_INT"
#endif
#ifdef QOG_GRAD_FAST
		" QOG_GRAD_FAST"
#endif
#ifdef QOG_FRONT_FAST
		" QOG_FRONT_FAST"
#endif
#ifdef QOG_ALIAS_FAST
		" QOG_ALIAS_FAST"
#endif
#ifdef QOG_SPANS_CORE1
		" QOG_SPANS_CORE1"
#endif
#ifdef QOG_LITSPAN_INTERP
		" QOG_LITSPAN_INTERP"
#endif
#ifdef QOG_RTURBO
		" QOG_RTURBO QOG_TURBO_TEXT"	// Agent AP's text list came with this logger
#endif
#ifdef QOG_DRAW_CORE1
		" QOG_DRAW_CORE1"
#endif
#ifdef QOG_LEADZ_FLOAT
		" QOG_LEADZ_FLOAT"
#endif
#ifdef QOG_BPOLY_AXIAL
		" QOG_BPOLY_AXIAL"
#endif
#ifdef QOG_BMODEL_FACING
		" QOG_BMODEL_FACING"
#endif
#ifdef QOG_BMODEL_BBOXF
		" QOG_BMODEL_BBOXF"
#endif
#ifdef QOG_BMODEL_FRUSTUM
		" QOG_BMODEL_FRUSTUM"
#endif
#ifdef QOG_DSURF_NOFRUSTUM
		" QOG_DSURF_NOFRUSTUM"
#endif
#ifdef QOG_FX_REUSE
		" QOG_FX_REUSE"
#endif
#ifdef QOG_FGRAD_AXIAL
		" QOG_FGRAD_AXIAL"
#endif
#ifdef QOG_LITSPAN_Q
		" QOG_LITSPAN_Q"		// Agent AR: the Q span drawer (d_lit_q.h)
#endif
#ifdef QOG_LITQ_SHARELIGHT
		" QOG_LITQ_SHARELIGHT"
#endif
#ifdef QOG_ALIAS_TURBOTINY
		" QOG_ALIAS_TURBOTINY"
#endif
#ifdef QOG_PART_DIEBITS
		" QOG_PART_DIEBITS"
#endif
#ifdef QOG_SCAN_FAST
		" QOG_SCAN_FAST"
#endif
#ifdef QOG_BPOLY_SIDES
		" QOG_BPOLY_SIDES"
#endif
#ifdef QOG_BMSPLIT_INT
		" QOG_BMSPLIT_INT"
#endif
#ifdef QOG_LMAP_FUSE
		" QOG_LMAP_FUSE"
#endif
#if defined(QOG_BMODEL_BBOXINT) && defined(QOG_WORLDNODE_INT)
		" QOG_BMODEL_BBOXINT"
#endif
#ifdef QOG_LEFTEDGE_LOCALS
		" QOG_LEFTEDGE_LOCALS"
#endif
#if defined(QOG_CLIPDIST_FILL4) && defined(QOG_FRONT_FAST)
		" QOG_CLIPDIST_FILL4"
#endif
#if defined(QOG_NODESIDE_BITS) && defined(QOG_WORLDNODE_FAST)
		" QOG_NODESIDE_BITS"
#endif
		"\n");
	fprintf (bm_log, "frame,racerows,racepacked,rows,packed,rfast,vwidth");
	for (i = 0 ; i < BM_RANGES ; i++)
		fprintf (bm_log, ",sf%dlo,sf%dhi", i, i);
	fprintf (bm_log, ",conlines,center");
	for (i = 0 ; i < bm_ncol ; i++)
		fprintf (bm_log, ",%s", bm_colname[i]);
	for (i = 0 ; i < QOG_PH_N ; i++)
		fprintf (bm_log, ",us_%s", bm_phname[i]);
	fprintf (bm_log, "\n");
}

void qog_bm_bmodels (int begin)
{
	if (begin)
	{
		bm_bmmark[0] = qog_wc_count[QOG_WC_FACES];
		bm_bmmark[1] = qog_wc_count[QOG_WC_EMITEDGE];
		bm_bmmark[2] = qog_wc_count[QOG_WC_CLIPEDGE];
		bm_bmmark[3] = qog_lms[13];
		return;
	}
	qog_bm_count[QOG_BM_BMFACES] += qog_wc_count[QOG_WC_FACES] - bm_bmmark[0];
	qog_bm_count[QOG_BM_BMEMIT] += qog_wc_count[QOG_WC_EMITEDGE] - bm_bmmark[1];
	qog_bm_count[QOG_BM_BMCLIP] += qog_wc_count[QOG_WC_CLIPEDGE] - bm_bmmark[2];
	qog_bm_count[QOG_BM_BMMARK] += qog_lms[13] - bm_bmmark[3];
}

void qog_bm_scanfree (int lo, int hi)
{
	if (bm_nrange < BM_RANGES)
	{
		bm_range[bm_nrange][0] = lo;
		bm_range[bm_nrange][1] = hi;
		bm_nrange++;
	}
}

void qog_bm_frame (int rows, int packed)
{
	int			i;
	unsigned	v, d;

	if (!bm_tried)
		bm_open ();
	if (bm_log && (cls.timedemo || (bm_all && cls.signon == SIGNONS && cl.worldmodel)))
	{
		if (!bm_was)
		{	// the first frame of a timedemo: differences start here
			for (i = 0 ; i < bm_ncol ; i++)
				bm_last[i] = *bm_col[i] + (bm_col2[i] ? *bm_col2[i] : 0);
			memcpy (bm_lasttime, qog_ph_time, sizeof bm_lasttime);
			bm_lastcon = con_current;
			bm_lastcenter = scr_centertime_start;
		}
		fprintf (bm_log, "%d,%d,%d,%d,%d,%d,%d", host_framecount, bm_lastrows, bm_lastpacked,
				 rows, packed, (int)r_fast.value, r_refdef.vrect.width);
		for (i = 0 ; i < BM_RANGES ; i++)
		{
			if (i < bm_nrange)
				fprintf (bm_log, ",%d,%d", bm_range[i][0], bm_range[i][1]);
			else
				fprintf (bm_log, ",0,0");
		}
		fprintf (bm_log, ",%d,%d", con_current - bm_lastcon, scr_centertime_start != bm_lastcenter);
		bm_lastcon = con_current;
		bm_lastcenter = scr_centertime_start;
		for (i = 0 ; i < bm_ncol ; i++)
		{
			v = *bm_col[i] + (bm_col2[i] ? *bm_col2[i] : 0);
			d = v >= bm_last[i] ? v - bm_last[i] : v;	// a count cleared meanwhile (a new level)
			bm_last[i] = v;
			fprintf (bm_log, ",%u", d);
		}
		for (i = 0 ; i < QOG_PH_N ; i++)
		{
			d = qog_ph_time[i] - bm_lasttime[i];
			bm_lasttime[i] = qog_ph_time[i];
			fprintf (bm_log, ",%.0f", qog_phase_us (d));
		}
		fprintf (bm_log, "\n");
		bm_was = 1;
	}
	else if (bm_was)
	{
		bm_was = 0;
		if (bm_log)
			fflush (bm_log);
	}
	bm_nrange = 0;
	bm_lastrows = rows;
	bm_lastpacked = packed;
}
#endif
