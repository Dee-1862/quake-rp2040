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
// d_lit_q.h -- QuakeOG (Agent AR): the Fast and Turbo lit span drawer
// (QOG_LITSPAN_Q), included by d_scanlit.c before D_LitFixSetup, which
// fills its surface values.
//
// It draws what Agent AH's fixed point drawer (D_LITSPANS_FIX_BODY,
// d_litspan.h) draws, with the work per span cut down for the M0+:
//
// - The span start in 32 bits. D_LitFixSetup also keeps the surface's
//   1/z * 2^k, Ns and Nt and their steps shifted down by 32, so D is near
//   2^28 at the surface's nearest point. A span's start values are then
//   three 32-bit sums of two products each, done modulo 2^32 (every true
//   value of the surface fits 32 bits, so the wrapped sums are the true
//   ones). The coefficients lose at most one unit each, so a start is off
//   by at most |du| + |dv| + 1 units of D. A span whose D falls below
//   LITQ_DMIN at either end (more than 16 times farther than the nearest
//   point) takes Agent AH's 64-bit start. In timedemo demo3 no span does.
// - The z of a span far enough for the one-byte z-buffer's plain steps is
//   written by the pixel loop, not by a second walk (D_QogZSpanI).
// - The divides go straight to the SIO divider, not through the SDK's
//   wrapper, and a span of one pixel computes its end once.
// - A span's sub-spans, light runs and pixels are one routine
//   (litq_span): the light blend from one code path for every mip level
//   (S << mip puts the light cell at bit 20), the light of a sub-span's
//   runs blended in a pass before its pixels, and the interpolator's
//   texture accumulators carried from run to run. On the board it is
//   hand-written Thumb in RAM (port/rp2040/d_litq_rp2040.S), on the PC
//   litq_span_c below, which the board also keeps and checks the
//   assembly against at start-up (D_QogLitQCheck).
//
// QOG_LITQ_SHARELIGHT (on for the board): each light run ends at the next
// run's first pixel, not at its own last one, so a run's end is the next
// run's start and a run of 8 pixels blends the light once, not twice. The
// light along a span is then one continuous line through every eighth
// pixel. With the 32-bit starts it changes 0.26% of the pixels of the
// test frames in Fast mode and 0.55% in Turbo (single pixels one colour
// map row apart).

#ifndef LITQ_DMIN
#define LITQ_DMIN	(1 << 24)	// D at the nearest point is 2^28 to 2^29
#endif

#if defined(_WIN32)
#define LITQ_UDIV(a, b)	((unsigned)(a) / (unsigned)(b))
#define LITQ_SDIV(a, b)	((int)(a) / (int)(b))
#define LITQ_RAM
#else
// the SIO divider of this core (SIO_BASE 0xD0000000, DIV_UDIVIDEND 0x60,
// DIV_UDIVISOR 0x64, DIV_SDIVIDEND 0x68, DIV_SDIVISOR 0x6C, DIV_QUOTIENT
// 0x70). The quotient is ready 8 cycles after the divisor is written. An
// interrupt that divides saves and restores a dirty divider (the SDK's
// divider.S), so this is safe outside interrupts.
#define LITQ_DIVREG(r)	(((volatile unsigned *)0xD0000060u)[r])
static __inline unsigned litq_udiv (unsigned a, unsigned b)
{
	LITQ_DIVREG (0) = a;
	LITQ_DIVREG (1) = b;
	__asm volatile ("b 1f\n1: b 1f\n1: b 1f\n1: b 1f\n1:" ::: "memory");
	return LITQ_DIVREG (4);
}
static __inline int litq_sdiv (int a, int b)
{
	LITQ_DIVREG (2) = (unsigned)a;
	LITQ_DIVREG (3) = (unsigned)b;
	__asm volatile ("b 1f\n1: b 1f\n1: b 1f\n1: b 1f\n1:" ::: "memory");
	return (int)LITQ_DIVREG (4);
}
#define LITQ_UDIV(a, b)	litq_udiv ((a), (b))
#define LITQ_SDIV(a, b)	litq_sdiv ((a), (b))
#ifdef QOG_LITQ_RAM
#define LITQ_RAM	__attribute__((section (".time_critical.litq"), noinline))
#else
#define LITQ_RAM
#endif
#endif

// The surface's values, written on core 0 by D_SetupLitSurface and
// D_LitFixSetup before the spans are shared, read by both cores. The
// layout is the assembly's.
typedef struct
{
	const unsigned	*lbase;		// 0: blocklights moved back by the origin's cells
	int				lrowbytes;	// 4
	int				mip;		// 8
	int				tshift;		// 12
	const int		*recip;		// 16: litspan_recip
	int				ssw, tsw;	// 20, 24: litfix_sw, litfix_tw
	int				exts, extt;	// 28, 32: bbextents, bbextentt
	int				soff16;		// 36: lit_soff16
	int				toff16;		// 40: lit_toff16
} litq_surf_t;

// One span for litq_span: its start values, its pixels and z. The layout
// is the assembly's, which also keeps its own values at the end.
typedef struct
{
	int				D, Ns, Nt;	// 0, 4, 8: 1/z * 2^k and s, t times it, at the first pixel
	int				DU, SU, TU;	// 12, 16, 20: their steps a pixel
	int				m;			// 24: the divisor's shift, kept from span to span
	int				count;		// 28: pixels
	unsigned char	*pd;		// 32: the first pixel
	d_zval_t		*pz;		// 36: its z, NULL when D_QogZSpanI wrote it
	int				izi;		// 40: 1/z * 2^31 at the first pixel
	int				izistep;	// 44
	int				share;		// 48: a run's light ends at the next run's start
	const litq_surf_t	*surf;	// 52: the surface's values (each core's copy of this file has its own)
	int				own[22];	// 56: the assembly's (s, t, the next ones, steps, the runs' light)
} litq_span_t;

static litq_surf_t	litq_surf;


// id's four-sample light at S, T (with the origin added), every mip level
// through one path: S << mip puts the light column at bit 20 and the
// fraction in bits 12 to 19, as S >> (lshift + 16) and S << (16 - lshift)
// >> 24 do (d_litspan.h's LITSPAN_BLEND)
static __inline int litq_blend (const unsigned *lb, int rb, int m, int S, int T)
{
	unsigned		sx = (unsigned)S << m, tx = (unsigned)T << m;
	const unsigned	*l = (const unsigned *)((const unsigned char *)lb +
				(int)(tx >> 20) * rb + ((sx >> 20) << 2));
	int				f = (int)(sx << 12 >> 24);
	int				a = (int)l[0];
	int				top = a + ((((int)l[1] - a) * f) >> 8);
	int				bot;

	l = (const unsigned *)((const unsigned char *)l + rb);
	a = (int)l[0];
	bot = a + ((((int)l[1] - a) * f) >> 8);
	f = (int)(tx << 12 >> 24);
	return top + (((bot - top) * f) >> 8);
}

// The runs of one sub-span of n pixels from S, T (origin added): runs of
// up to 8 pixels, the light blended at a run's first and last pixel and
// stepped between them on interpolator 1, the texel addresses on
// interpolator 0, whose accumulators carry over to the next run (8 steps
// of a full run are the next run's start). The same pixels as
// d_litspan.h's LITSPAN_FAST_RUNS with LITSPAN_RUN_P2LIN. With share, each
// run's light ends at the next run's first pixel (the last run at its own
// last pixel), so the next run reuses it. Returns izi at the next pixel.
static int litq_runs_c (unsigned char *pd, d_zval_t *pz, int izi, int izs,
	int S, int T, int ss, int ts, int n, int share)
{
	const unsigned	*lb = litq_surf.lbase;
	int				rb = litq_surf.lrowbytes, m = litq_surf.mip, tsh = litq_surf.tshift;
	int				la = litq_blend (lb, rb, m, S, T);

	LITI_SET (0, LITI_ACC0, S);
	LITI_SET (0, LITI_BASE0, ss);
	LITI_SET (0, LITI_ACC1, (unsigned)T << tsh);
	LITI_SET (0, LITI_BASE1, (unsigned)ts << tsh);
	for (;;)
	{
		int		k = n > 8 ? 8 : n, lz, i;

		if (k == 1)
			lz = la;
		else if (share && n > 8)
			lz = litq_blend (lb, rb, m, S + 8 * ss, T + 8 * ts);
		else
			lz = litq_blend (lb, rb, m, S + (k - 1) * ss, T + (k - 1) * ts);
		LITI_SET (1, LITI_ACC0, (unsigned)la << 16);
		LITI_SET (1, LITI_BASE0, (lz - la) * (share && n > 8 ? 8192 : litspan_recip[k - 1]));
		for (i = 0 ; i < k ; i++)
		{
			const unsigned char	*tp = (const unsigned char *)(size_t)LITI_POPFULL (0);
			const unsigned char	*rp = (const unsigned char *)(size_t)LITI_POPFULL (1);
			pd[i] = rp[*tp];
			if (pz)
			{
				pz[i] = (d_zval_t)(izi >> 19);
				izi += izs;
			}
		}
		if ((n -= 8) <= 0)
			break;
		pd += 8;
		if (pz)
			pz += 8;
		S += 8 * ss;
		T += 8 * ts;
		la = share ? lz : litq_blend (lb, rb, m, S, T);
	}
	return izi;
}

// a texture coordinate in 16.16 from N (Ns or Nt), clamped as id clamps it
// (d_litspan.h's LITFIX_COORD)
#define LITQ_COORD(N_, m_, Dm_, r_, sw_, ext_, lo_, out_)					\
	{																	\
		int		n_ = (N_) >> (m_);										\
		if (n_ <= 0)													\
			(out_) = 0;													\
		else if ((unsigned)n_ >= (Dm_))									\
			(out_) = (ext_);											\
		else															\
			(out_) = (fixed16_t)(((unsigned)n_ * (r_)) >> (sw_));		\
		if ((out_) > (ext_))											\
			(out_) = (ext_);											\
		else if ((out_) < (lo_))										\
			(out_) = (lo_);												\
	}

// D to a divisor of 17 bits, m kept from the last point, r = (2^32 - 1) / Dm
#define LITQ_NORM(D_, m_, Dm_, r_)											\
	{																	\
		unsigned	d_ = (unsigned)(D_);								\
		while ((d_ >> (m_)) >= 0x20000)									\
			(m_)++;														\
		while ((m_) > 0 && (d_ >> (m_)) < 0x10000)						\
			(m_)--;														\
		(Dm_) = d_ >> (m_);												\
		if (!(Dm_))														\
			(Dm_) = 1;													\
		(r_) = LITQ_UDIV (0xFFFFFFFFu, (Dm_));							\
	}

/*
================
litq_span_c

QuakeOG (Agent AR): one span from its start values: Agent AH's sub-spans
(D_LITSPANS_FIX_BODY: the length by how fast 1/z changes, the ends by one
divide, the steps) and litq_runs_c for the pixels. The board's assembly
(litq_span_asm) does the same.
================
*/
static QOG_NOINLINE void litq_span_c (litq_span_t *c)
{
	int				D = c->D, Ns = c->Ns, Nt = c->Nt, DU = c->DU, SU = c->SU, TU = c->TU;
	int				m = c->m, count = c->count, sh, spancount, izi = c->izi;
	const int		ssw = litq_surf.ssw, tsw = litq_surf.tsw;
	const int		exts = litq_surf.exts, extt = litq_surf.extt;
	unsigned char	*pdest = c->pd;
	d_zval_t		*pz = c->pz;
	fixed16_t		s, t, snext, tnext, sstep = 0, tstep = 0;
	unsigned		Dm, r;

	LITQ_NORM (D, m, Dm, r)
	LITQ_COORD (Ns, m, Dm, r, ssw, exts, 0, s)
	LITQ_COORD (Nt, m, Dm, r, tsw, extt, 0, t)
	do
	{
		unsigned	adu_ = (unsigned)(DU < 0 ? -DU : DU);

		// the longest sub-span this 1/z allows (as the float drawer chooses it)
		sh = 3;
		if (LITSPAN_FAST_SUB > 3 && adu_ < 0x00400000u && (unsigned)D > adu_ * LITFIX_LIM16)
		{
			sh = 4;
			if (LITSPAN_FAST_SUB > 4 && (unsigned)D > adu_ * LITFIX_LIM32)
				sh = 5;
		}
		spancount = count >= (1 << sh) ? 1 << sh : count;
		count -= spancount;

		snext = s;
		tnext = t;
		if (count || spancount > 1)
		{
			if (count)
			{
				D += DU << sh;
				Ns += SU << sh;
				Nt += TU << sh;
			}
			else
			{
				D += DU * (spancount - 1);
				Ns += SU * (spancount - 1);
				Nt += TU * (spancount - 1);
			}
			if (D < 1)
				D = 1;
			else if (D >= 0x20000000)
			{
			// nearer along the span: four bits less, the ratio stays
				D >>= 4; Ns >>= 4; Nt >>= 4;
				DU >>= 4; SU >>= 4; TU >>= 4;
			}
			LITQ_NORM (D, m, Dm, r)
			LITQ_COORD (Ns, m, Dm, r, ssw, exts, 8, snext)
			LITQ_COORD (Nt, m, Dm, r, tsw, extt, 8, tnext)
			if (count)
			{
				sstep = (snext - s) >> sh;
				tstep = (tnext - t) >> sh;
			}
			else
			{
				sstep = LITQ_SDIV (snext - s, spancount - 1);
				tstep = LITQ_SDIV (tnext - t, spancount - 1);
			}
		}

		izi = litq_runs_c (pdest, pz, izi, c->izistep, s + litq_surf.soff16, t + litq_surf.toff16,
				sstep, tstep, spancount, c->share);
		pdest += spancount;
		if (pz)
			pz += spancount;
		s = snext;
		t = tnext;
	} while (count > 0);
	c->m = m;
}

#if defined(QOG_LITQ_ASM) && !defined(_WIN32)
void litq_span_asm (litq_span_t *c);	// port/rp2040/d_litq_rp2040.S, shared by both cores
// D_QogLitQCheck found the assembly equal to litq_span_c: one flag for both
// cores' copies of this file, written once at start-up before core 1 draws
#ifdef QOG_C1_COPY
extern int	litq_asm_ok;
#else
int			litq_asm_ok;
#endif
#define LITQ_SPAN(c)	(litq_asm_ok ? litq_span_asm (c) : litq_span_c (c))

#ifndef QOG_C1_COPY

/*
================
D_QogLitQCheck

QuakeOG (Agent AR): once at start-up (from D_QogLitInterpCheck), the
assembly draws made-up spans next to litq_span_c: 1 to 80 pixels, every mip
level, with and without z and shared light, 1/z and its steps over the
range D_DrawSpans8LitQ gives, a light grid of pseudo-random values, texels
and colour map rows read from flash. litq_asm_ok only when every pixel and
z and the divisor's shift are the same.
================
*/
void D_QogLitQCheck (void)
{
	unsigned char	pa[96], pb[96], za[96], zb[96];
	unsigned		seed = 0x2545F491u;
	int				i, n, mip, mode, bad = 0;
	litq_span_t		ca, cb;

	for (i = 0 ; i < 18 * 18 ; i++)
	{
		seed = seed * 1664525u + 1013904223u;
		blocklights[i] = (seed >> 10) & 0x3FFF;
	}
	litq_surf.lbase = blocklights;
	litq_surf.lrowbytes = 18 * 4;
	litq_surf.tshift = 6;
	litq_surf.recip = litspan_recip;
	litq_surf.ssw = 16 - 10;
	litq_surf.tsw = 16 - 10;
	litq_surf.soff16 = 0;
	litq_surf.toff16 = 0;
	LITI_SET (0, LITI_CTRL0, LITI_CTRL (16, 0, 5));
	LITI_SET (0, LITI_CTRL1, LITI_CTRL (16, 6, 11));
	LITI_SET (0, LITI_BASE2, 0x10004000u);			// texels from flash
	LITI_SET (1, LITI_CTRL0, LITI_CTRL (16, 8, 13));
	LITI_SET (1, LITI_CTRL1, 0);
	LITI_SET (1, LITI_ACC1, 0);
	LITI_SET (1, LITI_BASE1, 0);
	LITI_SET (1, LITI_BASE2, 0x10008000u);			// colour map rows from flash
	for (mip = 0 ; mip < 4 ; mip++)
	{
		litq_surf.mip = mip;
		litq_surf.exts = ((272 << 16) >> mip) - 1;	// a grid of 18 cells
		litq_surf.extt = ((272 << 16) >> mip) - 1;
		for (mode = 0 ; mode < 4 ; mode++)
			for (n = 1 ; n <= 80 ; n += 1 + (n > 20))
			{
				seed = seed * 1664525u + 1013904223u;
				memset (&ca, 0, sizeof(ca));
				ca.D = 0x01000000 + (int)(seed & 0x0FFFFFFF);
				ca.DU = (int)((seed >> 3) & 0x1FFFF) - 0x10000;
				ca.Ns = (int)(((unsigned)ca.D >> 2) + ((seed >> 5) & 0xFFFF));
				ca.Nt = (int)(((unsigned)ca.D >> 3) + ((seed >> 7) & 0xFFFF));
				ca.SU = (int)((seed >> 9) & 0xFFFF) - 0x8000 + ca.DU / 4;
				ca.TU = (int)((seed >> 11) & 0xFFFF) - 0x8000 + ca.DU / 8;
				ca.m = (int)(seed >> 28);
				ca.count = n;
				ca.izi = (int)(seed & 0x00FFFFFF);
				ca.izistep = (int)((seed >> 13) & 0x7FFF) - 0x4000;
				ca.share = mode >> 1;
				ca.surf = &litq_surf;
				cb = ca;
				memset (pa, 0, sizeof(pa)); memset (pb, 0, sizeof(pb));
				memset (za, 0, sizeof(za)); memset (zb, 0, sizeof(zb));
				ca.pd = pa; ca.pz = (mode & 1) ? (d_zval_t *)za : NULL;
				cb.pd = pb; cb.pz = (mode & 1) ? (d_zval_t *)zb : NULL;
				litq_span_c (&ca);
				litq_span_asm (&cb);
				if (memcmp (pa, pb, sizeof(pa)) || memcmp (za, zb, sizeof(za)) || ca.m != cb.m)
					bad++;
			}
	}
	litq_asm_ok = !bad;
	if (bad)
		Con_Printf ("Fast spans in C: the assembly check failed %d times\n", bad);
}
#endif	// !QOG_C1_COPY
#else
#define LITQ_SPAN(c)	litq_span_c (c)
#endif

// Agent AH's span start (D_LITSPANS_FIX_BODY), for the rare span that
// reaches too far for the 32-bit one, out of the drawer so its 64-bit code
// stays in flash
static QOG_NOINLINE void litq_start64 (int du_, int dv_, litq_span_t *c)
{
	litfix_i64	D64 = (litfix_i64)((litfix_u64)litfix_d0 +
			(litfix_u64)litfix_mul (du_, litfix_du) + (litfix_u64)litfix_mul (dv_, litfix_dv));
	litfix_i64	S64 = (litfix_i64)((litfix_u64)litfix_s0 +
			(litfix_u64)litfix_mul (du_, litfix_su) + (litfix_u64)litfix_mul (dv_, litfix_sv));
	litfix_i64	T64 = (litfix_i64)((litfix_u64)litfix_t0 +
			(litfix_u64)litfix_mul (du_, litfix_tu) + (litfix_u64)litfix_mul (dv_, litfix_tv));
	unsigned	hi_;
	int			b_;

	if (D64 < 1)
		D64 = 1;
	c->izi = (D64 >> litfix_zsh) > 0x7FFFFFFF ? 0x7FFFFFFF : (int)(D64 >> litfix_zsh);
	hi_ = (unsigned)(D64 >> 32);
	b_ = (hi_ ? 32 + litfix_bitlen (hi_) : litfix_bitlen ((unsigned)D64)) - 25;
	if (b_ < litfix_stepbits)
		b_ = litfix_stepbits;
	if (b_ < 0)
		b_ = 0;
	c->D = (int)(D64 >> b_);
	c->Ns = (int)(S64 >> b_);
	c->Nt = (int)(T64 >> b_);
	c->DU = (int)(litfix_du >> b_);
	c->SU = (int)(litfix_su >> b_);
	c->TU = (int)(litfix_tu >> b_);
}

/*
================
D_DrawSpans8LitQ

QuakeOG (Agent AR): every step-th span from pspan (1, or 2 with both cores):
the 32-bit start, the z, then litq_span.
================
*/
static LITQ_RAM QOG_NOINLINE void D_DrawSpans8LitQ (espan_t *pspan, int step)	// out of line: its frame never under Agent AH's drawer
{
	// the surface's 32-bit coefficients: the high words of Agent AH's 64-bit
	// values (D_LitFixSetup), so D is 2^28 to 2^29 at the nearest point
	const int		d0 = (int)(litfix_d0 >> 32), du = (int)(litfix_du >> 32), dv = (int)(litfix_dv >> 32);
	const int		s0 = (int)(litfix_s0 >> 32), su = (int)(litfix_su >> 32), sv = (int)(litfix_sv >> 32);
	const int		t0 = (int)(litfix_t0 >> 32), tu = (int)(litfix_tu >> 32), tv = (int)(litfix_tv >> 32);
	const int		zsh = 32 - litfix_zsh;	// 1/z * 2^31 is D << zsh, or D >> -zsh (litfix_zsh is k - 31)
	const int		uref = litfix_uref, vref = litfix_vref;
	int				izistep = (int)(d_zistepu * 0x8000 * 0x10000);
	litq_span_t		c;

	LITI_PROLOGUE	// a power-of-two texture (D_LITFAST sends the others to Agent AH's drawer)
#ifdef QOG_LITQ_SHARELIGHT
	c.share = 1;	// each light run ends at the next one's start (QOG_LITQ_SHARELIGHT)
#else
	c.share = 0;
#endif
	c.m = 8;
	c.izistep = izistep;
	c.surf = &litq_surf;
	for (;;)
	{
		int		du_ = pspan->u - uref, dv_ = pspan->v - vref, count = pspan->count, izi, D;

		c.count = count;
		c.pd = (unsigned char *)d_viewbuffer + screenwidth * pspan->v + pspan->u;
		D = (int)((unsigned)d0 + (unsigned)du_ * (unsigned)du + (unsigned)dv_ * (unsigned)dv);
		if (D >= LITQ_DMIN && D + (count - 1) * du >= LITQ_DMIN)
		{
			c.D = D;
			c.Ns = (int)((unsigned)s0 + (unsigned)du_ * (unsigned)su + (unsigned)dv_ * (unsigned)sv);
			c.Nt = (int)((unsigned)t0 + (unsigned)du_ * (unsigned)tu + (unsigned)dv_ * (unsigned)tv);
			c.DU = du;
			c.SU = su;
			c.TU = tu;
			if (zsh >= 31)
				izi = 0x7FFFFFFF;		// 1/z past 2^30
			else if (zsh >= 0)
				izi = (unsigned)D >= (0x80000000u >> zsh) ? 0x7FFFFFFF : D << zsh;
			else
				izi = zsh <= -31 ? 0 : D >> -zsh;
		}
		else
		{
			litq_start64 (du_, dv_, &c);	// Agent AH's exact 64-bit start
			izi = c.izi;
			if (c.D < 1)
				c.D = 1;
		}

		// the z: by the pixel loop when every pixel's is z >> 3 (D_QogZSpanI's
		// far case), else by D_QogZSpanI
		c.pz = NULL;
#if defined(QOG_ZBUFFER8) && defined(QOG_ZBUFFER8_NEAR) && !defined(QOG_ZSPAN8_SLOW)
		if (izi >= 0 && izi < (1024 << 16) && izistep > -(1 << 22) && izistep < (1 << 22) &&
			(unsigned)(izi + izistep * (count - 1)) < (1024u << 16))
			c.pz = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;
		else
#endif
			D_QogZSpanI (pspan, izi, izistep);
		c.izi = izi;
		LITQ_SPAN (&c);

		// the next span of this core
		{
			int		k = step;
			do
			{
				if (pspan->pnext == SPAN_NONE)
					return;
				pspan = SPAN_PTR (pspan->pnext);
			} while (--k > 0);
		}
	}
}
