/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2026 QuakeOG contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
// d_scanlit.c -- QuakeOG: world surfaces drawn straight from texture and
// lightmap, with no surface cache.
//
// id's renderer first builds a lit copy of every visible surface into the
// surface cache (600 KB at 320x200), then draws spans from it. Here each
// pixel fetches its texel from the texture (read in place from flash),
// blends the four surrounding lightmap samples, and maps through the
// colormap. The only per-surface RAM is the 18x18 light grid that
// R_BuildLightMap already fills (dynamic lights included).
//
// D_DrawSpans8Lit is id's D_DrawSpans8 with only the inner pixel fetch
// changed, so perspective correction and clamping behave exactly as before.

#include "quakedef.h"
#include "d_local.h"
#include "r_local.h"
#include "qog_phase.h"	// QuakeOG: render phase counts (QOG_PHASE_TIME)

#ifndef QOG_SURFCACHE

extern unsigned	blocklights[18*18];
void R_BuildLightMap (void);

static byte		*lit_tex;			// mip level pixels
static int		lit_tw, lit_th;		// mip level size
static int		lit_soff, lit_toff;	// texture origin of the surface, wrapped into the texture
static int		lit_smax;			// lightmap row length
static int		lit_lshift;			// surface pixels at this mip -> lightmap cells
static byte		*lit_colormap;
#ifdef QOG_LITSPAN_FAST
// QuakeOG: state of the restructured span drawer (d_litspan.h)
static int		lit_kind;			// 0 to 3 power-of-two texture at that mip, 4 any size
static int		lit_soff16, lit_toff16;	// texture origin in 16.16
static int		lit_smask, lit_tshift;	// power of two: s mask, log2 of the width
static unsigned	lit_tmask2;			// power of two: t mask shifted up by lit_tshift
static int		lit_tw16, lit_th16;	// any size: texture size in 16.16
static const unsigned	*lit_lgrid;		// blocklights
static const unsigned	*lit_lbase;		// blocklights moved back by the origin's light cells
#define LITSPAN_NEXT(p)	((p)->pnext != SPAN_NONE && ((p) = SPAN_PTR ((p)->pnext)))
#ifdef QOG_LITSPAN_ZFUSE
// QuakeOG: each span's z is written here from the 1/z the drawer computes
// at its start (D_QogZSpan, d_scan.c), and D_DrawSurfaces skips D_DrawZSpans
void D_QogZSpan (espan_t *pspan, float zi, int izistep);
#define LITSPAN_ZDECL			int izistep_ = (int)(d_zistepu * 0x8000 * 0x10000);
#define LITSPAN_ZSPAN(p, zi)	D_QogZSpan (p, zi, izistep_);
#endif
#include "d_litspan.h"
#endif

/*
================
D_SetupLitSurface

Per surface, per frame: the light grid and the texture to sample.
================
*/
void D_SetupLitSurface (msurface_t *pface, int mip)
{
	texture_t	*tex;
	int			i;

	tex = R_TextureAnimation (SURF_TEXINFO (r_drawsurf.model, pface)->texture);	// QuakeOG
	r_drawsurf.surf = pface;
	r_drawsurf.texture = tex;
	r_drawsurf.surfmip = mip;
	for (i=0 ; i<MAXLIGHTMAPS ; i++)
		r_drawsurf.lightadj[i] = d_lightstylevalue[pface->styles[i]];
	R_BuildLightMap ();

	lit_tex = tex->mips[mip];
	lit_tw = tex->width >> mip;
	lit_th = tex->height >> mip;
	// texturemins are multiples of 16, so the division is exact at any mip
	lit_soff = (pface->texturemins[0] / (1 << mip)) % lit_tw;
	if (lit_soff < 0)
		lit_soff += lit_tw;
	lit_toff = (pface->texturemins[1] / (1 << mip)) % lit_th;
	if (lit_toff < 0)
		lit_toff += lit_th;
	lit_smax = (pface->extents[0] >> 4) + 1;
	lit_lshift = 4 - mip;	// a lightmap cell is 16 texels at mip 0
	lit_colormap = (byte *)vid.colormap;
	QOG_PH_COUNT (QOG_PC_LITSURFS, 1);
	QOG_PH_COUNT (QOG_PC_LMCELLS, lit_smax * ((pface->extents[1] >> 4) + 1));
#ifdef QOG_LITSPAN_FAST
	// QuakeOG: the restructured drawer's per-surface values (d_litspan.h)
	LITSPAN_SETUP (blocklights)
#endif
}

#ifdef QOG_LITSPAN_FAST
/*
=============
D_DrawSpans8Lit

QuakeOG: the same pixels as id's D_DrawSpans8 with LitPixel below, with
no modulo and fewer instructions per pixel (d_litspan.h).
=============
*/

#ifdef QOG_PHASE_TIME
static void D_QogCountLit (espan_t *pspan, int any)
{
	do
	{
		QOG_PH_COUNT (QOG_PC_LITSPANS, 1);
		QOG_PH_COUNT (QOG_PC_LITPX, pspan->count);
		QOG_PH_COUNT (QOG_PC_RUNS, (pspan->count + 7) >> 3);
		if (any)
			QOG_PH_COUNT (QOG_PC_LITANYPX, pspan->count);
#ifdef QOG_LITSPAN_ZFUSE
		QOG_PH_COUNT (QOG_PC_ZPX, pspan->count);	// z written here too
#endif
	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR (pspan->pnext)));
}
#endif

void D_DrawSpans8Lit (espan_t *pspan)
{
#ifdef QOG_PHASE_TIME
	D_QogCountLit (pspan, lit_kind == 4);
#endif
	{
#if defined(QOG_LITSPAN16) && defined(QOG_LITSPAN_LINLIGHT)
	D_LITSPANS16LIN_BODY	// QuakeOG: approximate, both below
#elif defined(QOG_LITSPAN16)
	D_LITSPANS16_BODY		// QuakeOG: approximate, one float divide per 16 pixels
#elif defined(QOG_LITSPAN_LINLIGHT)
	D_LITSPANS8LIN_BODY		// QuakeOG: approximate, light linear along each run
#else
	D_LITSPANS8_BODY		// QuakeOG: exact
#endif
	}
}

#else	// !QOG_LITSPAN_FAST

/*
================
LitPixel

s and t are 16.16 surface coordinates at the current mip, already clamped
to the surface by the caller, so the lightmap reads stay inside the grid.
================
*/
static byte LitPixel (fixed16_t s, fixed16_t t)
{
	int			tx, ty, ls, lt, fx, fy, top, bot, light;
	unsigned	*l;

	tx = ((s >> 16) + lit_soff) % lit_tw;
	ty = ((t >> 16) + lit_toff) % lit_th;

	ls = s >> lit_lshift;
	lt = t >> lit_lshift;
	fx = (ls >> 8) & 0xFF;
	fy = (lt >> 8) & 0xFF;
	l = blocklights + (lt >> 16) * lit_smax + (ls >> 16);

	top = (int)l[0] + ((((int)l[1] - (int)l[0]) * fx) >> 8);
	bot = (int)l[lit_smax] + ((((int)l[lit_smax+1] - (int)l[lit_smax]) * fx) >> 8);
	light = top + (((bot - top) * fy) >> 8);

	return lit_colormap[(light & 0xFF00) + lit_tex[ty * lit_tw + tx]];
}

/*
=============
D_DrawSpans8Lit

id's D_DrawSpans8, sampling texture and light directly.
=============
*/
void D_DrawSpans8Lit (espan_t *pspan)
{
	int				count, spancount;
	unsigned char	*pdest;
	fixed16_t		s, t, snext, tnext, sstep, tstep;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz8stepu, tdivz8stepu, zi8stepu;

	sstep = 0;	// keep compiler happy
	tstep = 0;	// ditto

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;

	do
	{
		pdest = (unsigned char *)((byte *)d_viewbuffer +
				(screenwidth * pspan->v) + pspan->u);

		count = pspan->count;

	// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

		s = (int)(sdivz * z) + sadjust;
		if (s > bbextents)
			s = bbextents;
		else if (s < 0)
			s = 0;

		t = (int)(tdivz * z) + tadjust;
		if (t > bbextentt)
			t = bbextentt;
		else if (t < 0)
			t = 0;

		do
		{
		// calculate s and t at the far end of the span
			if (count >= 8)
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if (count)
			{
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 8)
					snext = 8;

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 8)
					tnext = 8;

				sstep = (snext - s) >> 3;
				tstep = (tnext - t) >> 3;
			}
			else
			{
				spancountminus1 = (float)(spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 8)
					snext = 8;

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 8)
					tnext = 8;

				if (spancount > 1)
				{
					sstep = (snext - s) / (spancount - 1);
					tstep = (tnext - t) / (spancount - 1);
				}
			}

			do
			{
				*pdest++ = LitPixel (s, t);
				s += sstep;
				t += tstep;
			} while (--spancount > 0);

			s = snext;
			t = tnext;

		} while (count > 0);

	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR(pspan->pnext)));	// QuakeOG
}

#endif	// !QOG_LITSPAN_FAST

#endif	// !QOG_SURFCACHE
