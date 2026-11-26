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
// d_scanlit.c -- QuakeOG, world surfaces drawn straight from texture and
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

#ifndef QOG_DC_NAMES	// QuakeOG, d_core1.h includes them before its names
#include "quakedef.h"
#include "d_local.h"
#include "r_local.h"
#include "qog_phase.h"	// QuakeOG, render phase counts (QOG_PHASE_TIME)
#endif
#include "d_drawctx.h"
#ifndef QOG_PH_ON
#define QOG_PH_ON	1	// QuakeOG, d_core1.h gives the shared copy its own
#endif

#ifndef QOG_SURFCACHE

#ifdef QOG_C1_COPY
#undef QOG_AR_STATS		// QuakeOG, PC measurement, core 0's copy only
#undef QOG_AR_DUMP
#endif
// QuakeOG, the Q drawer (d_lit_q.h) is the Fast drawer's, and
// its runs use the interpolators
#if defined(QOG_LITSPAN_Q) && !(defined(QOG_LITSPAN_INTERP) && defined(QOG_RFAST) && \
	defined(QOG_LITSPAN_FAST) && !defined(QOG_RFAST_FLOATSETUP))
#undef QOG_LITSPAN_Q
#endif

#ifndef QOG_DC_NAMES
extern unsigned	blocklights[18*18];
#endif
void R_BuildLightMap (void);

#ifndef QOG_DC_NAMES	// QuakeOG, else in the core's context (d_drawctx.h)
static byte		*lit_tex;			// mip level pixels
static int		lit_tw, lit_th;		// mip level size
static int		lit_soff, lit_toff;	// texture origin of the surface, wrapped into the texture
static int		lit_smax;			// lightmap row length
static int		lit_lshift;			// surface pixels at this mip -> lightmap cells
static byte		*lit_colormap;
#endif
#ifdef QOG_LITSPAN_FAST
// QuakeOG, state of the restructured span drawer (d_litspan.h)
#ifndef QOG_DC_NAMES
static int		lit_kind;			// 0 to 3 power-of-two texture at that mip, 4 any size
static int		lit_soff16, lit_toff16;	// texture origin in 16.16
static int		lit_smask, lit_tshift;	// power of two, s mask, log2 of the width
static unsigned	lit_tmask2;			// power of two, t mask shifted up by lit_tshift
static int		lit_tw16, lit_th16;	// any size, texture size in 16.16
static const unsigned	*lit_lgrid;		// blocklights
static const unsigned	*lit_lbase;		// blocklights moved back by the origin's light cells
#endif
#ifdef QOG_LITSPAN_INTERP
// QuakeOG, interpolator 0's lane controls for this surface's
// texture (d_litspan.h), set by D_SetupLitSurface, written by each core
#ifndef QOG_DC_NAMES
static unsigned	lit_ictrl0, lit_ictrl1;
#endif
#if defined(_WIN32) && !defined(QOG_DC_NAMES)
__declspec(thread) unsigned	liti_regs[2][16];	// the PC's stand-in for each core's SIO interpolators
#endif
#define LITI_PROLOGUE	if (lit_kind <= 3) LITI_SURFACE (lit_ictrl0, lit_ictrl1)
#else
#define LITI_PROLOGUE
#endif
#define LITSPAN_NEXT(p)	((p)->pnext != SPAN_NONE && ((p) = SPAN_PTR ((p)->pnext)))
#ifdef QOG_LITSPAN_ZFUSE
// QuakeOG, each span's z is written here from the 1/z the drawer computes
// at its start (D_QogZSpan, d_scan.c), and D_DrawSurfaces skips D_DrawZSpans
void D_QogZSpan (espan_t *pspan, float zi, int izistep);
#define LITSPAN_ZDECL			int izistep_ = (int)(d_zistepu * 0x8000 * 0x10000);
#define LITSPAN_ZSPAN(p, zi)	D_QogZSpan (p, zi, izistep_);
#if defined(QOG_RFAST) && !defined(QOG_RFAST_FLOATSETUP)
// QuakeOG, the fixed point Fast drawer's z, from its 1/z * 2^k
void D_QogZSpanI (espan_t *pspan, int izi, int izistep);
#define LITFIX_ZSPAN(p, D64)	D_QogZSpanI (p, ((D64) >> litfix_zsh) > 0x7FFFFFFF ? 0x7FFFFFFF \
									: (int)((D64) >> litfix_zsh), izistep_);
#endif
#endif
#if defined(QOG_RFAST) && !defined(QOG_RFAST_FLOATSETUP)
// QuakeOG, one surface's values for the fixed point Fast drawer
// (QOG_RFAST_FIX, d_litspan.h), set on core 0 by D_LitFixSetup and read by
// both cores
#ifndef QOG_DC_NAMES
static int			litfix_ok;				// the surface can be drawn by D_DrawSpans8LitFix
static int			litfix_uref, litfix_vref;	// the reference point, the first span's start
static long long	litfix_d0, litfix_du, litfix_dv;	// 1/z * 2^k at it and its steps
static long long	litfix_s0, litfix_su, litfix_sv;	// (s / 65536) * (1/z) * 2^(k - ws)
static long long	litfix_t0, litfix_tu, litfix_tv;	// (t / 65536) * (1/z) * 2^(k - wt)
static int			litfix_sw, litfix_tw;	// 16 - ws and 16 - wt
static int			litfix_stepbits;		// bits of the largest u step, less 25
static int			litfix_zsh;				// k - 31, 1/z * 2^31 is D >> litfix_zsh
float				d_qognearzi;			// the surface's nearest 1/z (d_edge.c)
#endif
#endif
#include "d_litspan.h"
#endif

#if defined(QOG_LITSPAN_INTERP) && defined(QOG_RFAST) && !defined(_WIN32)
/*
================
D_LitInterpCheck

QuakeOG, the board checks once that its interpolators give what
the pixel loop's C expressions give (d_litspan.h, QOG_LITSPAN_INTERP), for
a 64x64 texture and a light run, at start-up from D_Init, where printing
is safe (inside the edge drawing a Con_Printf frame of 4 KB would not be).
If they differ, every lit surface takes id's exact drawer (r_qogfast 0),
which does not use them, and a console line says why.
================
*/
static int	liti_state;		// 0 not checked, 1 good, 2 bad

void D_QogLitInterpCheck (void)
{
	unsigned	S = 0x00C3A5F1u, T2 = 0x2F0BC055u, ss = 0x0001B3C7u, ts = 0xFFF2A1D9u;
	unsigned	lacc = 0x12345678u, lstep = 0x0006F00Du;
	unsigned	tex = 0x20010000u, cm = 0x10123400u;
	int			i, bad = 0;

	LITI_SET (0, LITI_CTRL0, LITI_CTRL (16, 0, 5));
	LITI_SET (0, LITI_CTRL1, LITI_CTRL (16, 6, 11));
	LITI_SET (0, LITI_BASE2, tex);
	LITI_SET (1, LITI_CTRL0, LITI_CTRL (16, 8, 13));
	LITI_SET (1, LITI_CTRL1, 0);
	LITI_SET (1, LITI_ACC1, 0);
	LITI_SET (1, LITI_BASE1, 0);
	LITI_SET (1, LITI_BASE2, cm);
	LITI_SET (0, LITI_ACC0, S);
	LITI_SET (0, LITI_BASE0, ss);
	LITI_SET (0, LITI_ACC1, T2);
	LITI_SET (0, LITI_BASE1, ts);
	LITI_SET (1, LITI_ACC0, lacc);
	LITI_SET (1, LITI_BASE0, lstep);
	for (i = 0 ; i < 64 ; i++)
	{
		unsigned	a = LITI_POPFULL (0), b = LITI_POPFULL (1);

		if (a != tex + ((T2 >> 16) & (63u << 6)) + ((S >> 16) & 63u) ||
			b != cm + (lacc >> 24 << 8))
			bad++;
		S += ss;
		T2 += ts;
		lacc += lstep;
	}
	liti_state = bad ? 2 : 1;
	if (bad)
		Con_Printf ("Fast render off: the interpolator check failed %d of 64\n", bad);
#if defined(QOG_LITSPAN_Q) && defined(QOG_LITQ_ASM)
	if (!bad)
	{
		void D_QogLitQCheck (void);
		D_QogLitQCheck ();	// QuakeOG, the span runs' assembly (d_lit_q.h)
		void D_QogLitWrapCheck (void);
		D_QogLitWrapCheck ();	// the span loop's
	}
#endif
}
#define LITI_CHECK	if (liti_state == 2) r_qogfast = 0;
#else
#define LITI_CHECK
#endif

#ifdef QOG_DRAW_CORE1
// QuakeOG, the check above, on core 0 before core 1 draws with
// its own copy of this file (d_core1.c), which never checks
void D_QogLitCheck (void)
{
	LITI_CHECK
}
#endif

#ifdef QOG_RFAST_STATS
// QuakeOG, PC measurement of the Fast drawer's work (d_litspan.h).
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

#ifdef QOG_AR_STATS
/*
================
D_QogArStats

QuakeOG, PC measurement only. The lit spans of each surface by
length, and how much farther than the surface's nearest point each span
reaches (the nearest 1/z over the span's smaller end), summed since the
level started and printed every 100 views as "qog_arstat spans".
================
*/
static unsigned	ar_st[24];
static int		ar_frame0 = -1, ar_lastframe;

static void D_QogArStats (espan_t *pspan)
{
	float	nz = 0;

	if (ar_frame0 < 0 || r_framecount < ar_frame0)
	{
		memset (ar_st, 0, sizeof(ar_st));
		ar_frame0 = r_framecount;
	}
	if (r_framecount != ar_lastframe)
	{
		ar_lastframe = r_framecount;
		if ((r_framecount - ar_frame0) && !((r_framecount - ar_frame0) % 100))
			Con_Printf ("qog_arstat spans views %d fast %d turbo %d surf %u spans %u px %u len 1 %u 2 %u 3-4 %u 5-8 %u 9-16 %u "
				"17-32 %u 33-64 %u 65+ %u ratio 4 %u 8 %u 16 %u 32 %u 64 %u 128 %u 256 %u more %u\n",
				r_framecount - ar_frame0, r_qogfast,
#ifdef QOG_RTURBO
				r_qog_turboview,
#else
				0,
#endif
				ar_st[0], ar_st[1], ar_st[2], ar_st[3], ar_st[4], ar_st[5], ar_st[6], ar_st[7], ar_st[8],
				ar_st[9], ar_st[10], ar_st[11], ar_st[12], ar_st[13], ar_st[14], ar_st[15], ar_st[16],
				ar_st[17], ar_st[18]);
	}
	{
		espan_t	*p = pspan;
		do
		{
			float	z0 = d_ziorigin + p->v * d_zistepv + p->u * d_zistepu;
			float	z1 = z0 + (p->count - 1) * d_zistepu;
			if (z0 > nz) nz = z0;
			if (z1 > nz) nz = z1;
		} while (p->pnext != SPAN_NONE && (p = SPAN_PTR (p->pnext)));
	}
	ar_st[0]++;
	do
	{
		int		c = pspan->count, b;
		float	z0 = d_ziorigin + pspan->v * d_zistepv + pspan->u * d_zistepu;
		float	z1 = z0 + (c - 1) * d_zistepu, zm = z0 < z1 ? z0 : z1, r;

		ar_st[1]++;
		ar_st[2] += c;
		b = c <= 1 ? 0 : c <= 2 ? 1 : c <= 4 ? 2 : c <= 8 ? 3 : c <= 16 ? 4 : c <= 32 ? 5 : c <= 64 ? 6 : 7;
		ar_st[3 + b]++;
		r = zm > 0 ? nz / zm : 1e9f;
		b = r <= 4 ? 0 : r <= 8 ? 1 : r <= 16 ? 2 : r <= 32 ? 3 : r <= 64 ? 4 : r <= 128 ? 5 : r <= 256 ? 6 : 7;
		ar_st[11 + b]++;
	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR (pspan->pnext)));
}
#define QOG_ARSTATS(p)	D_QogArStats (p)
#else
#define QOG_ARSTATS(p)
#endif

#if defined(QOG_RFAST) && defined(QOG_LITSPAN_FAST)
// QuakeOG, the Fast mode drawer (r_fast 1) is a function of its
// own, so the exact drawer's code and stack stay as they were
#if defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE	__attribute__((noinline))
#endif
#ifndef QOG_RFAST_FIX
#define QOG_LITFAST_FN													\
static QOG_NOINLINE void D_DrawSpans8LitFast (espan_t *pspan, int step)	\
{																		\
	(void)step;															\
	LITI_PROLOGUE														\
	{																	\
	D_LITSPANS_FAST_BODY												\
	}																	\
}
#define D_LITFAST(p, step)	D_DrawSpans8LitFast (p, step)
#else
// QuakeOG, QOG_RFAST_FIX, the Fast drawer with integer spans
// (d_litspan.h). A surface whose values do not fit (D_LitFixSetup) keeps
// The earlier float drawer.
#define QOG_LITFAST_FN													\
static QOG_NOINLINE void D_DrawSpans8LitFast (espan_t *pspan, int step)	\
{																		\
	(void)step;															\
	LITI_PROLOGUE														\
	{																	\
	D_LITSPANS_FAST_BODY												\
	}																	\
}																		\
static QOG_NOINLINE void D_DrawSpans8LitFix (espan_t *pspan, int step)	\
{																		\
	(void)step;															\
	LITI_PROLOGUE														\
	{																	\
	D_LITSPANS_FIX_BODY													\
	}																	\
}
#ifdef QOG_LITSPAN_Q
// QuakeOG, the Fast drawer with 32-bit span starts, the light
// runs and pixels in one loop and the z written by it (d_lit_q.h)
static void D_DrawSpans8LitQ (espan_t *pspan, int step);
#define D_LITFAST(p, step)	(litfix_ok ? (lit_kind <= 3 ? D_DrawSpans8LitQ (p, step) : D_DrawSpans8LitFix (p, step)) \
								: D_DrawSpans8LitFast (p, step))
#else
#define D_LITFAST(p, step)	(litfix_ok ? D_DrawSpans8LitFix (p, step) : D_DrawSpans8LitFast (p, step))
#endif

#ifdef QOG_LITSPAN_Q
#include "d_lit_q.h"	// QuakeOG
#endif

/*
================
D_LitFixSetup

QuakeOG, the surface's fixed point values for D_DrawSpans8LitFix,
on core 0 before the spans are shared with core 1, from id's float
gradients (D_CalcGradients) at the first span's start. litfix_ok is 0 when
they do not fit, and the float drawer draws the surface.
================
*/
// QuakeOG, x * 2^k truncated to a 64-bit integer, as
// (long long)(x * 2^k) gives it (the product is exact), from the float's
// bits, so no float multiply or conversion routine runs. 0 when |x * 2^k|
// may reach 2^62, a NaN or an infinity.
static int litfix_scale (float x, int k, long long *out)
{
	union { float f; unsigned u; } c;
	unsigned	m;
	int			e, sh;
	long long	v;

	c.f = x;
	e = (int)((c.u >> 23) & 0xFF);
	if (e == 0)
	{
		*out = 0;			// zero or a denormal, far below one unit
		return 1;
	}
	if (e == 255)
		return 0;
	m = (c.u & 0x7FFFFF) | 0x800000;	// x = m * 2^(e - 150), m below 2^24
	sh = e - 150 + k;
	if (sh > 38)
		return 0;
	if (sh >= 0)
		v = (long long)m << sh;
	else if (sh > -24)
		v = (long long)(m >> -sh);
	else
		v = 0;
	*out = (c.u & 0x80000000u) ? -v : v;
	return 1;
}

static int litfix_bits64 (long long x)	// bits in |x|
{
	unsigned long long	a = x < 0 ? (unsigned long long)-x : (unsigned long long)x;
	unsigned			hi = (unsigned)(a >> 32);

	return hi ? 32 + litfix_bitlen (hi) : litfix_bitlen ((unsigned)a);
}

static QOG_NOINLINE void D_LitFixSetup (espan_t *pspan)	// its frame never under the drawer's
{
	float		nz, du, dv, zr, sr, tr, a, su, sv, tu, tv;
	int			e, kexp, ws, wt, b;
	union { float f; unsigned u; } c;

	litfix_ok = 0;
	du = (float)pspan->u;
	dv = (float)pspan->v;
	zr = d_ziorigin + dv*d_zistepv + du*d_zistepu;
	if (!(zr > 0))
		return;
	nz = d_qognearzi;
	if (!(nz >= zr))
		nz = zr;			// also a NaN
	c.f = nz;
	e = (int)((c.u >> 23) & 0xFF) - 127;	// nz below 2^(e + 1)
	kexp = 60 - e;							// so 1/z * 2^k stays below 2^61
	if (e < -100 || kexp < 31 || kexp > 127)
		return;
	if (!litfix_scale (zr, kexp, &litfix_d0) || !litfix_scale (d_zistepu, kexp, &litfix_du) ||
		!litfix_scale (d_zistepv, kexp, &litfix_dv))
		return;

	// s and t with sadjust and tadjust folded in, Ns / D is s / 65536 in
	// [0, 2^ws) over the surface
	a = (float)sadjust * (1.0f / 65536.0f);
	sr = (d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu) + a * zr;
	su = d_sdivzstepu + a * d_zistepu;
	sv = d_sdivzstepv + a * d_zistepv;
	a = (float)tadjust * (1.0f / 65536.0f);
	tr = (d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu) + a * zr;
	tu = d_tdivzstepu + a * d_zistepu;
	tv = d_tdivzstepv + a * d_zistepv;
	ws = litfix_bitlen ((unsigned)(bbextents >> 16) + 1) + 1;
	wt = litfix_bitlen ((unsigned)(bbextentt >> 16) + 1) + 1;
	if (ws > 15 || wt > 15)
		return;
	if (!litfix_scale (sr, kexp - ws, &litfix_s0) || !litfix_scale (su, kexp - ws, &litfix_su) ||
		!litfix_scale (sv, kexp - ws, &litfix_sv) || !litfix_scale (tr, kexp - wt, &litfix_t0) ||
		!litfix_scale (tu, kexp - wt, &litfix_tu) || !litfix_scale (tv, kexp - wt, &litfix_tv))
		return;

	litfix_uref = pspan->u;
	litfix_vref = pspan->v;
	litfix_sw = 16 - ws;
	litfix_tw = 16 - wt;
	b = litfix_bits64 (litfix_du);
	if (b < litfix_bits64 (litfix_su))
		b = litfix_bits64 (litfix_su);
	if (b < litfix_bits64 (litfix_tu))
		b = litfix_bits64 (litfix_tu);
	litfix_stepbits = b - 25;
	litfix_zsh = kexp - 31;
	litfix_ok = 1;
#ifdef QOG_LITSPAN_Q
	// QuakeOG, the rest of the surface's values for litq_span (d_lit_q.h)
	litq_surf.ssw = litfix_sw;
	litq_surf.tsw = litfix_tw;
	litq_surf.exts = bbextents;
	litq_surf.extt = bbextentt;
#endif
}
#endif
#endif

#if defined(QOG_AR_DUMP) && defined(QOG_RFAST_FIX)
/*
================
D_QogArDump

QuakeOG, PC measurement only. `qog_ardump <file> <first view>
<views>` writes every Fast lit surface of those views with what the span
drawers read (gradients, the lit_ and litfix_ values, the light grid, the
mip level's texels, the spans), so build-ar's emulator can replay them
through the board's own drawers and count their cycles. The colour map
comes first.
================
*/
static FILE	*ar_dumpf;
static int	ar_dump0, ar_dumpn;

void D_QogArDump_f (void)
{
	if (Cmd_Argc () < 4)
	{
		Con_Printf ("qog_ardump <file> <first view> <views>\n");
		return;
	}
	if (ar_dumpf)
		fclose (ar_dumpf);
	ar_dumpf = fopen (Cmd_Argv (1), "wb");
	ar_dump0 = Q_atoi (Cmd_Argv (2));
	ar_dumpn = Q_atoi (Cmd_Argv (3));
	if (ar_dumpf)
		fwrite (vid.colormap, 1, 16384, ar_dumpf);
}

static void D_QogArDump (espan_t *pspan)
{
	int		hdr[64], n = 0, i;
	espan_t	*p;

	if (!ar_dumpf || r_framecount < ar_dump0)
		return;
	if (r_framecount >= ar_dump0 + ar_dumpn)
	{
		fclose (ar_dumpf);
		ar_dumpf = NULL;
		Con_Printf ("qog_ardump: done\n");
		return;
	}
	memset (hdr, 0, sizeof(hdr));
	memcpy (&hdr[0], &d_sdivzstepu, 4); memcpy (&hdr[1], &d_tdivzstepu, 4); memcpy (&hdr[2], &d_zistepu, 4);
	memcpy (&hdr[3], &d_sdivzstepv, 4); memcpy (&hdr[4], &d_tdivzstepv, 4); memcpy (&hdr[5], &d_zistepv, 4);
	memcpy (&hdr[6], &d_sdivzorigin, 4); memcpy (&hdr[7], &d_tdivzorigin, 4); memcpy (&hdr[8], &d_ziorigin, 4);
	hdr[9] = sadjust; hdr[10] = tadjust; hdr[11] = bbextents; hdr[12] = bbextentt;
	memcpy (&hdr[13], &d_qognearzi, 4);
	hdr[14] = lit_tw; hdr[15] = lit_th; hdr[16] = lit_soff; hdr[17] = lit_toff; hdr[18] = lit_smax;
	hdr[19] = lit_lshift; hdr[20] = lit_kind; hdr[21] = lit_soff16; hdr[22] = lit_toff16;
	hdr[23] = lit_smask; hdr[24] = lit_tshift; hdr[25] = (int)lit_tmask2; hdr[26] = lit_tw16; hdr[27] = lit_th16;
#ifdef QOG_LITSPAN_INTERP
	hdr[28] = (int)lit_ictrl0; hdr[29] = (int)lit_ictrl1;
#endif
	hdr[30] = (int)(lit_lbase - blocklights);
	hdr[31] = litfix_ok; hdr[32] = litfix_uref; hdr[33] = litfix_vref;
	hdr[34] = litfix_sw; hdr[35] = litfix_tw; hdr[36] = litfix_stepbits; hdr[37] = litfix_zsh;
	hdr[38] = screenwidth;
#ifdef QOG_RTURBO
	hdr[39] = r_qog_turboview;
#endif
	for (p = pspan ; ; p = SPAN_PTR (p->pnext))
	{
		n++;
		if (p->pnext == SPAN_NONE)
			break;
	}
	hdr[40] = n;
	hdr[41] = lit_tw * lit_th;
	fwrite (hdr, 4, 64, ar_dumpf);
	{
		long long	f[9];
		f[0] = litfix_d0; f[1] = litfix_du; f[2] = litfix_dv; f[3] = litfix_s0; f[4] = litfix_su;
		f[5] = litfix_sv; f[6] = litfix_t0; f[7] = litfix_tu; f[8] = litfix_tv;
		fwrite (f, 8, 9, ar_dumpf);
	}
	fwrite (blocklights, 4, 18 * 18, ar_dumpf);
	fwrite (lit_tex, 1, lit_tw * lit_th, ar_dumpf);
	for (p = pspan, i = 0 ; i < n ; i++, p = SPAN_PTR (p->pnext))
	{
		short	sp[4];
		sp[0] = p->u; sp[1] = p->v; sp[2] = p->count; sp[3] = 0;
		fwrite (sp, 2, 4, ar_dumpf);
	}
}
#define QOG_ARDUMP(p)	D_QogArDump (p)
#else
#define QOG_ARDUMP(p)
#endif

/*
================
D_SetupLitSurface

Per surface, per frame, the light grid and the texture to sample.
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
	// QuakeOG, measurement options for the board (exact, the same
	// bytes). Texels, or the colour map, read from flash through the
	// RP2040's XIP no-allocate alias (0x11000000), a read the XIP cache
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
	// QuakeOG, the restructured drawer's per-surface values (d_litspan.h)
	LITSPAN_SETUP (blocklights)
#ifdef QOG_LITSPAN_INTERP
	// QuakeOG, (S >> 16) & smask and (T2 >> 16) & tmask2 as lanes
	if (lit_kind <= 3)
	{
		int		hbits = 0;

		while ((1 << hbits) < lit_th)
			hbits++;
		lit_ictrl0 = LITI_CTRL (16, 0, lit_tshift - 1);
		lit_ictrl1 = LITI_CTRL (16, lit_tshift, lit_tshift + hbits - 1);
	}
#endif
#if defined(QOG_LITSPAN_Q) && defined(QOG_RFAST_FIX)
	// QuakeOG, the surface's values for litq_runs (d_lit_q.h)
	litq_surf.lbase = lit_lbase;
	litq_surf.lrowbytes = lit_smax * 4;
	litq_surf.mip = mip;
	litq_surf.tshift = lit_tshift;
	litq_surf.recip = litspan_recip;
	litq_surf.soff16 = lit_soff16;
	litq_surf.toff16 = lit_toff16;
#endif
#endif
}

#ifdef QOG_LITSPAN_FAST
/*
=============
D_DrawSpans8Lit

QuakeOG, the same pixels as id's D_DrawSpans8 with LitPixel below, with
no modulo and fewer instructions per pixel (d_litspan.h).
=============
*/

#if defined(QOG_PHASE_TIME) || defined(QOG_C1_PHCOUNT)	// QuakeOG, core 1 counts too in the cost model's builds
static void D_QogCountLit (espan_t *pspan, int any)
{
	if (!QOG_PH_ON)
		return;		// the phase sums are core 0's
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
QuakeOG, the display chip's second core draws half of each lit
surface's spans (QOG_SPANS_CORE1, board client only, exact). Core 0 sets
the surface up (D_SetupLitSurface, D_CalcGradients) as before, then core 1
draws the 2nd, 4th, 6th ... span of the list while core 0 draws the 1st,
3rd, 5th ..., and core 0 waits for core 1 before the next surface. A span
writes only its own pixels and z, and the drawer only reads the surface's
state (the lit_* values, the gradients, the light grid), which nothing
changes until both cores are done, so the picture is the same in any
order. The port runs the other half, core 1 on the board (vid_rp2040.c),
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
// QuakeOG, Fast mode, every step-th span from pspan. Both cores
// pick the drawer before they call it, so the two frames never stack.
QOG_LITFAST_FN
#define D_LITSTEP(p, step)												\
	(r_qogfast ? D_LITFAST (p, step) : D_DrawSpans8LitStep (p, step))
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
	D_LITSTEP ((espan_t *)pspan, 2);	// QuakeOG, or the Fast drawer
}

void D_DrawSpans8Lit (espan_t *pspan)
{
#if defined(QOG_PHASE_TIME) || defined(QOG_C1_PHCOUNT)	// QuakeOG, core 1 counts too in the cost model's builds
	D_QogCountLit (pspan, lit_kind == 4);
#endif
	LITI_CHECK	// QuakeOG, the interpolators once, else Exact
	QOG_ARSTATS (pspan);	// QuakeOG, PC measurement
#if defined(QOG_RFAST) && defined(QOG_RFAST_FIX)
	if (r_qogfast)
	{
		D_LitFixSetup (pspan);	// QuakeOG, before core 1 reads it
		QOG_ARDUMP (pspan);		// QuakeOG, PC measurement
	}
#endif
	if (pspan->pnext == SPAN_NONE
#ifdef QOG_DRAW_CORE1
		|| d_qogpipe_on		// QuakeOG, core 1 draws surfaces of its own
#endif
		)
	{
		D_LITSTEP (pspan, 1);	// QuakeOG, or the Fast drawer
		return;
	}
	Sys_QogCore1Start (D_QogSpansCore1, SPAN_PTR (pspan->pnext));
	D_LITSTEP (pspan, 2);	// QuakeOG, or the Fast drawer
	Sys_QogCore1Join ();
}

#else	// !QOG_SPANS_CORE1

#ifdef QOG_RFAST
// QuakeOG, Fast mode, a drawer of its own, and the exact drawer
// below in a function of its own, so the two frames never stack
QOG_LITFAST_FN
static QOG_NOINLINE void D_DrawSpans8LitExact (espan_t *pspan);

void D_DrawSpans8Lit (espan_t *pspan)
{
#if defined(QOG_PHASE_TIME) || defined(QOG_C1_PHCOUNT)	// QuakeOG, core 1 counts too in the cost model's builds
	D_QogCountLit (pspan, lit_kind == 4);
#endif
	LITI_CHECK	// QuakeOG, the interpolators once, else Exact
	QOG_ARSTATS (pspan);	// QuakeOG, PC measurement
	if (r_qogfast)
	{
#ifdef QOG_RFAST_FIX
		D_LitFixSetup (pspan);	// QuakeOG
		QOG_ARDUMP (pspan);		// QuakeOG, PC measurement
#endif
		D_LITFAST (pspan, 1);
	}
	else
		D_DrawSpans8LitExact (pspan);
}

static QOG_NOINLINE void D_DrawSpans8LitExact (espan_t *pspan)
#else
void D_DrawSpans8Lit (espan_t *pspan)
#endif
{
#if (defined(QOG_PHASE_TIME) || defined(QOG_C1_PHCOUNT)) && !defined(QOG_RFAST)
	D_QogCountLit (pspan, lit_kind == 4);
#endif
	{
#if defined(QOG_LITSPAN16) && defined(QOG_LITSPAN_LINLIGHT)
	D_LITSPANS16LIN_BODY	// QuakeOG, approximate, both below
#elif defined(QOG_LITSPAN16)
	D_LITSPANS16_BODY		// QuakeOG, approximate, one float divide per 16 pixels
#elif defined(QOG_LITSPAN_LINLIGHT)
	D_LITSPANS8LIN_BODY		// QuakeOG, approximate, light linear along each run
#else
	D_LITSPANS8_BODY		// QuakeOG, exact
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
