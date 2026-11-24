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

#ifdef QOG_RFAST_STATS
// QuakeOG (Agent AA): PC measurement of the Fast drawer's work (d_litspan.h).
// qog_rfaststats prints the sums since the last call and clears them.
unsigned	litfast_stats[6];
static int	litfast_frame0;

void D_QogRfastStats_f (void)
{
	int		views = r_framecount - litfast_frame0;

	Con_Printf ("qog_rfaststats: views %d sub8 %u sub16 %u sub32 %u px %u spans %u exactruns %u\n",
			views, litfast_stats[0], litfast_stats[1], litfast_stats[2],
			litfast_stats[3], litfast_stats[4], litfast_stats[5]);
	memset (litfast_stats, 0, sizeof(litfast_stats));
	litfast_frame0 = r_framecount;
}
#endif

#if defined(QOG_RFAST) && defined(QOG_LITSPAN_FAST)
// QuakeOG (Agent AA): the Fast mode drawer (r_fast 1) is a function of its
// own, so the exact drawer's code and stack stay as they were
#if defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE	__attribute__((noinline))
#endif
#define QOG_LITFAST_FN													\
static QOG_NOINLINE void D_DrawSpans8LitFast (espan_t *pspan, int step)	\
{																		\
	(void)step;															\
	D_LITSPANS_FAST_BODY												\
}
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
#if !defined(_WIN32) && (defined(QOG_LIT_TEX_NOALLOC) || defined(QOG_LIT_CMAP_NOALLOC))
	// QuakeOG (Agent Z): measurement options for the board (exact, the same
	// bytes). Texels, or the colour map, read from flash through the
	// RP2040's XIP no-allocate alias (0x11000000): a read the XIP cache
	// holds is served from it, a miss is read from flash without evicting
	// anything, so streaming texels leave the span code, the colour map and
	// the light grid code in the 16 KB cache.
#define QOG_XIP_NOALLOC(p)	((unsigned)(size_t)(p) - 0x10000000u < 0x01000000u ? \
							 (byte *)(p) + 0x01000000 : (byte *)(p))
#ifdef QOG_LIT_TEX_NOALLOC
	lit_tex = QOG_XIP_NOALLOC (lit_tex);
#endif
#ifdef QOG_LIT_CMAP_NOALLOC
	lit_colormap = QOG_XIP_NOALLOC (lit_colormap);
#endif
#endif
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

#ifdef QOG_SPANS_CORE1
/*
=============
QuakeOG (Agent V): the display chip's second core draws half of each lit
surface's spans (QOG_SPANS_CORE1, board client only, exact). Core 0 sets
the surface up (D_SetupLitSurface, D_CalcGradients) as before, then core 1
draws the 2nd, 4th, 6th ... span of the list while core 0 draws the 1st,
3rd, 5th ..., and core 0 waits for core 1 before the next surface. A span
writes only its own pixels and z, and the drawer only reads the surface's
state (the lit_* values, the gradients, the light grid), which nothing
changes until both cores are done, so the picture is the same in any
order. The port runs the other half: core 1 on the board (vid_rp2040.c),
a second thread or the same thread before or after on the PC (vid_pc.c).
=============
*/
static int D_QogSpanStep (espan_t **pp, int step)
{
	espan_t	*p = *pp;

	do
	{
		if (p->pnext == SPAN_NONE)
			return 0;
		p = SPAN_PTR (p->pnext);
	} while (--step > 0);
	*pp = p;
	return 1;
}

#undef LITSPAN_NEXT
#define LITSPAN_NEXT(p)	D_QogSpanStep (&(p), step)

#ifdef QOG_RFAST
// QuakeOG (Agent AA): Fast mode, every step-th span from pspan. Both cores
// pick the drawer before they call it, so the two frames never stack.
QOG_LITFAST_FN
#define D_LITSTEP(p, step)												\
	(r_qogfast ? D_DrawSpans8LitFast (p, step) : D_DrawSpans8LitStep (p, step))
#else
#define D_LITSTEP(p, step)	D_DrawSpans8LitStep (p, step)
#endif

// the drawer below, on every step-th span from pspan
static void D_DrawSpans8LitStep (espan_t *pspan, int step)
{
#if defined(QOG_LITSPAN16) && defined(QOG_LITSPAN_LINLIGHT)
	D_LITSPANS16LIN_BODY
#elif defined(QOG_LITSPAN16)
	D_LITSPANS16_BODY
#elif defined(QOG_LITSPAN_LINLIGHT)
	D_LITSPANS8LIN_BODY
#else
	D_LITSPANS8_BODY
#endif
}

static void D_QogSpansCore1 (void *pspan)
{
	D_LITSTEP ((espan_t *)pspan, 2);	// QuakeOG (Agent AA): or the Fast drawer
}

void D_DrawSpans8Lit (espan_t *pspan)
{
#ifdef QOG_PHASE_TIME
	D_QogCountLit (pspan, lit_kind == 4);
#endif
	if (pspan->pnext == SPAN_NONE)
	{
		D_LITSTEP (pspan, 1);	// QuakeOG (Agent AA): or the Fast drawer
		return;
	}
	Sys_QogCore1Start (D_QogSpansCore1, SPAN_PTR (pspan->pnext));
	D_LITSTEP (pspan, 2);	// QuakeOG (Agent AA): or the Fast drawer
	Sys_QogCore1Join ();
}

#else	// !QOG_SPANS_CORE1

#ifdef QOG_RFAST
// QuakeOG (Agent AA): Fast mode, a drawer of its own, and the exact drawer
// below in a function of its own, so the two frames never stack
QOG_LITFAST_FN
static QOG_NOINLINE void D_DrawSpans8LitExact (espan_t *pspan);

void D_DrawSpans8Lit (espan_t *pspan)
{
#ifdef QOG_PHASE_TIME
	D_QogCountLit (pspan, lit_kind == 4);
#endif
	if (r_qogfast)
		D_DrawSpans8LitFast (pspan, 1);
	else
		D_DrawSpans8LitExact (pspan);
}

static QOG_NOINLINE void D_DrawSpans8LitExact (espan_t *pspan)
#else
void D_DrawSpans8Lit (espan_t *pspan)
#endif
{
#if defined(QOG_PHASE_TIME) && !defined(QOG_RFAST)
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
#endif	// QOG_SPANS_CORE1

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
