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
#include "qog_phase.h"	// QuakeOG: render phase times (QOG_PHASE_TIME)

#ifdef QOG_PHASE_TIME
// QuakeOG: pixels in a span list, for the phase counts
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
	// QuakeOG: SPAN_PTR, see espan_t in r_shared.h
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

	pplane = SURF_PLANE (currententity->model, pface);	// QuakeOG

	mipscale = 1.0 / (float)(1 << miplevel);

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
	sadjust = ((fixed16_t)(DotProduct (p_temp1, p_saxis) * 0x10000 + 0.5)) -
			((pface->texturemins[0] << 16) >> miplevel)
			+ SURF_TEXINFO (currententity->model, pface)->vecs[0][3]*t;
	tadjust = ((fixed16_t)(DotProduct (p_temp1, p_taxis) * 0x10000 + 0.5)) -
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
D_DrawOneSurface

QuakeOG (Agent AH): id's loop body of D_DrawSurfaces for one surface with
spans, in a function of its own so that QOG_SURF_SORT can draw the
surfaces in another order
==============
*/
static void D_DrawOneSurface (surf_t *s, vec3_t world_transformed_modelorg)
{
	msurface_t		*pface;
#ifdef QOG_SURFCACHE
	surfcache_t		*pcurrentcache;
#endif
	vec3_t			local_modelorg;

	r_drawnpolycount++;

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
		pface = SURF_DATA (s);	// QuakeOG: SURF_DATA, SURF_ENTITY
		miplevel = 0;
		cacheblock = (pixel_t *)
				SURF_TEXINFO (SURF_ENTITY (s)->model, pface)->texture->mips[0];	// QuakeOG
		cachewidth = 64;

		if (s->insubmodel)
		{
		// FIXME: we don't want to do all this for every polygon!
		// TODO: store once at start of frame
			currententity = SURF_ENTITY (s);	//FIXME: make this passed in to
										// R_RotateBmodel ()
			VectorSubtract (r_origin, currententity->origin,
					local_modelorg);
			TransformVector (local_modelorg, transformed_modelorg);

			R_RotateBmodel ();	// FIXME: don't mess with the frustum,
								// make entity passed in
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
			R_TransformFrustum ();
		}
	}
	else
	{
		if (s->insubmodel)
		{
		// FIXME: we don't want to do all this for every polygon!
		// TODO: store once at start of frame
			currententity = SURF_ENTITY (s);	//FIXME: make this passed in to
										// R_RotateBmodel ()
			VectorSubtract (r_origin, currententity->origin, local_modelorg);
			TransformVector (local_modelorg, transformed_modelorg);

			R_RotateBmodel ();	// FIXME: don't mess with the frustum,
								// make entity passed in
		}

		pface = SURF_DATA (s);	// QuakeOG: SURF_DATA, SURF_ENTITY
		miplevel = D_MipLevelForScale (s->nearzi * scale_for_mip
		* SURF_TEXINFO (SURF_ENTITY (s)->model, pface)->mipadjust);	// QuakeOG

		r_drawsurf.model = SURF_ENTITY (s)->model;	// QuakeOG: owner of pface's state arrays
#ifdef QOG_SURFCACHE
	// FIXME: make this passed in to D_CacheSurface
		pcurrentcache = D_CacheSurface (pface, miplevel);

		cacheblock = (pixel_t *)pcurrentcache->data;
		cachewidth = pcurrentcache->width;

		D_CalcGradients (pface);

		(*d_drawspans) (SPAN_PTR (s->spans));	// QuakeOG
#else
	// QuakeOG: no surface cache, light each pixel as it is drawn (d_scanlit.c)
#if defined(QOG_RFAST) && defined(QOG_LITSPAN_FAST) && !defined(QOG_RFAST_FLOATSETUP)
		{
			extern float	d_qognearzi;	// QuakeOG (Agent AH): for the fixed point Fast drawer (d_scanlit.c)
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
		D_DrawSpans8Lit (SPAN_PTR (s->spans));	// QuakeOG
		QOG_PH_ADD (QOG_PH_SPANS, ph);
		}
#endif

#if !defined(QOG_LITSPAN_ZFUSE) || defined(QOG_SURFCACHE)	// QuakeOG: else D_DrawSpans8Lit wrote z
		QOG_ZSPANS (SPAN_PTR (s->spans));	// QuakeOG
#endif

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
			R_TransformFrustum ();
		}
	}
}

#ifdef QOG_SURF_SORT
/*
==============
D_QogSurfKey

QuakeOG (Agent AH): the drawing group of a surface with spans: 0 sky,
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

QuakeOG (Agent AH): the surfaces of this flush drawn grouped by texture
and mip level, as MG24Quake sorts them, so the texels of one texture are
read from flash once a flush and not again after other textures pushed
them out of the 16 KB XIP cache. Exact: every pixel and z of a flush
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

			D_DrawSolidSurface (s, (int)SURF_DATA (s) & 0xFF);	// QuakeOG: SURF_DATA
			D_DrawZSpans (SPAN_PTR (s->spans));	// QuakeOG
		}
	}
	else
	{
#ifdef QOG_SURF_SORT
		D_QogDrawSorted (world_transformed_modelorg);
#else
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
			if (!s->spans)
				continue;

			D_DrawOneSurface (s, world_transformed_modelorg);	// QuakeOG (Agent AH)
		}
#endif
	}
}

