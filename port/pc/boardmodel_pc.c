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

extern unsigned	qog_lms[16];			/* r_surf.c, QOG_LMAP_STATS */
extern cvar_t	r_fast;					/* r_main.c */
extern int		con_current;			/* console.c */
extern float	scr_centertime_start;	/* screen.c */

static const char	*const bm_phname[QOG_PH_N] = { "setup", "world", "bmodels", "scan", "lmap", "grad",
	"spans", "zspans", "sky", "turb", "models", "vmodel", "parts", "total" };
static const char	*const bm_pcname[QOG_PC_N] = { "frames", "litsurfs", "lmcells", "litspans", "litpx",
	"anypx", "runs", "zpx", "skypx", "turbpx" };
static const char	*const bm_wcname[QOG_WC_N] = { "nodes", "leafs", "faces", "clipedge", "emitedge",
	"cached", "project", "clipdot", "rotate", "rottrig" };
static const char	*const bm_lmsname[16] = { "lms_views", "lms_dlights", "lms_builds", "lms_distinct",
	"lms_repeat", "lms_lux", "lms_stylelux", "lms_dlbuilds", "lms_pairs", "lms_pairsin", "lms_pairlux",
	"lms_lit", "lms_rowslit", "lms_marknodes", "lms_marks", "lms_treenodes" };
static const char	*const bm_asname[QAS_N] = { "check", "drawn", "trivial", "subdivm", "verts", "tris",
	"back", "smooth", "zero", "tiny", "spans", "pix", "pixw", "subtri", "subcall", "subpix", "cliptri",
	"parts", "partvis", "partpix" };
static const char	*const bm_bmname[QOG_BM_N] = { "dlpair_int", "dlcell_int", "dlpair_flt", "dlcell_flt",
	"dllit_flt", "reachup", "reachup_step", "grad_fast", "grad_idf", "grad_idd", "scanlines", "scanedges",
	"surfdrawn", "bmsurf", "bmfaces", "bmemit", "bmclip", "bmmark", "bmcheck", "bmsplit", "bpoly",
	"bpolyedge" };

/* every count, as pointers, so one loop takes the differences */
#define BM_MAXCOL	160
#define BM_RANGES	4
static unsigned		*bm_col[BM_MAXCOL];
static char			bm_colname[BM_MAXCOL][24];
static unsigned		bm_last[BM_MAXCOL];
static unsigned		bm_lasttime[QOG_PH_N];
static int			bm_ncol;
static FILE			*bm_log;
static int			bm_tried, bm_lastrows = 200, bm_lastpacked, bm_was, bm_all;
static int			bm_range[BM_RANGES][2], bm_nrange, bm_lastcon;
static float		bm_lastcenter;
static unsigned		bm_bmmark[4];

static void bm_add (unsigned *p, const char *prefix, const char *name)
{
	if (bm_ncol >= BM_MAXCOL)
		Sys_Error ("boardmodel: too many columns");
	bm_col[bm_ncol] = p;
	snprintf (bm_colname[bm_ncol], sizeof bm_colname[0], "%s%s", prefix, name);
	bm_ncol++;
}

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
		bm_add (&qog_ph_count[i], "pc_", bm_pcname[i]);
	for (i = 0 ; i < QOG_WC_N ; i++)
		bm_add (&qog_wc_count[i], "wc_", bm_wcname[i]);
	for (i = 0 ; i < 16 ; i++)
		bm_add (&qog_lms[i], "", bm_lmsname[i]);
	for (i = 0 ; i < QAS_N ; i++)
		bm_add (&qog_astats[0][i], "am_", bm_asname[i]);
	for (i = 0 ; i < QAS_N ; i++)
		bm_add (&qog_astats[1][i], "vm_", bm_asname[i]);
	for (i = 0 ; i < QOG_BM_N ; i++)
		bm_add (&qog_bm_count[i], "bm_", bm_bmname[i]);
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
		" QOG_RTURBO"
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
				bm_last[i] = *bm_col[i];
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
			v = *bm_col[i];
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
