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
// d_litspan.h -- QuakeOG, the lit span drawer's body (QOG_LITSPAN_FAST),
// shared by engine/d_scanlit.c and the board benchmark (apps/qogbench), so
// the benchmark times exactly the engine's code. No engine headers needed.
//
// It draws the same pixels as d_scanlit.c's D_DrawSpans8Lit with LitPixel.
// id's D_DrawSpans8 float stepping, unchanged, then per pixel the texel,
// the 4-sample light blend and the colormap. What changed is only how the
// integer work is done.
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
// - lshift as a constant, one copy of the pixel loop per mip level, so every
//   shift is an immediate (the M0+ shifts a register by a register in place,
//   which costs a copy and a register).
//
// The includer defines LITSPAN_NEXT(pspan), an expression that advances
// pspan and is false at the end of the list, and provides the variables
// the macros name, the lit_* state D_SetupLitSurface sets (d_scanlit.c),
// id's d_* gradients, sadjust, tadjust, bbextents, bbextentt,
// d_viewbuffer, screenwidth and the fixed16_t type.

#ifndef D_LITSPAN_H
#define D_LITSPAN_H

// QuakeOG, every flash read of the drawer goes through
// LITSPAN_RD (kind 0 texel, 1 colour map), so the PC measurement build
// (QOG_XIPSIM, engine/d_xipsim.c) can model the board's XIP cache
#ifdef QOG_XIPSIM
const unsigned char *XipSim_Rd (int kind, const unsigned char *p);
#define LITSPAN_RD(kind, base, idx)	(*XipSim_Rd ((kind), (const unsigned char *)&(base)[idx]))
#else
#define LITSPAN_RD(kind, base, idx)	((base)[idx])
#endif

// Power-of-two texture, lshift L (4 - mip), a run of n pixels (1 to 8) in
// two passes, so each pass keeps its values in the M0+'s eight low
// registers. Pass 1 blends the light of each pixel and stores the colormap
// row in lrow[]. Pass 2 fetches the texels and maps them. Both passes jump
// into an unrolled sequence (Duff's device), pixel i of the run at slot
// 8 - n + i, so every slot offset is a constant.
//
// fx is (S >> (L + 8)) & 0xFF as two shifts (no mask register). The texel
// row uses T scaled by the texture width (T2 = T << tshift, unsigned and
// wrapping), (T2 >> 16) & (tmask << tshift) is ((T >> 16) & tmask) << tshift
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
		pd[i] = LITSPAN_RD (1, lrow[i], LITSPAN_RD (0, tx, ((T2 >> 16) & tmask2) + ((S >> 16) & smask)));	\
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

// Any texture size, wrapped 16.16 texture coordinates, the light from the
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
			*pd++ = LITSPAN_RD (1, cm, (light_ & 0xFF00) + LITSPAN_RD (0, tx, (tw_ >> 16) * tw + (sw >> 16)));	\
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

// QOG_LITSPAN_LINLIGHT (approximate, off), power-of-two texture, the light
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

// QOG_LITSPAN_INTERP (exact, board client), the linear light run
// below with its texel address and colour map row from the RP2040's SIO
// interpolators, which each core has a pair of. Interpolator 0 holds S
// and T2 in its accumulators and the texture in BASE2, its FULL result is
// tx + ((S >> 16) & smask) + ((T2 >> 16) & tmask2), the texel's address,
// and reading it adds sstep and tstep2 (BASE0, BASE1) to S and T2, as the
// pixel loop does. Interpolator 1 holds the light accumulator and the
// colour map, FULL is cm + ((lacc >> 16) & 0x3F00), the light row
// (lacc >> 24 << 8 while the light is below 64 rows, which the blend
// guarantees), and the read steps lacc. So a pixel is two register reads,
// two byte loads and a store, about 11 cycles in place of about 32, with
// the same values. Lane control words (both lanes ADD_RAW, shift 16).
// interpolator 0 lane 0 masks bits 0 to log2 width - 1, lane 1 bits tshift
// to tshift + log2 height - 1. Interpolator 1 lane 0 masks bits 8 to 13,
// lane 1 is 0 (accumulator and base 0). The PC emulates the registers
// with the hardware's rules, so the PC's pictures test this code too.
#ifdef QOG_LITSPAN_INTERP
#define LITI_ACC0		0
#define LITI_ACC1		1
#define LITI_BASE0		2
#define LITI_BASE1		3
#define LITI_BASE2		4
#define LITI_CTRL0		11
#define LITI_CTRL1		12
#define LITI_ADD_RAW	(1u << 18)
#define LITI_CTRL(shift, lsb, msb)	(LITI_ADD_RAW | ((unsigned)(msb) << 10) | ((unsigned)(lsb) << 5) | (unsigned)(shift))
#if defined(_WIN32)
// PC, the SIO registers of one core, as words at the hardware's offsets
// (ACCUM0, ACCUM1, BASE0 to BASE2 at words 0 to 4, POP at 5 to 7, PEEK at
// 8 to 10, CTRL_LANE0 and CTRL_LANE1 at 11 and 12)
extern __declspec(thread) unsigned	liti_regs[2][16];
static __inline unsigned liti_lane (const unsigned *r, int lane)
{
	unsigned	c = r[LITI_CTRL0 + lane], lsb = (c >> 5) & 31, msb = (c >> 10) & 31;
	unsigned	mask = (msb == 31 ? 0xFFFFFFFFu : (2u << msb) - 1) & ~((1u << lsb) - 1);

	return (r[LITI_ACC0 + lane] >> (c & 31)) & mask;
}
static __inline unsigned liti_popfull (int n)
{
	unsigned	*r = liti_regs[n];
	unsigned	l0 = liti_lane (r, 0), l1 = liti_lane (r, 1);
	unsigned	full = r[LITI_BASE2] + l0 + l1;

	r[LITI_ACC0] = r[LITI_BASE0] + ((r[LITI_CTRL0] & LITI_ADD_RAW) ? r[LITI_ACC0] : l0);
	r[LITI_ACC1] = r[LITI_BASE1] + ((r[LITI_CTRL1] & LITI_ADD_RAW) ? r[LITI_ACC1] : l1);
	return full;
}
#define LITI_SET(n, reg, v)	(liti_regs[n][reg] = (unsigned)(v))
#define LITI_POPFULL(n)		liti_popfull (n)
#else
// the board, SIO_BASE 0xD0000000, INTERP0 at 0x80, INTERP1 at 0xC0
#define LITI_REG(n, reg)	(((volatile unsigned *)(0xD0000080u + 0x40u * (n)))[reg])
#define LITI_SET(n, reg, v)	(LITI_REG (n, reg) = (unsigned)(v))
#define LITI_POPFULL(n)		LITI_REG (n, 7)
#endif

// once per surface on each core that draws it (power-of-two textures)
#define LITI_SURFACE(ctrl0_, ctrl1_)										\
	{																	\
		LITI_SET (0, LITI_CTRL0, (ctrl0_));								\
		LITI_SET (0, LITI_CTRL1, (ctrl1_));								\
		LITI_SET (0, LITI_BASE2, (size_t)lit_tex);						\
		LITI_SET (1, LITI_CTRL0, LITI_CTRL (16, 8, 13));				\
		LITI_SET (1, LITI_CTRL1, 0);									\
		LITI_SET (1, LITI_ACC1, 0);										\
		LITI_SET (1, LITI_BASE1, 0);									\
		LITI_SET (1, LITI_BASE2, (size_t)lit_colormap);					\
	}

#define LITSPAN_RUN_P2LIN(L, rs_, rt_, rn_, rpd_)						\
	{																	\
		int		la_, lz_;												\
		{																\
			const unsigned	*lb = lit_lbase;							\
			int		lrowbytes = lit_smax * 4;							\
			int		S0 = (rs_) + lit_soff16, T0 = (rt_) + lit_toff16;	\
			int		S1 = S0 + ((rn_) - 1) * sstep, T1 = T0 + ((rn_) - 1) * tstep;	\
			LITSPAN_BLEND (L, S0, T0, la_)								\
			LITSPAN_BLEND (L, S1, T1, lz_)								\
		}																\
		LITI_SET (0, LITI_ACC0, (rs_) + lit_soff16);					\
		LITI_SET (0, LITI_BASE0, sstep);								\
		LITI_SET (0, LITI_ACC1, (unsigned)((rt_) + lit_toff16) << lit_tshift);	\
		LITI_SET (0, LITI_BASE1, (unsigned)tstep << lit_tshift);		\
		LITI_SET (1, LITI_ACC0, (unsigned)la_ << 16);					\
		LITI_SET (1, LITI_BASE0, (lz_ - la_) * litspan_recip[(rn_) - 1]);	\
		{																\
			unsigned char	*pd = (rpd_), *pe = (rpd_) + (rn_);		\
			do															\
			{															\
				const unsigned char	*tp_ = (const unsigned char *)(size_t)LITI_POPFULL (0);	\
				const unsigned char	*rp_ = (const unsigned char *)(size_t)LITI_POPFULL (1);	\
				*pd++ = rp_[*tp_];										\
			} while (pd != pe);											\
		}																\
	}
#else
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
				*pd++ = LITSPAN_RD (1, cm, (lacc_ >> 24 << 8) + LITSPAN_RD (0, tx, ((T2 >> 16) & tmask2) + ((S >> 16) & smask)));	\
				lacc_ += lstep_;										\
				S += sstep;												\
				T2 += tstep2;											\
			} while (pd != pe);											\
		}																\
	}
#endif	// QOG_LITSPAN_INTERP

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
// first time this surface needs it, the same float values, computed at most
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


// QOG_LITSPAN_ZFUSE (exact), the includer may write each span's z from the
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
// SH is log2 of the run length, 3 is id's 8 (exact), 4 one float divide per
// 16 pixels (QOG_LITSPAN16, approximate, as id's D_DrawSpans16 did on x86).
// lit_kind, 0 to 3 power-of-two texture at mip 0 to 3, 4 any texture.
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
// same float stepping and clamping as id's D_DrawSpans8, with three changes.
//
// - The perspective divide is redone every 8, 16 or 32 pixels, before each
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
// QuakeOG, the Fast drawer without float work per span (below)
#if defined(QOG_RFAST) && !defined(QOG_RFAST_FLOATSETUP) && !defined(QOG_RFAST_FIX)
#define QOG_RFAST_FIX
#endif

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

// 0x10000 / x through a 32-bit integer divide, x = M * 2^e, M in [1, 2),
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

// PC measurement only (QOG_RFAST_STATS), sub-spans by length (8, 16, 32),
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
		/* per surface, the steps of each sub-span length, and the 1/z	\
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


// QOG_RFAST_FIX (Fast mode, on with QOG_RFAST unless
// QOG_RFAST_FLOATSETUP is defined), the Fast drawer above with every float
// operation of a span moved out of the span. Per span the float drawer
// evaluates s/z, t/z and 1/z at the span start (two int to float, six
// multiplies and six adds) and at each sub-span end three adds, a
// reciprocal, two multiplies and two float to int conversions, all through
// the M0+'s software float routines, about 2,000 cycles a span (1,760 spans
// a view in demo3). Here they are integers.
//
// - Per surface, on core 0 (D_LitFixSetup, d_scanlit.c, about 40 float
//   operations), the three linear functions of the screen position are
//   turned into 64-bit fixed point at a reference point on the surface (its
//   first span), D = 1/z * 2^k, and Ns, Nt = (s or t) * (1/z) * 2^(k - w),
//   with sadjust and tadjust folded in, so Ns / D is the texture coordinate
//   in [0, 2^w) over the surface (w from its extent, at most 10 bits for
//   Quake's 256-texel surfaces). 2^k puts the surface's nearest 1/z near
//   2^60, so the farthest points still have 40 or more bits.
// - Per span, the start values are the reference values plus du and dv
//   times the steps, in 64-bit integers (exact), then all six values move to
//   32 bits with one shift chosen per span, about 2^25 for D.
// - Per sub-span end, three adds, D normalised to 17 bits, one hardware
//   integer divide (2^32 / D) and two multiplies give s and t in 16.16. The
//   error is at most 2^(w - 16) texels from the dropped bits and 2^-15 of the
//   coordinate from the reciprocal, at most 1/64 texel on the widest surface.
// - The sub-span length is chosen as the float drawer chooses it (1/z
//   against |d(1/z)/du| times the length over LITSPAN_FAST_ZREL), the light
//   runs, clamping and steps are the float drawer's.
#if defined(QOG_RFAST_FIX)
typedef long long			litfix_i64;
typedef unsigned long long	litfix_u64;

// bits in x (0 for 0)
static int litfix_bitlen (unsigned x)
{
	int		n = 0;

	if (x >= 0x10000) { x >>= 16; n = 16; }
	if (x >= 0x100) { x >>= 8; n += 8; }
	if (x >= 0x10) { x >>= 4; n += 4; }
	if (x >= 0x4) { x >>= 2; n += 2; }
	if (x >= 0x2) { x >>= 1; n += 1; }
	return n + (int)x;
}

// k * x for a screen distance k (|k| below 2^15), exact modulo 2^64 as the
// sums that use it, in three 32-bit multiplies (no 64-bit multiply call)
static __inline litfix_i64 litfix_mul (int k, litfix_i64 x)
{
	unsigned	lo = (unsigned)x;
	litfix_u64	r = (litfix_u64)(litfix_i64)((int)(lo & 0xFFFF) * k) +
					((litfix_u64)(litfix_i64)((int)(lo >> 16) * k) << 16) +
					((litfix_u64)((unsigned)(x >> 32) * (unsigned)k) << 32);

	return (litfix_i64)r;
}

// D (positive, below 2^30) to a divisor of 17 bits, m kept from the last
// point, and r = (2^32 - 1) / Dm by the integer divider
#define LITFIX_NORM(D_, m_, Dm_, r_)										\
	{																	\
		unsigned	d_ = (unsigned)(D_);								\
		while ((d_ >> (m_)) >= 0x20000)									\
			(m_)++;														\
		while ((m_) > 0 && (d_ >> (m_)) < 0x10000)						\
			(m_)--;														\
		(Dm_) = d_ >> (m_);												\
		if (!(Dm_))														\
			(Dm_) = 1;													\
		(r_) = 0xFFFFFFFFu / (Dm_);										\
	}

// a texture coordinate in 16.16 from N (Ns or Nt), clamped as id clamps it.
// N / D below 0 is 0, at or above 1 (twice the surface's extent) the
// extent, so the product never passes 2^32.
#define LITFIX_COORD(N_, m_, Dm_, r_, sw_, ext_, lo_, out_)				\
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

// sub-span length limits of the float drawer, 1/z above |d(1/z)/du| times
// these takes 16 or 32 pixels
#define LITFIX_LIM16	((unsigned)(16.0f / LITSPAN_FAST_ZREL))
#define LITFIX_LIM32	((unsigned)(32.0f / LITSPAN_FAST_ZREL))

#ifndef LITFIX_ZSPAN
#define LITFIX_ZSPAN(p, D64)
#endif

#define D_LITSPANS_FIX_BODY												\
	int				count, spancount, sh, m = 8;						\
	unsigned char	*pdest;												\
	fixed16_t		s, t, snext, tnext, sstep, tstep;					\
	int				D, Ns, Nt, DU, SU, TU;								\
	unsigned		Dm, r;												\
	const int		ssw = litfix_sw, tsw = litfix_tw;					\
	LITSPAN_ZDECL														\
	LITSPAN_FAST_LROW													\
	int				kind = lit_kind;									\
																		\
	sstep = 0;															\
	tstep = 0;															\
	do																	\
	{																	\
		pdest = (unsigned char *)((unsigned char *)d_viewbuffer +		\
				(screenwidth * pspan->v) + pspan->u);					\
																		\
		count = pspan->count;											\
		LITSPAN_FAST_STAT (4, 1);										\
		LITSPAN_FAST_STAT (5, (count + 7) >> 3);						\
																		\
		/* the span start, exact 64-bit values, then one shift to 32 bits	\
		   that puts D (or the largest step) near 2^25 */				\
		{																\
			int			du_ = pspan->u - litfix_uref, dv_ = pspan->v - litfix_vref;	\
			litfix_i64	D64 = (litfix_i64)((litfix_u64)litfix_d0 +	\
					(litfix_u64)litfix_mul (du_, litfix_du) + (litfix_u64)litfix_mul (dv_, litfix_dv));	\
			litfix_i64	S64 = (litfix_i64)((litfix_u64)litfix_s0 +	\
					(litfix_u64)litfix_mul (du_, litfix_su) + (litfix_u64)litfix_mul (dv_, litfix_sv));	\
			litfix_i64	T64 = (litfix_i64)((litfix_u64)litfix_t0 +	\
					(litfix_u64)litfix_mul (du_, litfix_tu) + (litfix_u64)litfix_mul (dv_, litfix_tv));	\
			unsigned	hi_;											\
			int			b_;												\
																		\
			if (D64 < 1)												\
				D64 = 1;												\
			LITFIX_ZSPAN (pspan, D64)									\
			hi_ = (unsigned)(D64 >> 32);								\
			b_ = (hi_ ? 32 + litfix_bitlen (hi_) : litfix_bitlen ((unsigned)D64)) - 25;	\
			if (b_ < litfix_stepbits)									\
				b_ = litfix_stepbits;									\
			if (b_ < 0)													\
				b_ = 0;													\
			D = (int)(D64 >> b_);										\
			Ns = (int)(S64 >> b_);										\
			Nt = (int)(T64 >> b_);										\
			DU = (int)(litfix_du >> b_);								\
			SU = (int)(litfix_su >> b_);								\
			TU = (int)(litfix_tu >> b_);								\
		}																\
		if (D < 1)														\
			D = 1;														\
		LITFIX_NORM (D, m, Dm, r)										\
		LITFIX_COORD (Ns, m, Dm, r, ssw, bbextents, 0, s)				\
		LITFIX_COORD (Nt, m, Dm, r, tsw, bbextentt, 0, t)				\
																		\
		do																\
		{																\
		/* the longest sub-span this 1/z allows */						\
			unsigned	adu_ = (unsigned)(DU < 0 ? -DU : DU);			\
			sh = 3;														\
			if (LITSPAN_FAST_SUB > 3 && adu_ < 0x00200000u && (unsigned)D > adu_ * LITFIX_LIM16)	\
			{															\
				sh = 4;													\
				if (LITSPAN_FAST_SUB > 4 && (unsigned)D > adu_ * LITFIX_LIM32)	\
					sh = 5;												\
			}															\
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
				D += DU << sh;											\
				Ns += SU << sh;											\
				Nt += TU << sh;											\
			}															\
			else														\
			{															\
				D += DU * (spancount - 1);								\
				Ns += SU * (spancount - 1);								\
				Nt += TU * (spancount - 1);								\
			}															\
			if (D < 1)													\
				D = 1;													\
			else if (D >= 0x20000000)									\
			{															\
			/* nearer along the span, four bits less, the ratio stays */	\
				D >>= 4; Ns >>= 4; Nt >>= 4;							\
				DU >>= 4; SU >>= 4; TU >>= 4;							\
			}															\
			LITFIX_NORM (D, m, Dm, r)									\
			LITFIX_COORD (Ns, m, Dm, r, ssw, bbextents, 8, snext)		\
			LITFIX_COORD (Nt, m, Dm, r, tsw, bbextentt, 8, tnext)		\
																		\
			if (count)													\
			{															\
				sstep = (snext - s) >> sh;								\
				tstep = (tnext - t) >> sh;								\
			}															\
			else if (spancount > 1)										\
			{															\
				sstep = (snext - s) / (spancount - 1);					\
				tstep = (tnext - t) / (spancount - 1);					\
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
#endif	// QOG_RFAST_FIX

#endif	// D_LITSPAN_H
