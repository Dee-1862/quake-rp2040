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
// d_litspan.h -- QuakeOG: the lit span drawer's body (QOG_LITSPAN_FAST),
// shared by engine/d_scanlit.c and the board benchmark (apps/qogbench), so
// the benchmark times exactly the engine's code. No engine headers needed.
//
// It draws the same pixels as d_scanlit.c's D_DrawSpans8Lit with LitPixel:
// id's D_DrawSpans8 float stepping, unchanged, then per pixel the texel,
// the 4-sample light blend and the colormap. What changed is only how the
// integer work is done:
//
// - Texture wrap without modulo. s and t never go below 0 (id clamps the
//   run ends to [0 or 8, bbextent] and steps between them), so
//   ((s >> 16) + soff) % tw is (S >> 16) & (tw - 1) with S = s + (soff << 16)
//   when tw is a power of two. Other sizes (about one texture in nine has a
//   height of 48, 96, 160 or 192) step wrapped 16.16 coordinates with a
//   compare and subtract per pixel, after one modulo per coordinate per run.
// - The texture origin folded into the light grid pointer. texturemins are
//   multiples of 16 and texture sizes multiples of 16 at mip 0, so soff and
//   toff are whole light cells (multiples of 1 << lshift at any mip). Then
//   S >> (lshift + 16) is the light column plus soff >> lshift and the
//   fraction bits are the same, so one base pointer moved back by the origin
//   serves both, and the loop keeps only S and T.
// - lshift as a constant: one copy of the pixel loop per mip level, so every
//   shift is an immediate (the M0+ shifts a register by a register in place,
//   which costs a copy and a register).
//
// The includer defines LITSPAN_NEXT(pspan), an expression that advances
// pspan and is false at the end of the list, and provides the variables
// the macros name: the lit_* state D_SetupLitSurface sets (d_scanlit.c),
// id's d_* gradients, sadjust, tadjust, bbextents, bbextentt,
// d_viewbuffer, screenwidth and the fixed16_t type.

#ifndef D_LITSPAN_H
#define D_LITSPAN_H

// Power-of-two texture, lshift L (4 - mip): a run of n pixels (1 to 8) in
// two passes, so each pass keeps its values in the M0+'s eight low
// registers. Pass 1 blends the light of each pixel and stores the colormap
// row in lrow[]. Pass 2 fetches the texels and maps them. Both passes jump
// into an unrolled sequence (Duff's device), pixel i of the run at slot
// 8 - n + i, so every slot offset is a constant.
//
// fx is (S >> (L + 8)) & 0xFF as two shifts (no mask register). The texel
// row uses T scaled by the texture width (T2 = T << tshift, unsigned and
// wrapping): (T2 >> 16) & (tmask << tshift) is ((T >> 16) & tmask) << tshift
// as long as tshift + log2 height <= 16, which D_SetupLitSurface checks.
#define LITSPAN_LIGHT(L, i)												\
	{																	\
		const unsigned	*l_ = (const unsigned *)((const unsigned char *)lb +	\
				(T >> ((L) + 16)) * lrowbytes + ((S >> ((L) + 16)) << 2));	\
		int		f_ = (int)((unsigned)S << (16 - (L)) >> 24);			\
		int		a_ = (int)l_[0];										\
		int		top_ = a_ + ((((int)l_[1] - a_) * f_) >> 8);			\
		int		bot_;													\
		l_ = (const unsigned *)((const unsigned char *)l_ + lrowbytes);	\
		a_ = (int)l_[0];												\
		bot_ = a_ + ((((int)l_[1] - a_) * f_) >> 8);					\
		f_ = (int)((unsigned)T << (16 - (L)) >> 24);					\
		lrow[i] = cm + ((unsigned)(top_ + (((bot_ - top_) * f_) >> 8)) >> 8 << 8);	\
		S += sstep;														\
		T += tstep;														\
	}

#define LITSPAN_TEXEL(i)												\
	{																	\
		pd[i] = lrow[i][tx[((T2 >> 16) & tmask2) + ((S >> 16) & smask)]];	\
		S += sstep;														\
		T2 += tstep2;													\
	}

// Run of rn_ pixels (1 to 8) from s rs_, t rt_ into rpd_.
#define LITSPAN_RUN_P2(L, rs_, rt_, rn_, rpd_)							\
	{																	\
		const unsigned	*lb = lit_lbase;								\
		const unsigned char	*cm = lit_colormap;							\
		int		lrowbytes = lit_smax * 4;								\
		int		S = (rs_) + lit_soff16, T = (rt_) + lit_toff16;			\
		switch (rn_)													\
		{																\
		default:														\
		case 8: LITSPAN_LIGHT (L, 0)									\
		case 7: LITSPAN_LIGHT (L, 1)									\
		case 6: LITSPAN_LIGHT (L, 2)									\
		case 5: LITSPAN_LIGHT (L, 3)									\
		case 4: LITSPAN_LIGHT (L, 4)									\
		case 3: LITSPAN_LIGHT (L, 5)									\
		case 2: LITSPAN_LIGHT (L, 6)									\
		case 1: LITSPAN_LIGHT (L, 7)									\
		}																\
	}																	\
	{																	\
		const unsigned char	*tx = lit_tex;								\
		int		smask = lit_smask;										\
		unsigned	tmask2 = lit_tmask2, tstep2 = (unsigned)tstep << lit_tshift;	\
		unsigned	T2 = (unsigned)((rt_) + lit_toff16) << lit_tshift;	\
		int		S = (rs_) + lit_soff16;									\
		unsigned char	*pd = (rpd_) + (rn_) - 8;						\
		switch (rn_)													\
		{																\
		default:														\
		case 8: LITSPAN_TEXEL (0)										\
		case 7: LITSPAN_TEXEL (1)										\
		case 6: LITSPAN_TEXEL (2)										\
		case 5: LITSPAN_TEXEL (3)										\
		case 4: LITSPAN_TEXEL (4)										\
		case 3: LITSPAN_TEXEL (5)										\
		case 2: LITSPAN_TEXEL (6)										\
		case 1: LITSPAN_TEXEL (7)										\
		}																\
	}

// Any texture size: wrapped 16.16 texture coordinates, the light from the
// unshifted s and t through blocklights itself, lshift in a register.
#define LITSPAN_RUN_ANY(rs_, rt_, rn_, rpd_)								\
	{																	\
		const unsigned	*lb = lit_lgrid;								\
		const unsigned char	*tx = lit_tex, *cm = lit_colormap;			\
		int		lsmax = lit_smax, tw = lit_tw, l8 = lit_lshift + 8, l16 = lit_lshift + 16;	\
		int		twf = lit_tw16, thf = lit_th16;							\
		int		ss = (rs_), tt = (rt_), n = (rn_);						\
		int		sw = (ss + lit_soff16) % twf, tw_ = (tt + lit_toff16) % thf;	\
		int		ssw = sstep % twf, tsw = tstep % thf;					\
		unsigned char	*pd = (rpd_);									\
		if (ssw < 0) ssw += twf;										\
		if (tsw < 0) tsw += thf;										\
		do																\
		{																\
			const unsigned	*l_ = lb + (tt >> l16) * lsmax + (ss >> l16);	\
			int		fx_ = (ss >> l8) & 0xFF;							\
			int		top_ = (int)l_[0] + ((((int)l_[1] - (int)l_[0]) * fx_) >> 8);	\
			int		bot_ = (int)l_[lsmax] + ((((int)l_[lsmax+1] - (int)l_[lsmax]) * fx_) >> 8);	\
			int		light_ = top_ + (((bot_ - top_) * ((tt >> l8) & 0xFF)) >> 8);	\
			*pd++ = cm[(light_ & 0xFF00) + tx[(tw_ >> 16) * tw + (sw >> 16)]];	\
			ss += sstep;												\
			tt += tstep;												\
			sw += ssw;													\
			if (sw >= twf)												\
				sw -= twf;												\
			tw_ += tsw;													\
			if (tw_ >= thf)												\
				tw_ -= thf;												\
		} while (--n > 0);												\
	}

// QOG_LITSPAN_LINLIGHT (approximate, off): power-of-two texture, the light
// blended exactly at the first and the last pixel of the run and stepped
// linearly between them (16.16), one pass of up to 16 pixels.
#define LITSPAN_BLEND(L, S_, T_, out_)									\
	{																	\
		const unsigned	*l_ = (const unsigned *)((const unsigned char *)lb +	\
				((T_) >> ((L) + 16)) * lrowbytes + (((S_) >> ((L) + 16)) << 2));	\
		int		f_ = (int)((unsigned)(S_) << (16 - (L)) >> 24);		\
		int		a_ = (int)l_[0];										\
		int		top_ = a_ + ((((int)l_[1] - a_) * f_) >> 8);			\
		int		bot_;													\
		l_ = (const unsigned *)((const unsigned char *)l_ + lrowbytes);	\
		a_ = (int)l_[0];												\
		bot_ = a_ + ((((int)l_[1] - a_) * f_) >> 8);					\
		f_ = (int)((unsigned)(T_) << (16 - (L)) >> 24);				\
		(out_) = top_ + (((bot_ - top_) * f_) >> 8);					\
	}

#define LITSPAN_RUN_P2LIN(L, rs_, rt_, rn_, rpd_)						\
	{																	\
		int		la_, lz_;												\
		unsigned	lacc_;												\
		int		lstep_;													\
		{																\
			const unsigned	*lb = lit_lbase;							\
			int		lrowbytes = lit_smax * 4;							\
			int		S0 = (rs_) + lit_soff16, T0 = (rt_) + lit_toff16;	\
			int		S1 = S0 + ((rn_) - 1) * sstep, T1 = T0 + ((rn_) - 1) * tstep;	\
			LITSPAN_BLEND (L, S0, T0, la_)								\
			LITSPAN_BLEND (L, S1, T1, lz_)								\
		}																\
		lacc_ = (unsigned)la_ << 16;									\
		lstep_ = (lz_ - la_) * litspan_recip[(rn_) - 1];				\
		{																\
			const unsigned char	*tx = lit_tex, *cm = lit_colormap;		\
			int		smask = lit_smask;									\
			unsigned	tmask2 = lit_tmask2, tstep2 = (unsigned)tstep << lit_tshift;	\
			unsigned	T2 = (unsigned)((rt_) + lit_toff16) << lit_tshift;	\
			int		S = (rs_) + lit_soff16;								\
			unsigned char	*pd = (rpd_), *pe = (rpd_) + (rn_);		\
			do															\
			{															\
				*pd++ = cm[(lacc_ >> 24 << 8) + tx[((T2 >> 16) & tmask2) + ((S >> 16) & smask)]];	\
				lacc_ += lstep_;										\
				S += sstep;												\
				T2 += tstep2;											\
			} while (pd != pe);											\
		}																\
	}

// 65536 / n for the linear light step over n + 1 pixels (n = 0 steps none),
// for runs of up to 32 pixels (Fast mode, QOG_RFAST)
static const int	litspan_recip[32] =
{
	0, 65536, 32768, 21845, 16384, 13107, 10923, 9362,
	8192, 7282, 6554, 5958, 5461, 5041, 4681, 4369,
	4096, 3855, 3641, 3449, 3277, 3121, 2979, 2849,
	2731, 2621, 2521, 2427, 2341, 2260, 2185, 2114
};

// Per surface, after lit_tex, lit_tw, lit_th, lit_soff, lit_toff, lit_smax
// and lit_lshift are set (D_SetupLitSurface). The power-of-two path also
// needs the origin to be whole light cells, which holds for every Quake
// texture (sizes and texturemins are multiples of 16 at mip 0), but it is
// checked anyway, as is the T2 width (tshift + log2 height <= 16).
#define LITSPAN_SETUP(grid)												\
	lit_soff16 = lit_soff << 16;										\
	lit_toff16 = lit_toff << 16;										\
	lit_tw16 = lit_tw << 16;											\
	lit_th16 = lit_th << 16;											\
	lit_lgrid = (grid);													\
	lit_kind = 4;														\
	if (!(lit_tw & (lit_tw - 1)) && !(lit_th & (lit_th - 1)) && lit_tw * lit_th <= 0x10000 &&	\
		!(lit_soff & ((1 << lit_lshift) - 1)) && !(lit_toff & ((1 << lit_lshift) - 1)))	\
	{																	\
		lit_kind = 4 - lit_lshift;										\
		lit_smask = lit_tw - 1;											\
		for (lit_tshift = 0 ; (1 << lit_tshift) < lit_tw ; lit_tshift++)	\
			;															\
		lit_tmask2 = (unsigned)(lit_th - 1) << lit_tshift;				\
		lit_lbase = (grid) - ((lit_toff >> lit_lshift) * lit_smax + (lit_soff >> lit_lshift));	\
	}

// The last run of a span steps the float gradients by spancount - 1. id
// multiplies each time (an int to float conversion and three float
// multiplies per span). QOG_LITSPAN_TAILMUL (exact) keeps each product the
// first time this surface needs it: the same float values, computed at most
// once per span length per surface (96 bytes of stack, 192 with 16-pixel
// runs).
#ifdef QOG_LITSPAN_TAILMUL
#define LITSPAN_TAIL_DECL(N)											\
	float		tails_[N], tailt_[N], tailz_[N];						\
	unsigned	tdone_ = 0;
#define LITSPAN_TAIL(k)													\
	{																	\
		int		k_ = (k);												\
		if (!(tdone_ & (1u << k_)))										\
		{																\
			spancountminus1 = (float)k_;								\
			tails_[k_] = d_sdivzstepu * spancountminus1;				\
			tailt_[k_] = d_tdivzstepu * spancountminus1;				\
			tailz_[k_] = d_zistepu * spancountminus1;					\
			tdone_ |= 1u << k_;											\
		}																\
		sdivz += tails_[k_];											\
		tdivz += tailt_[k_];											\
		zi += tailz_[k_];												\
	}
#else
#define LITSPAN_TAIL_DECL(N)
#define LITSPAN_TAIL(k)													\
	spancountminus1 = (float)(k);										\
	sdivz += d_sdivzstepu * spancountminus1;							\
	tdivz += d_tdivzstepu * spancountminus1;							\
	zi += d_zistepu * spancountminus1;
#endif


// QOG_LITSPAN_ZFUSE (exact): the includer may write each span's z from the
// 1/z this drawer computes at the span start anyway (the same expression as
// D_DrawZSpans), instead of D_DrawZSpans walking the spans again.
#ifndef LITSPAN_ZSPAN
#define LITSPAN_ZDECL
#define LITSPAN_ZSPAN(p, zi)
#endif


// A power-of-two run through P2RUN, which takes at most P2MAX pixels.
#define LITSPAN_DO(P2RUN, L, P2MAX, SH)									\
	if ((P2MAX) >= (1 << (SH)))											\
	{																	\
		P2RUN (L, s, t, spancount, pdest)								\
	}																	\
	else																\
	{																	\
		fixed16_t		hs_ = s, ht_ = t;								\
		int				hn_ = spancount;								\
		unsigned char	*hp_ = pdest;									\
		for (;;)														\
		{																\
			int		k_ = hn_ > 8 ? 8 : hn_;								\
			P2RUN (L, hs_, ht_, k_, hp_)								\
			if ((hn_ -= 8) <= 0)										\
				break;													\
			hs_ += 8 * sstep;											\
			ht_ += 8 * tstep;											\
			hp_ += 8;													\
		}																\
	}

// id's D_DrawSpans8 (d_scan.c), the pixel loop replaced by the runs above.
// SH is log2 of the run length: 3 is id's 8 (exact), 4 one float divide per
// 16 pixels (QOG_LITSPAN16, approximate, as id's D_DrawSpans16 did on x86).
// lit_kind: 0 to 3 power-of-two texture at mip 0 to 3, 4 any texture.
#define D_LITSPANS_BODY(SH, P2RUN, P2MAX)								\
	int				count, spancount;									\
	unsigned char	*pdest;												\
	fixed16_t		s, t, snext, tnext, sstep, tstep;					\
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;		\
	LITSPAN_TAIL_DECL (1 << (SH))									\
	LITSPAN_ZDECL													\
	float			sdivzNstepu, tdivzNstepu, ziNstepu;					\
	int				kind = lit_kind;									\
	const unsigned char	*lrow[8];	/* colormap rows of a run, pass 1 */	\
																		\
	sstep = 0;	/* keep compiler happy */								\
	tstep = 0;	/* ditto */												\
	(void)lrow;															\
																		\
	sdivzNstepu = d_sdivzstepu * (1 << (SH));							\
	tdivzNstepu = d_tdivzstepu * (1 << (SH));							\
	ziNstepu = d_zistepu * (1 << (SH));									\
																		\
	do																	\
	{																	\
		pdest = (unsigned char *)((unsigned char *)d_viewbuffer +		\
				(screenwidth * pspan->v) + pspan->u);					\
																		\
		count = pspan->count;											\
																		\
	/* calculate the initial s/z, t/z, 1/z, s, and t and clamp */		\
		du = (float)pspan->u;											\
		dv = (float)pspan->v;											\
																		\
		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;		\
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;		\
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;					\
		LITSPAN_ZSPAN (pspan, zi)										\
		z = (float)0x10000 / zi;	/* prescale to 16.16 fixed-point */	\
																		\
		s = (int)(sdivz * z) + sadjust;									\
		if (s > bbextents)												\
			s = bbextents;												\
		else if (s < 0)													\
			s = 0;														\
																		\
		t = (int)(tdivz * z) + tadjust;									\
		if (t > bbextentt)												\
			t = bbextentt;												\
		else if (t < 0)													\
			t = 0;														\
																		\
		do																\
		{																\
		/* calculate s and t at the far end of the span */				\
			if (count >= (1 << (SH)))									\
				spancount = (1 << (SH));								\
			else														\
				spancount = count;										\
																		\
			count -= spancount;											\
																		\
			if (count)													\
			{															\
				sdivz += sdivzNstepu;									\
				tdivz += tdivzNstepu;									\
				zi += ziNstepu;											\
				z = (float)0x10000 / zi;	/* prescale to 16.16 fixed-point */	\
																		\
				snext = (int)(sdivz * z) + sadjust;						\
				if (snext > bbextents)									\
					snext = bbextents;									\
				else if (snext < 8)										\
					snext = 8;											\
																		\
				tnext = (int)(tdivz * z) + tadjust;						\
				if (tnext > bbextentt)									\
					tnext = bbextentt;									\
				else if (tnext < 8)										\
					tnext = 8;											\
																		\
				sstep = (snext - s) >> (SH);							\
				tstep = (tnext - t) >> (SH);							\
			}															\
			else														\
			{															\
				LITSPAN_TAIL (spancount - 1)								\
				z = (float)0x10000 / zi;	/* prescale to 16.16 fixed-point */	\
				snext = (int)(sdivz * z) + sadjust;						\
				if (snext > bbextents)									\
					snext = bbextents;									\
				else if (snext < 8)										\
					snext = 8;											\
																		\
				tnext = (int)(tdivz * z) + tadjust;						\
				if (tnext > bbextentt)									\
					tnext = bbextentt;									\
				else if (tnext < 8)										\
					tnext = 8;											\
																		\
				if (spancount > 1)										\
				{														\
					sstep = (snext - s) / (spancount - 1);				\
					tstep = (tnext - t) / (spancount - 1);				\
				}														\
			}															\
																		\
			switch (kind)												\
			{															\
			case 0:	LITSPAN_DO (P2RUN, 4, P2MAX, SH)	break;			\
			case 1:	LITSPAN_DO (P2RUN, 3, P2MAX, SH)	break;			\
			case 2:	LITSPAN_DO (P2RUN, 2, P2MAX, SH)	break;			\
			case 3:	LITSPAN_DO (P2RUN, 1, P2MAX, SH)	break;			\
			default: LITSPAN_RUN_ANY (s, t, spancount, pdest)	break;	\
			}															\
			pdest += spancount;											\
																		\
			s = snext;													\
			t = tnext;													\
																		\
		} while (count > 0);											\
																		\
	} while (LITSPAN_NEXT (pspan));

// The exact drawer (QOG_LITSPAN_FAST) and the approximate ones.
#define D_LITSPANS8_BODY		D_LITSPANS_BODY (3, LITSPAN_RUN_P2, 8)
#define D_LITSPANS8LIN_BODY		D_LITSPANS_BODY (3, LITSPAN_RUN_P2LIN, 16)
#define D_LITSPANS16_BODY		D_LITSPANS_BODY (4, LITSPAN_RUN_P2, 8)
#define D_LITSPANS16LIN_BODY	D_LITSPANS_BODY (4, LITSPAN_RUN_P2LIN, 16)


// Fast mode (QOG_RFAST, the r_fast cvar, approximate and hard to see). The
// same float stepping and clamping as id's D_DrawSpans8, with three changes:
//
// - The perspective divide is redone every 8, 16 or 32 pixels: before each
//   sub-span, the longest one over which 1/z changes by at most
//   LITSPAN_FAST_ZREL of its value. Affine stepping between two exact points
//   is off by at most about (texels crossed) * ZREL / 4, so floors and
//   ceilings (1/z is constant along a screen row) and walls seen straight on
//   take 32, and only walls at a steep angle close by keep 8.
// - The light is blended exactly at both ends of each run of up to
//   LITSPAN_FAST_LRUN pixels and stepped linearly between them (one pass,
//   QOG_LITSPAN_LINLIGHT's run). Textures of other sizes keep the exact
//   per-pixel light.
// - 0x10000 / zi comes from an integer divide of the mantissa (the RP2040's
//   hardware divider) with about 16 good bits, not a float divide.
#ifndef LITSPAN_FAST_SUB
#define LITSPAN_FAST_SUB	5		// log2 of the longest sub-span (3 to 5)
#endif
#ifndef LITSPAN_FAST_ZREL
#define LITSPAN_FAST_ZREL	0.08f	// largest change of 1/z over a sub-span
#endif
#ifndef LITSPAN_FAST_LRUN
#define LITSPAN_FAST_LRUN	8		// pixels per linear light run (2 to 32), 0 exact light
										// (16 changes 4.2% of the pixels of the test shots, 8 2.8%)
#endif
#ifndef LITSPAN_FAST_RECIP
#define LITSPAN_FAST_RECIP	1		// 0 keeps the float divide
#endif

// 0x10000 / x through a 32-bit integer divide: x = M * 2^e, M in [1, 2),
// q = (2^32 - 1) / (M's top 16 bits) is 2 / M in 16.16 with 16 or 17 good
// bits, and the exponent is set directly. Zero, negative, tiny, huge and
// non-finite x take the float divide.
static float litspan_frecip (float x)
{
	union { float f; unsigned u; } v;
	unsigned	q;

	v.f = x;
	if ((int)v.u < (15 << 23) || v.u >= (254u << 23))
		return (float)0x10000 / x;
	q = 0xFFFFFFFFu / (((v.u & 0x7FFFFF) | 0x800000) >> 8);
	v.u = ((269 - (v.u >> 23)) << 23) | ((q << 7) & 0x7FFFFF);
	return v.f;
}

// PC measurement only (QOG_RFAST_STATS): sub-spans by length (8, 16, 32),
// then pixels, spans and the exact drawer's runs of 8 for the same spans,
// read with qog_rfaststats (d_scanlit.c)
#ifdef QOG_RFAST_STATS
extern unsigned	litfast_stats[6];
#define LITSPAN_FAST_STAT(i, n)	(litfast_stats[i] += (n))
#else
#define LITSPAN_FAST_STAT(i, n)
#endif

#if LITSPAN_FAST_RECIP
#define LITSPAN_FAST_Z(zi)	litspan_frecip (zi)
#else
#define LITSPAN_FAST_Z(zi)	((float)0x10000 / (zi))
#endif

// a sub-span of spancount pixels as runs of linear light (or, with
// LITSPAN_FAST_LRUN 0, as the exact drawer's runs of 8, for measurements)
#if LITSPAN_FAST_LRUN
#define LITSPAN_FAST_RUN		LITSPAN_RUN_P2LIN
#define LITSPAN_FAST_RLEN		LITSPAN_FAST_LRUN
#define LITSPAN_FAST_LROW
#else
#define LITSPAN_FAST_RUN		LITSPAN_RUN_P2
#define LITSPAN_FAST_RLEN		8
#define LITSPAN_FAST_LROW		const unsigned char	*lrow[8];
#endif
#define LITSPAN_FAST_RUNS(L)											\
	{																	\
		fixed16_t		hs_ = s, ht_ = t;								\
		int				hn_ = spancount;								\
		unsigned char	*hp_ = pdest;									\
		for (;;)														\
		{																\
			int		k_ = hn_ > LITSPAN_FAST_RLEN ? LITSPAN_FAST_RLEN : hn_;	\
			LITSPAN_FAST_RUN (L, hs_, ht_, k_, hp_)						\
			if ((hn_ -= LITSPAN_FAST_RLEN) <= 0)						\
				break;													\
			hs_ += LITSPAN_FAST_RLEN * sstep;							\
			ht_ += LITSPAN_FAST_RLEN * tstep;							\
			hp_ += LITSPAN_FAST_RLEN;									\
		}																\
	}

#define D_LITSPANS_FAST_BODY											\
	int				count, spancount, sh;								\
	unsigned char	*pdest;												\
	fixed16_t		s, t, snext, tnext, sstep, tstep;					\
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;		\
	float			stepsu[LITSPAN_FAST_SUB - 2], steptu[LITSPAN_FAST_SUB - 2];	\
	float			stepzu[LITSPAN_FAST_SUB - 2], zlim[LITSPAN_FAST_SUB - 2];	\
	LITSPAN_ZDECL														\
	LITSPAN_FAST_LROW													\
	int				kind = lit_kind;									\
																		\
	sstep = 0;															\
	tstep = 0;															\
	{																	\
		/* per surface: the steps of each sub-span length, and the 1/z	\
		   above which 1/z changes by at most ZREL over that length */	\
		float	az_ = d_zistepu < 0 ? -d_zistepu : d_zistepu;			\
		int		k_;														\
		for (k_ = 0 ; k_ <= LITSPAN_FAST_SUB - 3 ; k_++)				\
		{																\
			float	n_ = (float)(8 << k_);								\
			stepsu[k_] = d_sdivzstepu * n_;								\
			steptu[k_] = d_tdivzstepu * n_;								\
			stepzu[k_] = d_zistepu * n_;								\
			zlim[k_] = az_ * n_ * (1.0f / LITSPAN_FAST_ZREL);			\
		}																\
	}																	\
																		\
	do																	\
	{																	\
		pdest = (unsigned char *)((unsigned char *)d_viewbuffer +		\
				(screenwidth * pspan->v) + pspan->u);					\
																		\
		count = pspan->count;											\
		LITSPAN_FAST_STAT (4, 1);										\
		LITSPAN_FAST_STAT (5, (count + 7) >> 3);						\
																		\
		du = (float)pspan->u;											\
		dv = (float)pspan->v;											\
																		\
		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;		\
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;		\
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;					\
		LITSPAN_ZSPAN (pspan, zi)										\
		z = LITSPAN_FAST_Z (zi);										\
																		\
		s = (int)(sdivz * z) + sadjust;									\
		if (s > bbextents)												\
			s = bbextents;												\
		else if (s < 0)													\
			s = 0;														\
																		\
		t = (int)(tdivz * z) + tadjust;									\
		if (t > bbextentt)												\
			t = bbextentt;												\
		else if (t < 0)													\
			t = 0;														\
																		\
		do																\
		{																\
		/* the longest sub-span this 1/z allows */						\
			sh = 3;														\
			while (sh < LITSPAN_FAST_SUB && zi > zlim[sh - 2])			\
				sh++;													\
			if (count >= (1 << sh))										\
				spancount = 1 << sh;									\
			else														\
				spancount = count;										\
			LITSPAN_FAST_STAT (sh - 3, 1);								\
			LITSPAN_FAST_STAT (3, spancount);							\
																		\
			count -= spancount;											\
																		\
			if (count)													\
			{															\
				sdivz += stepsu[sh - 3];								\
				tdivz += steptu[sh - 3];								\
				zi += stepzu[sh - 3];									\
				z = LITSPAN_FAST_Z (zi);								\
																		\
				snext = (int)(sdivz * z) + sadjust;						\
				if (snext > bbextents)									\
					snext = bbextents;									\
				else if (snext < 8)										\
					snext = 8;											\
																		\
				tnext = (int)(tdivz * z) + tadjust;						\
				if (tnext > bbextentt)									\
					tnext = bbextentt;									\
				else if (tnext < 8)										\
					tnext = 8;											\
																		\
				sstep = (snext - s) >> sh;								\
				tstep = (tnext - t) >> sh;								\
			}															\
			else														\
			{															\
				spancountminus1 = (float)(spancount - 1);				\
				sdivz += d_sdivzstepu * spancountminus1;				\
				tdivz += d_tdivzstepu * spancountminus1;				\
				zi += d_zistepu * spancountminus1;						\
				z = LITSPAN_FAST_Z (zi);								\
				snext = (int)(sdivz * z) + sadjust;						\
				if (snext > bbextents)									\
					snext = bbextents;									\
				else if (snext < 8)										\
					snext = 8;											\
																		\
				tnext = (int)(tdivz * z) + tadjust;						\
				if (tnext > bbextentt)									\
					tnext = bbextentt;									\
				else if (tnext < 8)										\
					tnext = 8;											\
																		\
				if (spancount > 1)										\
				{														\
					sstep = (snext - s) / (spancount - 1);				\
					tstep = (tnext - t) / (spancount - 1);				\
				}														\
			}															\
																		\
			switch (kind)												\
			{															\
			case 0:	LITSPAN_FAST_RUNS (4)	break;						\
			case 1:	LITSPAN_FAST_RUNS (3)	break;						\
			case 2:	LITSPAN_FAST_RUNS (2)	break;						\
			case 3:	LITSPAN_FAST_RUNS (1)	break;						\
			default: LITSPAN_RUN_ANY (s, t, spancount, pdest)	break;	\
			}															\
			pdest += spancount;											\
																		\
			s = snext;													\
			t = tnext;													\
																		\
		} while (count > 0);											\
																		\
	} while (LITSPAN_NEXT (pspan));

#endif	// D_LITSPAN_H
