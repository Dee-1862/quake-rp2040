/* QuakeOG (Agent AS): work counts for the board cost model (QOG_BOARDMODEL).
 *
 * PC measurement builds only (tools/boardbench.ps1). Each count says which
 * code path did the work, so tools/boardmodel.py can give each path its own
 * board cost. port/pc/boardmodel_pc.c writes every count once a frame while
 * a timedemo runs. Without QOG_BOARDMODEL the macros are empty. */
#ifndef QOG_BMODEL_H
#define QOG_BMODEL_H

enum
{
	QOG_BM_DLPAIR_INT,		/* dynamic light and surface pairs done in integers (QOG_DLIGHT_INT) */
	QOG_BM_DLCELL_INT,		/*   their light grid cells */
	QOG_BM_DLPAIR_FLT,		/* pairs done by id's float loop */
	QOG_BM_DLCELL_FLT,		/*   their cells */
	QOG_BM_DLLIT_FLT,		/*   of them lit (5 more float calls each) */
	QOG_BM_REACHUP,			/* lost light questions answered by R_QogLightReachesWorld */
	QOG_BM_REACHUP_STEP,	/*   its plane tests */
	QOG_BM_GRAD_FAST,		/* D_CalcGradients by D_QogGradFast (QOG_GRAD_FAST) */
	QOG_BM_GRAD_IDF,		/* id's D_CalcGradients without double arithmetic (QOG_GRAD_EXACTF) */
	QOG_BM_GRAD_IDD,		/* id's D_CalcGradients with its doubles */
	QOG_BM_SCANLINES,		/* scan lines R_ScanEdges stepped */
	QOG_BM_SCANEDGES,		/* edges visited by R_GenerateSpans, summed over the scan lines */
	QOG_BM_SURFDRAWN,		/* surfaces D_DrawSurfaces drew with spans */
	QOG_BM_BMSURF,			/*   of them brush entity surfaces (a rotate and restore each) */
	QOG_BM_BMFACES,			/* R_RenderFace calls inside the bmodels phase */
	QOG_BM_BMEMIT,			/* R_EmitEdge calls inside the bmodels phase */
	QOG_BM_BMCLIP,			/* R_ClipEdge calls inside the bmodels phase */
	QOG_BM_BMMARK,			/* R_MarkLights nodes inside the bmodels phase (brush entity dynamic lights) */
	QOG_BM_BMCHECK,			/* brush entities R_BmodelCheckBBox tested */
	QOG_BM_BMSPLIT,			/* R_SplitEntityOnNode2 plane tests */
	QOG_BM_BPOLY,			/* R_RecursiveClipBPoly calls (brush polygons clipped to the world tree) */
	QOG_BM_BPOLYEDGE,		/*   edges they clipped */
	QOG_BM_N
};

#ifdef QOG_BOARDMODEL
extern unsigned	qog_bm_count[QOG_BM_N];
#define QOG_BM(c, n)	(qog_bm_count[c] += (unsigned)(n))
/* port/pc/boardmodel_pc.c */
void qog_bm_bmodels (int begin);		/* around R_DrawBEntitiesOnList */
void qog_bm_scanfree (int lo, int hi);	/* VID_QogScanFree: the view writes frame bytes [lo, hi) next */
void qog_bm_frame (int rows, int packed);	/* VID_Update: the frame ends, rows and packed rows sent */
#else
#define QOG_BM(c, n)	((void)0)
#endif

#endif
