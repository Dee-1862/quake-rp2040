/* QuakeOG, PC measurement only (QOG_AQ_STATS). Work counts of the
 * brush entity front end, the world walk and the edge scan per view, printed
 * every 100 views as sums since the start (r_main.c). Without the flag the
 * macro is empty. */
#ifndef QOG_AQSTATS_H
#define QOG_AQSTATS_H

enum
{
	AQ_VIEWS,		/* R_RenderView_ calls */
	AQ_BENTS,		/* brush entities in cl_visedicts */
	AQ_BBOXREJ,		/*   rejected by R_BmodelCheckBBox */
	AQ_BNOTOP,		/*   past it, but in no visible leaf (no top node) */
	AQ_BCLIPPED,	/*   clipped to the world BSP */
	AQ_BONELEAF,	/*   in one leaf */
	AQ_BSURFS,		/* brush surfaces tested for facing */
	AQ_BFRONT,		/*   of which facing the eye */
	AQ_BPOLY,		/* R_RecursiveClipBPoly calls */
	AQ_BPOLYEDGE,	/*   edges tested there */
	AQ_BPOLYSPLIT,	/*   edges split there */
	AQ_BFACES,		/* R_RenderBmodelFace calls */
	AQ_BMARK,		/* R_MarkLights calls from the brush entity loop */
	AQ_BSPLIT,		/* R_SplitEntityOnNode2 calls */
	AQ_BROTANG,		/* brush entities drawn with an angle not zero */
	AQ_BCLIPEDGE,	/* R_ClipEdge calls of brush entities */
	AQ_BEMIT,		/* R_EmitEdge calls of brush entities */
	AQ_BPROJ,		/* points projected for brush entities */
	AQ_DSURF,		/* surfaces drawn by D_DrawSurfaces */
	AQ_DSUB,		/*   of which brush entity surfaces */
	AQ_DSUBSAME,	/*     of the same entity as the brush surface before */
	AQ_LEADZ,		/* R_LeadingEdge 1/z compares of brush surfaces */
	AQ_NODEAX,		/* world node side tests on an axial plane */
	AQ_NODEDOT,		/* world node side tests with a dot product */
	AQ_CULLFLT,		/* world node box tests that took id's floats */
	AQ_FGRAD,		/* face gradients (Fast) */
	AQ_FPROJFLT,	/* Fast points that took id's float projection */
	AQ_FCLIPVERT,	/* clipped points made in R_ClipEdge */
	AQ_SCANLINES,	/* scan lines of R_ScanEdges */
	AQ_SCANEDGES,	/* active edges stepped in R_ScanEdges */
	AQ_SPANSGEN,	/* spans generated */
	AQ_EFRAGS,		/* R_StoreEfrags entities */
	AQ_BPOLYAX,		/* R_RecursiveClipBPoly calls on an axial plane */
	AQ_LEADZFB,		/* 1/z compares that needed id's doubles */
	AQ_BBOXFB,		/* bbox plane tests that needed more than a float compare */
	AQ_BPOLYFB,		/* clip side tests that took id's floats */
	AQ_BFRFULL,		/* brush entity view planes made by R_TransformFrustum */
	AQ_BFRDIST,		/*   made from the world's normals, dists only */
	AQ_LEADZ2,		/* 1/z compares inside R_LeadingEdge's search loop */
	AQ_LEADZHIT,	/*   that found surf's half made for the edge already */
	AQ_LEADZMID,	/* 1/z compares the bits of the two sums left to float */
	AQ_LEADZNEAR,	/*   decided as within 0.4% */
	AQ_CLIPSAME,	/* clipped point coordinates taken from an end (no float) */
	AQ_N
};

#ifdef QOG_AQ_STATS
extern unsigned	qog_aq[AQ_N];
#define QOG_AQ(c, n)	(qog_aq[c] += (unsigned)(n))
#else
#define QOG_AQ(c, n)
#endif

#endif
