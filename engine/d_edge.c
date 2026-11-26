/* Changed by the QuakeOG project, 2026 (last change November 2026). The git history has the details. */
/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// d_edge.c

#include "quakedef.h"
#include "d_local.h"
#include "qog_phase.h"	// QuakeOG, render phase times (QOG_PHASE_TIME)
#include "qog_bmodel.h"	// QuakeOG, board cost model counts (QOG_BOARDMODEL)
#include "qog_aqstats.h"	// QuakeOG, front end work counts (QOG_AQ_STATS)

#if defined(QOG_PHASE_TIME) || defined(QOG_C1_PHCOUNT)	// QuakeOG, core 1 counts too in the cost model's builds
// QuakeOG, pixels in a span list, for the phase counts
static int D_QogSpanPixels (espan_t *pspan)
{
	int	n = 0;

	do
		n += pspan->count;
	while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR (pspan->pnext)));
	return n;
}
#define QOG_ZSPANS(sp)	{ QOG_PH_T (ph_); D_DrawZSpans (sp); QOG_PH_ADD (QOG_PH_ZSPANS, ph_); 						  QOG_PH_COUNT (QOG_PC_ZPX, D_QogSpanPixels (sp)); }
#else
#define QOG_ZSPANS(sp)	D_DrawZSpans (sp)
#endif

static int	miplevel;

float		scale_for_mip;
int			screenwidth;
int			ubasestep, errorterm, erroradjustup, erroradjustdown;
int			vstartscan;

// FIXME: should go away
extern void			R_RotateBmodel (void);
extern void			R_TransformFrustum (void);
#ifdef QOG_DSURF_NOFRUSTUM
/*
QuakeOG, id turned the view planes for every brush entity
surface it drew (R_RotateBmodel) and back again after it (two
R_TransformFrustum calls, about 80 float operations each), and transformed
the entity's origin each time. Nothing that draws spans reads the view
planes, so the surfaces turn only the vectors and modelorg
(R_QogRotateBmodelVecs) and the view planes are set back once at the end of
D_DrawSurfaces from the same vectors and modelorg as id's last restore,
which gives the same bits. The transformed origin is kept for the entity
drawn last, from the same inputs (the vectors are the world's whenever it is
made).
*/
extern void			R_QogRotateBmodelVecs (void);
extern vec3_t		transformed_modelorg;
static byte			d_qog_frustumdirty;
static entity_t		*d_qog_xfent;
static vec3_t		d_qog_xfmodelorg;

static void D_QogSubmodelBegin (surf_t *s)
{
	vec3_t	local_modelorg;

	currententity = SURF_ENTITY (s);
	if (currententity != d_qog_xfent)
	{
		VectorSubtract (r_origin, currententity->origin, local_modelorg);
		TransformVector (local_modelorg, d_qog_xfmodelorg);
		d_qog_xfent = currententity;
	}
	VectorCopy (d_qog_xfmodelorg, transformed_modelorg);
	R_QogRotateBmodelVecs ();
	d_qog_frustumdirty = true;
}
#endif

vec3_t		transformed_modelorg;

/*
==============
D_DrawPoly

==============
*/
void D_DrawPoly (void)
{
// this driver takes spans, not polygons
}


/*
=============
D_MipLevelForScale
=============
*/
int D_MipLevelForScale (float scale)
{
	int		lmiplevel;

	if (scale >= d_scalemip[0] )
		lmiplevel = 0;
	else if (scale >= d_scalemip[1] )
		lmiplevel = 1;
	else if (scale >= d_scalemip[2] )
		lmiplevel = 2;
	else
		lmiplevel = 3;

	if (lmiplevel < d_minmip)
		lmiplevel = d_minmip;

	return lmiplevel;
}


/*
==============
D_DrawSolidSurface
==============
*/

// FIXME: clean this up

void D_DrawSolidSurface (surf_t *surf, int color)
{
	espan_t	*span;
	byte	*pdest;
	int		u, u2, pix;
	
	pix = (color<<24) | (color<<16) | (color<<8) | color;
	// QuakeOG, SPAN_PTR, see espan_t in r_shared.h
	for (span=surf->spans ? SPAN_PTR(surf->spans) : NULL ; span ;
		span=span->pnext ? SPAN_PTR(span->pnext) : NULL)
	{
		pdest = (byte *)d_viewbuffer + screenwidth*span->v;
		u = span->u;
		u2 = span->u + span->count - 1;
		((byte *)pdest)[u] = pix;

		if (u2 - u < 8)
		{
			for (u++ ; u <= u2 ; u++)
				((byte *)pdest)[u] = pix;
		}
		else
		{
			for (u++ ; u & 3 ; u++)
				((byte *)pdest)[u] = pix;

			u2 -= 4;
			for ( ; u <= u2 ; u+=4)
				*(int *)((byte *)pdest + u) = pix;
			u2 += 4;
			for ( ; u <= u2 ; u++)
				((byte *)pdest)[u] = pix;
		}
	}
}


#ifdef QOG_GRAD_EXACTF
/*
==============
D_QogRoundHalf

QuakeOG, id's (fixed16_t)(x + 0.5) with x a float and 0.5 a
double, without double arithmetic. Below 2^23 in magnitude the float sum
x + 0.5f truncates to the same integer as the exact sum, either the sum is
exact, or the rounding cannot reach an integer (the distance from the
exact sum to the next integer away from zero is a multiple of x's spacing,
which is at least half the sum's spacing, and the one case of a tie lies
across a power of two the sum cannot reach). From 2^23 the float x is a
whole number, so the truncation of x + 0.5 is x, or x + 1 when x is
negative. From 2^31 id's conversion overflows, and the double is kept.
==============
*/
static int D_QogRoundHalf (float x)
{
	if (x > -8388608.0f && x < 8388608.0f)
		return (int)(x + 0.5f);
	if (x >= -2147483648.0f && x < 2147483648.0f)
		return (int)x + (x < 0);
	return (fixed16_t)(x + 0.5);
}

// QuakeOG, id's 1.0 / (float)(1 << miplevel), the same floats
static const float	d_qogmipscale[4] = {1.0f, 0.5f, 0.25f, 0.125f};
#define D_QOG_HALF(x)	D_QogRoundHalf (x)
#else
#define D_QOG_HALF(x)	((fixed16_t)((x) + 0.5))
#endif

#if defined(QOG_GRAD_FAST) && defined(QOG_RFAST)
#ifndef QOG_GRAD_EXACTF
#error "QOG_GRAD_FAST needs QOG_GRAD_EXACTF"
#endif
extern int	r_qogfast;	// r_main.c, r_fast for the current view
#ifndef QOG_NOINLINE
#if defined(__GNUC__)
#define QOG_NOINLINE	__attribute__((noinline))
#elif defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE
#endif
#endif
/*
==============
D_QogGradFast

QuakeOG, Fast mode (r_fast 1) gradients of a world surface,
from rows made once a view, in integers, as the earlier face 1/z gradients.
id's s/z steps and origin are a linear map of the texture axis vecs[0].
d_sdivzstepu = (vright . v) / xscale, d_sdivzstepv = -(vup . v) / yscale
and d_sdivzorigin = (vpn - xcenter vright / xscale + ycenter vup / yscale)
. v, each times the mip scale, the same for t with vecs[1]. The three rows
are kept in 2^13 to 2^14 times a power of two, the axis in 1/16384, so a
surface costs three integer dot products and three float multiplies per
axis instead of two TransformVector calls and about 45 float operations.
sadjust is the eye's texture coordinate, modelorg . v + v[3] (the view
rotation drops out of id's dot product), here in 64-bit integers from the
float bits (modelorg in 1/1024 units, v in 2^-24). Axes longer than 2 (a
texture scaled below one half) and brush entities keep id's float code.
The rows use 76 B of static RAM.
==============
*/
static struct
{
	int		frame;			// r_framecount the rows are for, 0 none
	int		ok;				// the values fit
	int		row[3][3];		// the rows in 1/sc
	float	rk[3];			// 1 / (sc * 16384)
	int		org[3];			// modelorg in 1/1024 units
} d_qoggr;

// (int)(f * 2^frac), truncated, from the float's bits, |result| below 2^30
static int D_QogGrFix (float f, int frac)
{
	union
	{
		float	f;
		int		i;
	} b;
	int		e, m;

	b.f = f;
	e = ((b.i >> 23) & 0xff) - (127 + 23) + frac;
	if (e <= -24)
		return 0;
	if (e > 6)
		return b.i < 0 ? -0x3fffffff : 0x3fffffff;
	m = (b.i & 0x7fffff) | 0x800000;
	m = e >= 0 ? m << e : m >> -e;
	return b.i < 0 ? -m : m;
}

static QOG_NOINLINE void D_QogGradRows (void)
{
	float	gr[3][3], m, sc, v;
	int		i, k;

	d_qoggr.frame = r_framecount;
	d_qoggr.ok = 0;
	for (i=0 ; i<3 ; i++)
	{
		if (modelorg[i] > 65536 || modelorg[i] < -65536)
			return;
		d_qoggr.org[i] = D_QogGrFix (modelorg[i], 10);
		gr[0][i] = vright[i] * xscaleinv;
		gr[1][i] = -vup[i] * yscaleinv;
		gr[2][i] = vpn[i] - xcenter * gr[0][i] - ycenter * gr[1][i];
	}
	for (k=0 ; k<3 ; k++)
	{
		m = 0;
		for (i=0 ; i<3 ; i++)
			if (gr[k][i] > m || -gr[k][i] > m)
				m = gr[k][i] > 0 ? gr[k][i] : -gr[k][i];
		if (m > 65536 || m < 1.0f / 65536)
			return;
		sc = 1;
		while (m * sc >= 16384)
			sc *= 0.5f;
		while (m * sc < 8192)
			sc *= 2;
		for (i=0 ; i<3 ; i++)
		{
			v = gr[k][i] * sc;
			d_qoggr.row[k][i] = (int)(v + (v >= 0 ? 0.5f : -0.5f));
		}
		d_qoggr.rk[k] = 1.0f / (sc * 16384);
	}
	d_qoggr.ok = 1;
}

static QOG_NOINLINE int D_QogGradFast (msurface_t *pface)
{
	mtexinfo_t	*ti = SURF_TEXINFO (currententity->model, pface);
	int			a, i, q24[3], q14[3], g;
	long long	S;
	float		mipscale = d_qogmipscale[miplevel];
	float		*out[3], rkm[3];
	int			q[2][3];

	if (d_qoggr.frame != r_framecount)
		D_QogGradRows ();
	if (!d_qoggr.ok)
		return 0;
	for (a=0 ; a<2 ; a++)
		for (i=0 ; i<3 ; i++)
		{
			q[a][i] = D_QogGrFix (ti->vecs[a][i], 24);
			if (q[a][i] > (1 << 25) || q[a][i] < -(1 << 25))
				return 0;	// longer than 2 (D_QogGrFix saturates above 64)
		}
	for (i=0 ; i<3 ; i++)
		rkm[i] = d_qoggr.rk[i] * mipscale;
	for (a=0 ; a<2 ; a++)
	{
		if (a == 0)
		{
			out[0] = &d_sdivzstepu;
			out[1] = &d_sdivzstepv;
			out[2] = &d_sdivzorigin;
		}
		else
		{
			out[0] = &d_tdivzstepu;
			out[1] = &d_tdivzstepv;
			out[2] = &d_tdivzorigin;
		}
		for (i=0 ; i<3 ; i++)
		{
			q24[i] = q[a][i];
			q14[i] = (q24[i] + 512) >> 10;
		}
		for (i=0 ; i<3 ; i++)
		{
			g = d_qoggr.row[i][0] * q14[0] + d_qoggr.row[i][1] * q14[1] + d_qoggr.row[i][2] * q14[2];
			*out[i] = (float)g * rkm[i];
		}
		// the eye's coordinate in 2^-34 texels, then 16.16 at this mip
		S = (long long)d_qoggr.org[0] * q24[0] + (long long)d_qoggr.org[1] * q24[1]
			+ (long long)d_qoggr.org[2] * q24[2]
			+ ((long long)D_QogGrFix (ti->vecs[a][3], 10) << 24);
		g = (int)(S >> (18 + miplevel)) - ((pface->texturemins[a] << 16) >> miplevel);
		if (a == 0)
			sadjust = g;
		else
			tadjust = g;
	}
	bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
	bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
	return 1;
}
#endif

/*
==============
D_CalcGradients
==============
*/
void D_CalcGradients (msurface_t *pface)
{
	mplane_t	*pplane;
	float		mipscale;
	vec3_t		p_temp1;
	vec3_t		p_saxis, p_taxis;
	float		t;

#if defined(QOG_GRAD_FAST) && defined(QOG_RFAST)
	if (r_qogfast && currententity == &cl_entities[0] && D_QogGradFast (pface))
	{
		QOG_BM (QOG_BM_GRAD_FAST, 1);	// QuakeOG
		return;	// QuakeOG, Fast mode, world surfaces
	}
#endif
#ifdef QOG_GRAD_EXACTF
	QOG_BM (QOG_BM_GRAD_IDF, 1);	// QuakeOG
#else
	QOG_BM (QOG_BM_GRAD_IDD, 1);
#endif
	pplane = SURF_PLANE (currententity->model, pface);	// QuakeOG

#ifdef QOG_GRAD_EXACTF
	mipscale = d_qogmipscale[miplevel];	// QuakeOG, the same float
#else
	mipscale = 1.0 / (float)(1 << miplevel);
#endif

	TransformVector (SURF_TEXINFO (currententity->model, pface)->vecs[0], p_saxis);
	TransformVector (SURF_TEXINFO (currententity->model, pface)->vecs[1], p_taxis);

	t = xscaleinv * mipscale;
	d_sdivzstepu = p_saxis[0] * t;
	d_tdivzstepu = p_taxis[0] * t;

	t = yscaleinv * mipscale;
	d_sdivzstepv = -p_saxis[1] * t;
	d_tdivzstepv = -p_taxis[1] * t;

	d_sdivzorigin = p_saxis[2] * mipscale - xcenter * d_sdivzstepu -
			ycenter * d_sdivzstepv;
	d_tdivzorigin = p_taxis[2] * mipscale - xcenter * d_tdivzstepu -
			ycenter * d_tdivzstepv;

	VectorScale (transformed_modelorg, mipscale, p_temp1);

	t = 0x10000*mipscale;
	sadjust = (D_QOG_HALF (DotProduct (p_temp1, p_saxis) * 0x10000)) -	// QuakeOG
			((pface->texturemins[0] << 16) >> miplevel)
			+ SURF_TEXINFO (currententity->model, pface)->vecs[0][3]*t;
	tadjust = (D_QOG_HALF (DotProduct (p_temp1, p_taxis) * 0x10000)) -	// QuakeOG
			((pface->texturemins[1] << 16) >> miplevel)
			+ SURF_TEXINFO (currententity->model, pface)->vecs[1][3]*t;

//
// -1 (-epsilon) so we never wander off the edge of the texture
//
	bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
	bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
}


/*
==============
D_QogDrawLitBody

QuakeOG, id's lit branch of D_DrawSurfaces from the mip level to
the z spans, unchanged, for the span list spans (s->spans, or the list the
two-core pipeline took from it, QOG_DRAW_CORE1). The caller has set the
surface's 1/z gradients and, for a brush entity, its transform. d_core1.c
compiles this file a second time with core 1's own copies of every value
written here (d_core1.h).
==============
*/
static void D_QogDrawLitBody (surf_t *s, espan_t *spans)
{
	msurface_t		*pface;
#ifdef QOG_SURFCACHE
	surfcache_t		*pcurrentcache;
#endif

		pface = SURF_DATA (s);	// QuakeOG, SURF_DATA, SURF_ENTITY
		miplevel = D_MipLevelForScale (s->nearzi * scale_for_mip
		* SURF_TEXINFO (SURF_ENTITY (s)->model, pface)->mipadjust);	// QuakeOG

		r_drawsurf.model = SURF_ENTITY (s)->model;	// QuakeOG, owner of pface's state arrays
#ifdef QOG_SURFCACHE
	// FIXME: make this passed in to D_CacheSurface
		pcurrentcache = D_CacheSurface (pface, miplevel);

		cacheblock = (pixel_t *)pcurrentcache->data;
		cachewidth = pcurrentcache->width;

		D_CalcGradients (pface);

		(*d_drawspans) (spans);	// QuakeOG
#else
	// QuakeOG, no surface cache, light each pixel as it is drawn (d_scanlit.c)
#if defined(QOG_RFAST) && defined(QOG_LITSPAN_FAST) && !defined(QOG_RFAST_FLOATSETUP)
		{
			extern float	d_qognearzi;	// QuakeOG, for the fixed point Fast drawer (d_scanlit.c)
			d_qognearzi = s->nearzi;
		}
#endif
		{
		QOG_PH_T (ph);	// QuakeOG
		D_SetupLitSurface (pface, miplevel);
		QOG_PH_ADD (QOG_PH_LIGHTMAP, ph);
		}
		{
		QOG_PH_T (ph);	// QuakeOG
		D_CalcGradients (pface);
		QOG_PH_ADD (QOG_PH_GRADIENTS, ph);
		}
		{
		QOG_PH_T (ph);	// QuakeOG
		D_DrawSpans8Lit (spans);	// QuakeOG
		QOG_PH_ADD (QOG_PH_SPANS, ph);
		}
#endif

#if !defined(QOG_LITSPAN_ZFUSE) || defined(QOG_SURFCACHE)	// QuakeOG, else D_DrawSpans8Lit wrote z
		QOG_ZSPANS (spans);	// QuakeOG
#endif
}

#ifdef QOG_DRAW_CORE1
/*
==============
D_QogDrawLit

QuakeOG, a lit world surface (insubmodel 0, not sky, background
or turbulent) with the span list spans, as D_DrawOneSurface draws it, for
the two-core pipeline. r_drawnpolycount is counted by the pipeline.
==============
*/
void D_QogDrawLit (surf_t *s, espan_t *spans)
{
	d_zistepu = s->d_zistepu;
	d_zistepv = s->d_zistepv;
	d_ziorigin = s->d_ziorigin;
	D_QogDrawLitBody (s, spans);
}
#endif

/*
==============
D_DrawOneSurface

QuakeOG, id's loop body of D_DrawSurfaces for one surface with
spans, in a function of its own so that QOG_SURF_SORT can draw the
surfaces in another order
==============
*/
static void D_DrawOneSurface (surf_t *s, vec3_t world_transformed_modelorg)
{
	msurface_t		*pface;
	vec3_t			local_modelorg;

	r_drawnpolycount++;
	QOG_BM (QOG_BM_SURFDRAWN, 1);	// QuakeOG
	QOG_BM (QOG_BM_BMSURF, s->insubmodel != 0);
	QOG_AQ (AQ_DSURF, 1);
#ifdef QOG_AQ_STATS
	{
		static entity_t	*aqlast;

		if (s->insubmodel)
		{
			QOG_AQ (AQ_DSUB, 1);
			QOG_AQ (AQ_DSUBSAME, SURF_ENTITY (s) == aqlast);
			aqlast = SURF_ENTITY (s);
		}
	}
#endif

	d_zistepu = s->d_zistepu;
	d_zistepv = s->d_zistepv;
	d_ziorigin = s->d_ziorigin;

	if (s->flags & SURF_DRAWSKY)
	{
		if (!r_skymade)
		{
			R_MakeSky ();
		}

		{
		QOG_PH_T (ph);	// QuakeOG
		D_DrawSkyScans8 (SPAN_PTR (s->spans));	// QuakeOG
		QOG_PH_ADD (QOG_PH_SKY, ph);
		QOG_PH_COUNT (QOG_PC_SKYPX, D_QogSpanPixels (SPAN_PTR (s->spans)));
		}
		QOG_ZSPANS (SPAN_PTR (s->spans));	// QuakeOG
	}
	else if (s->flags & SURF_DRAWBACKGROUND)
	{
	// set up a gradient for the background surface that places it
	// effectively at infinity distance from the viewpoint
		d_zistepu = 0;
		d_zistepv = 0;
		d_ziorigin = -0.9;

		D_DrawSolidSurface (s, (int)r_clearcolor.value & 0xFF);
		QOG_ZSPANS (SPAN_PTR (s->spans));	// QuakeOG
	}
	else if (s->flags & SURF_DRAWTURB)
	{
		pface = SURF_DATA (s);	// QuakeOG, SURF_DATA, SURF_ENTITY
		miplevel = 0;
		cacheblock = (pixel_t *)
				SURF_TEXINFO (SURF_ENTITY (s)->model, pface)->texture->mips[0];	// QuakeOG
		cachewidth = 64;

		if (s->insubmodel)
		{
#ifdef QOG_DSURF_NOFRUSTUM
			D_QogSubmodelBegin (s);	// QuakeOG
#else
		// FIXME: we don't want to do all this for every polygon!
		// TODO: store once at start of frame
			currententity = SURF_ENTITY (s);	//FIXME, make this passed in to
										// R_RotateBmodel ()
			VectorSubtract (r_origin, currententity->origin,
					local_modelorg);
			TransformVector (local_modelorg, transformed_modelorg);

			R_RotateBmodel ();	// FIXME, don't mess with the frustum,
								// make entity passed in
#endif
		}

		{
		QOG_PH_T (ph);	// QuakeOG
		D_CalcGradients (pface);
		Turbulent8 (SPAN_PTR (s->spans));	// QuakeOG
		QOG_PH_ADD (QOG_PH_TURB, ph);
		QOG_PH_COUNT (QOG_PC_TURBPX, D_QogSpanPixels (SPAN_PTR (s->spans)));
		}
		QOG_ZSPANS (SPAN_PTR (s->spans));	// QuakeOG

		if (s->insubmodel)
		{
		//
		// restore the old drawing state
		// FIXME: we don't want to do this every time!
		// TODO: speed up
		//
			currententity = &cl_entities[0];
			VectorCopy (world_transformed_modelorg,
						transformed_modelorg);
			VectorCopy (base_vpn, vpn);
			VectorCopy (base_vup, vup);
			VectorCopy (base_vright, vright);
			VectorCopy (base_modelorg, modelorg);
#ifndef QOG_DSURF_NOFRUSTUM	// QuakeOG, once at the end of D_DrawSurfaces
			R_TransformFrustum ();
#endif
		}
	}
	else
	{
		if (s->insubmodel)
		{
#ifdef QOG_DSURF_NOFRUSTUM
			D_QogSubmodelBegin (s);	// QuakeOG
#else
		// FIXME: we don't want to do all this for every polygon!
		// TODO: store once at start of frame
			currententity = SURF_ENTITY (s);	//FIXME, make this passed in to
										// R_RotateBmodel ()
			VectorSubtract (r_origin, currententity->origin, local_modelorg);
			TransformVector (local_modelorg, transformed_modelorg);

			R_RotateBmodel ();	// FIXME, don't mess with the frustum,
								// make entity passed in
#endif
		}

		D_QogDrawLitBody (s, SPAN_PTR (s->spans));	// QuakeOG, id's statements, below

		if (s->insubmodel)
		{
		//
		// restore the old drawing state
		// FIXME: we don't want to do this every time!
		// TODO: speed up
		//
			currententity = &cl_entities[0];
			VectorCopy (world_transformed_modelorg,
						transformed_modelorg);
			VectorCopy (base_vpn, vpn);
			VectorCopy (base_vup, vup);
			VectorCopy (base_vright, vright);
			VectorCopy (base_modelorg, modelorg);
#ifndef QOG_DSURF_NOFRUSTUM	// QuakeOG, once at the end of D_DrawSurfaces
			R_TransformFrustum ();
#endif
		}
	}
}

#ifdef QOG_SURF_SORT
/*
==============
D_QogSurfKey

QuakeOG, the drawing group of a surface with spans, 0 sky,
1 background, else its texture and mip level (a 12-bit hash of the base
texture's address, so two textures may share a group, which only costs
locality). Turbulent surfaces draw mip 0.
==============
*/
static int D_QogSurfKey (surf_t *s)
{
	msurface_t	*pface;
	mtexinfo_t	*ti;
	unsigned	h;
	int			mip;

	if (s->flags & SURF_DRAWSKY)
		return 0;
	if (s->flags & SURF_DRAWBACKGROUND)
		return 1;
	pface = SURF_DATA (s);
	ti = SURF_TEXINFO (SURF_ENTITY (s)->model, pface);
	h = (unsigned)(size_t)ti->texture;
	h = (h >> 4) ^ (h >> 16);
	mip = 0;
	if (!(s->flags & SURF_DRAWTURB))
		mip = D_MipLevelForScale (s->nearzi * scale_for_mip * ti->mipadjust);
	return 2 + (((h & 0xFFF) << 2) | mip);
}

/*
==============
D_QogDrawSorted

QuakeOG, the surfaces of this flush drawn grouped by texture
and mip level, as MG24Quake sorts them, so the texels of one texture are
read from flash once a flush and not again after other textures pushed
them out of the 16 KB XIP cache. Exact, every pixel and z of a flush
belongs to the spans of exactly one surface, so the surfaces can be drawn
in any order. The group key is kept in last_u, which only the span
generation of one scan line uses (r_edge.c sets it before every read),
so it is free between scan lines, where every flush happens. No RAM.
==============
*/
#define QOG_SORT_DONE	(-1)
static void D_QogDrawSorted (vec3_t world_transformed_modelorg)
{
	surf_t	*s, *first;
	int		key;

	for (s = &surfaces[1] ; s<surface_p ; s++)
		if (s->spans)
			s->last_u = D_QogSurfKey (s);
	for (first = &surfaces[1] ; ; first++)
	{
		while (first < surface_p && (!first->spans || first->last_u == QOG_SORT_DONE))
			first++;
		if (first >= surface_p)
			break;
		key = first->last_u;
		for (s = first ; s<surface_p ; s++)
		{
			if (s->last_u != key || !s->spans)
				continue;
			s->last_u = QOG_SORT_DONE;
			D_DrawOneSurface (s, world_transformed_modelorg);
		}
	}
}
#endif

#if defined(QOG_DRAW_CORE1) && !defined(QOG_C1_COPY)
/*
==============
QuakeOG, the two-core pipeline of the world's surfaces

R_ScanEdges (r_edge.c) runs on core 0 and, at the end of each scan line,
hands each lit world surface whose last scan line that was (its span list
is then final) to a ring. Core 1 takes surfaces from the ring and draws them
whole (light grid, gradients, spans and z) in its own copy of the drawing
code and state (d_core1.c), while core 0 goes on scanning. At every flush
(D_DrawSurfaces) core 0 draws the surfaces core 1 cannot (sky, background,
turbulent, brush entities) and then both cores take the rest of the ring
and the flush's remaining lit world surfaces until none is left, and core 0
waits for core 1 to finish its last one before the span buffer is reused.

Exact, every pixel and z of the view belongs to exactly one span, so the
surfaces can be drawn in any order and on either core. A surface handed on
early has the very span list (same spans, same order, same head) the flush
would give it, since no scan line after its last can add a span to it, and
its spans' z rows lie above the edge table in the z-buffer (r_edge.c checks
that). Each core draws a surface with id's statements from values nothing
changes meanwhile, the surface, the face, the frame's view values, and its
own copies of what the drawing writes.
==============
*/
#ifndef QOG_ESPAN_T_PACKED
#error "QOG_DRAW_CORE1 needs QOG_ESPAN_T_PACKED (a ring entry is a surface index and a span link)"
#endif
#ifndef QOG_SPANS_CORE1
#error "QOG_DRAW_CORE1 needs the core 1 job of QOG_SPANS_CORE1"
#endif
#define QOG_PIPE_RING	64		// a power of two. When full, the flush draws the surface

volatile int			d_qogpipe_on;		// R_ScanEdges runs the pipeline
volatile int			d_qogc1_drawing;	// core 1 draws a surface it took
static volatile unsigned	pipe_ring[QOG_PIPE_RING];	// surface index << 16 | span link
static volatile int		pipe_head, pipe_tail;
static surf_t * volatile	pipe_cursor;	// the flush's walk, NULL outside it
static volatile int		pipe_end;			// core 1's job may return when idle
static int				pipe_mode;			// Sys_QogCore1Mode, 0 core 1, 1 first, 2 last
static int				pipe_drawn;			// lit surfaces drawn through the pipeline (under the lock)
static int				pipe_early;			// of which handed on before their flush
#if defined(QOG_PHASE_TIME) && defined(_WIN32)
// PC measurement, the pixels of a span list
static int D_QogPipePx (espanref_t r)
{
	int	n = 0;

	for ( ; r != SPAN_NONE ; r = SPAN_PTR (r)->pnext)
		n += SPAN_PTR (r)->count;
	return n;
}
#define QOG_PIPE_PX(i, r)	(qog_c1_stat[i] += D_QogPipePx (r))
#else
#define QOG_PIPE_PX(i, r)
#endif

/*
==============
D_QogPipeBegin

QuakeOG, core 0 before the first scan line. Core 1's copies of
the view values its drawing reads and core 0's drawing may change (the
transform a brush entity's surface sets) are the world's, as D_DrawSurfaces
sets them.
==============
*/
void D_QogPipeBegin (int on)
{
	extern vec3_t		c1_vright, c1_vup, c1_vpn, c1_modelorg, c1_transformed_modelorg;
	extern entity_t		*c1_currententity;

	d_qogpipe_on = 0;
	if (!on || r_drawflat.value)
		return;
	D_QogLitCheck ();	// The earlier interpolator check, before core 1 reads r_qogfast
#ifdef QOG_DLIGHT_SPARSE
	// the world's dynamic light pass waits for the first question about a
	// lit surface (r_light.c R_DlightLit), the earlier R_QogDlightSettle runs
	// it here on core 0, after the brush entities and before any surface is
	// drawn, as the first drawn surface would, so neither core's drawing
	// writes the light hash
	R_QogDlightSettle ();
#endif
	VectorCopy (vright, c1_vright);
	VectorCopy (vup, c1_vup);
	VectorCopy (vpn, c1_vpn);
	VectorCopy (modelorg, c1_modelorg);
	TransformVector (modelorg, c1_transformed_modelorg);	// what D_DrawSurfaces computes
	c1_currententity = &cl_entities[0];
	pipe_head = pipe_tail = 0;
	pipe_cursor = NULL;
	pipe_end = 0;
	d_qogc1_drawing = 0;
	pipe_drawn = pipe_early = 0;
	pipe_mode = Sys_QogCore1Mode ();
	d_qogpipe_on = 1;
	if (pipe_mode == 0)
		Sys_QogCore1Start (D_QogC1Worker, NULL);
}

/*
==============
D_QogPipePush

QuakeOG, core 0, a lit world surface whose span list is final
==============
*/
void D_QogPipePush (surf_t *s)
{
	espanref_t	sp = s->spans;

	QOG_PIPE_PX (QOG_C1_EARLYPX, sp);
	if (pipe_mode == 1)
	{	// PC test, core 1's copy draws it at once, on this thread
		s->spans = SPAN_NONE;
		QOG_PIPE_PX (QOG_C1_PX, sp);
		pipe_drawn++;
		pipe_early++;
		D_QogC1Draw (s, SPAN_PTR (sp));
		return;
	}
	if (pipe_tail - pipe_head >= QOG_PIPE_RING)
		return;		// the flush draws it
	s->spans = SPAN_NONE;
	pipe_ring[pipe_tail & (QOG_PIPE_RING - 1)] = ((unsigned)(s - surfaces) << 16) | sp;
	pipe_tail = pipe_tail + 1;	// after the entry (volatile, in order)
	pipe_early++;
	if (pipe_mode == 0)
		Sys_QogCore1Wake ();
}

/*
==============
D_QogPipePop

QuakeOG, either core, the next surface to draw, the ring first,
then, inside a flush, the flush's lit world surfaces. Takes the span list
off the surface, so each surface is drawn once. core1 marks core 1 busy
until D_QogPipeDone.
==============
*/
int D_QogPipePop (int core1, surf_t **ps, espan_t **pspans)
{
	surf_t		*s = NULL, *c;
	espanref_t	sp = SPAN_NONE;
	unsigned	e;

	Sys_QogLock ();
	if (pipe_head != pipe_tail)
	{
		e = pipe_ring[pipe_head & (QOG_PIPE_RING - 1)];
		pipe_head = pipe_head + 1;
		s = surfaces + (e >> 16);
		sp = (espanref_t)(e & 0xFFFF);
	}
	else if ((c = pipe_cursor) != NULL)
	{
		for ( ; c < surface_p ; c++)
		{
			if (D_QOG_C1OK (c) && c->spans)
			{
				s = c++;
				sp = s->spans;
				s->spans = SPAN_NONE;
				break;
			}
		}
		pipe_cursor = c;
	}
	if (s)
	{
		QOG_PIPE_PX (QOG_C1_PX, sp);
		pipe_drawn++;
		if (core1)
			d_qogc1_drawing = 1;
	}
	Sys_QogUnlock ();
	if (!s)
		return 0;
	*ps = s;
	*pspans = SPAN_PTR (sp);
	return 1;
}

// QuakeOG, core 1 has drawn the surface it took (its stores
// before the flag, through the lock's barriers)
void D_QogPipeDone (void)
{
	Sys_QogLock ();
	d_qogc1_drawing = 0;
	Sys_QogUnlock ();
}

int D_QogPipeEnded (void)
{
	return pipe_end;
}

/*
==============
D_QogPipeSync

QuakeOG, core 0, every surface handed on is drawn and core 1 is
idle. Before the span buffer is reused or the z-buffer pointer changes.
==============
*/
void D_QogPipeSync (void)
{
	surf_t	*s;
	espan_t	*sp;

	if (!d_qogpipe_on)
		return;
	if (pipe_mode == 0)
	{
		while (D_QogPipePop (0, &s, &sp))
			D_QogDrawLit (s, sp);
		if (d_qogc1_drawing)
		{
#ifdef QOG_PHASE_TIME
			unsigned	t0 = qog_phase_clock ();
#endif
			while (d_qogc1_drawing)
				;
#ifdef QOG_PHASE_TIME
			qog_c1_stat[QOG_C1_C0WAIT] += qog_phase_clock () - t0;
#endif
		}
	}
	else
	{
		while (D_QogPipePop (1, &s, &sp))
		{
			D_QogC1Draw (s, sp);
			d_qogc1_drawing = 0;
		}
	}
}

/*
==============
D_QogPipeEnd

QuakeOG, core 0 after the last flush
==============
*/
void D_QogPipeEnd (void)
{
	if (!d_qogpipe_on)
		return;
	D_QogPipeSync ();
	pipe_end = 1;
	if (pipe_mode == 0)
	{
		Sys_QogCore1Wake ();
		Sys_QogCore1Join ();
	}
	r_drawnpolycount += pipe_drawn;
#ifdef QOG_PHASE_TIME
	qog_c1_stat[QOG_C1_SURFS] += pipe_drawn;
	qog_c1_stat[QOG_C1_EARLY] += pipe_early;
#endif
	d_qogpipe_on = 0;
}

/*
==============
D_QogPipeFlush

QuakeOG, D_DrawSurfaces while the pipeline runs. Core 0 draws
what only it can, both cores share the lit world surfaces.
==============
*/
static void D_QogPipeFlush (vec3_t world_transformed_modelorg)
{
	extern vec3_t	c1_vright, c1_vup, c1_vpn, c1_modelorg, c1_transformed_modelorg;
	surf_t			*s;

#ifdef _WIN32
	// PC check, core 1's copies are the values this flush draws with
	if (memcmp (c1_vright, vright, sizeof(vec3_t)) || memcmp (c1_vup, vup, sizeof(vec3_t))
		|| memcmp (c1_vpn, vpn, sizeof(vec3_t)) || memcmp (c1_modelorg, modelorg, sizeof(vec3_t))
		|| memcmp (c1_transformed_modelorg, world_transformed_modelorg, sizeof(vec3_t)))
		Sys_Error ("D_DrawSurfaces: the view moved under the core 1 pipeline");
#endif
	if (pipe_mode == 1)
	{	// PC test, core 1's share first
		pipe_cursor = &surfaces[1];
		D_QogPipeSync ();
	}
	pipe_cursor = &surfaces[1];
	if (pipe_mode == 0)
		Sys_QogCore1Wake ();
	for (s = &surfaces[1] ; s<surface_p ; s++)
	{
		if (D_QOG_C1OK (s) || !s->spans)
			continue;
		D_DrawOneSurface (s, world_transformed_modelorg);
	}
	D_QogPipeSync ();	// pipe_mode 2, core 1's share last
	pipe_cursor = NULL;
}
#endif

/*
==============
D_DrawSurfaces
==============
*/
void D_DrawSurfaces (void)
{
	surf_t			*s;
	vec3_t			world_transformed_modelorg;

	currententity = &cl_entities[0];
	TransformVector (modelorg, transformed_modelorg);
	VectorCopy (transformed_modelorg, world_transformed_modelorg);
#ifdef QOG_DSURF_NOFRUSTUM
	d_qog_frustumdirty = false;	// QuakeOG
	d_qog_xfent = NULL;
#endif

// TODO: could preset a lot of this at mode set time
	if (r_drawflat.value)
	{
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
			if (!s->spans)
				continue;

			d_zistepu = s->d_zistepu;
			d_zistepv = s->d_zistepv;
			d_ziorigin = s->d_ziorigin;

			D_DrawSolidSurface (s, (int)SURF_DATA (s) & 0xFF);	// QuakeOG, SURF_DATA
			D_DrawZSpans (SPAN_PTR (s->spans));	// QuakeOG
		}
	}
#if defined(QOG_DRAW_CORE1) && !defined(QOG_C1_COPY)
	else if (d_qogpipe_on)
		D_QogPipeFlush (world_transformed_modelorg);	// QuakeOG
#endif
	else
	{
#ifdef QOG_SURF_SORT
		D_QogDrawSorted (world_transformed_modelorg);
#else
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
			if (!s->spans)
				continue;

			D_DrawOneSurface (s, world_transformed_modelorg);	// QuakeOG
		}
#endif
	}
#ifdef QOG_DSURF_NOFRUSTUM
	// QuakeOG, id's last restore, from the world's vectors and
	// base_modelorg, which every brush surface above put back
	if (d_qog_frustumdirty)
		R_TransformFrustum ();
#endif
}

