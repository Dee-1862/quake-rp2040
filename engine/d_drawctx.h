/*
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
// d_drawctx.h -- QuakeOG, QOG_DRAW_SHARED. One copy of the lit
// surface drawing serves both cores (d_core1.c, d_core1s.c, d_core1e.c), so
// the code sits at one flash address and the cores share its lines in the
// XIP cache. Everything that drawing writes is in a context per core,
// picked by the core number. Include after quakedef.h and d_local.h.

#ifndef D_DRAWCTX_H
#define D_DRAWCTX_H

#ifndef QOG_DRAW_CORE1
#undef QOG_DRAW_SHARED		// one core draws, there is one copy anyway
#endif

// The surface's values for litq_span, written by D_SetupLitSurface and
// D_LitFixSetup before the spans are drawn (d_lit_q.h). The layout is the
// assembly's.
typedef struct
{
	const unsigned	*lbase;		// 0, blocklights moved back by the origin's cells
	int				lrowbytes;	// 4
	int				mip;		// 8
	int				tshift;		// 12
	const int		*recip;		// 16, litspan_recip
	int				ssw, tsw;	// 20, 24, litfix_sw, litfix_tw
	int				exts, extt;	// 28, 32, bbextents, bbextentt
	int				soff16;		// 36, lit_soff16
	int				toff16;		// 40, lit_toff16
} litq_surf_t;

// d_edge.c, the Fast mode gradient rows made once a view (D_QogGradFast)
typedef struct
{
	int		frame;			// r_framecount the rows are for, 0 none
	int		ok;				// the values fit
	int		row[3][3];		// the rows in 1/sc
	float	rk[3];			// 1 / (sc * 16384)
	int		org[3];			// modelorg in 1/1024 units
} d_qoggr_t;

#ifdef QOG_DRAW_SHARED
// The members carry the names of the variables they stand for with a q in
// front. The values used most come first, a Thumb load reaches 124 bytes.
typedef struct
{
	// the surface's gradients (D_CalcGradients)
	float		qsdivzstepu, qtdivzstepu, qzistepu;
	float		qsdivzstepv, qtdivzstepv, qzistepv;
	float		qsdivzorigin, qtdivzorigin, qziorigin;
	fixed16_t	qsadjust, qtadjust, qbbextents, qbbextentt;
	// d_scanlit.c, the surface's texture and light
	byte		*qlit_tex;
	byte		*qlit_colormap;
	const unsigned	*qlit_lgrid, *qlit_lbase;
	int			qlit_kind, qlit_soff16, qlit_toff16, qlit_smax;
	int			qlit_smask, qlit_tshift;
	unsigned	qlit_tmask2, qlit_ictrl0, qlit_ictrl1;
	int			qlit_tw, qlit_th, qlit_soff, qlit_toff, qlit_lshift;
	int			qlit_tw16, qlit_th16;
	int			qlitfix_ok, qlitfix_uref, qlitfix_vref;
	int			qlitfix_sw, qlitfix_tw, qlitfix_stepbits, qlitfix_zsh;
	float		qnearzi;
	int			qmiplevel;
	long long	qlitfix_d0, qlitfix_du, qlitfix_dv;
	long long	qlitfix_s0, qlitfix_su, qlitfix_sv;
	long long	qlitfix_t0, qlitfix_tu, qlitfix_tv;
	litq_surf_t	qlitq_surf;
	drawsurf_t	qr_drawsurf;
	d_qoggr_t	qd_qoggr;
} qog_drawctx_t;

extern qog_drawctx_t	qog_drawctx[2];	// core 0, core 1 (d_core1.c)

// What the drawing reads and core 0's brush entity surfaces change is core
// 0's global or core 1's own copy (d_core1.c, D_QogPipeBegin sets them to
// the world's), and each core has its light grid, core 0's r_surf.c's
extern entity_t		*c1_currententity;
extern vec3_t		c1_vright, c1_vup, c1_vpn, c1_modelorg, c1_transformed_modelorg;
extern vec3_t		transformed_modelorg;
extern unsigned		blocklights[18*18], c1_blocklights[18*18];

// What the drawing reads and core 0's brush entity surfaces change is core
// 0's global or core 1's own copy (d_core1.c, D_QogPipeBegin sets them to
// the world's), and each core has its light grid, core 0's r_surf.c's
extern entity_t		*c1_currententity;
extern vec3_t		c1_vright, c1_vup, c1_vpn, c1_modelorg, c1_transformed_modelorg;
extern vec3_t		transformed_modelorg;
extern unsigned		blocklights[18*18], c1_blocklights[18*18];

#if defined(_WIN32)
extern __declspec(thread) int	qog_pc_drawcore;	// the PC's stand-in for the core number
#define QOG_DRAWCORE	qog_pc_drawcore
#define QOG_DC			(&qog_drawctx[qog_pc_drawcore])
#else
// the core number, SIO CPUID at 0xD0000000, a one cycle load. Not volatile,
// so a function that uses it often reads it once.
static inline __attribute__((always_inline)) unsigned qog_drawcore (void)
{
	unsigned	core;

	__asm__ ("ldr %0, [%1]" : "=l" (core) : "l" (0xD0000000u));
	return core;
}
#define QOG_DRAWCORE	qog_drawcore ()

// this core's context. The empty asm hides how the pointer was made, so a
// function keeps it instead of working it out again at each use.
static inline __attribute__((always_inline)) qog_drawctx_t *qog_drawctx_this (void)
{
	qog_drawctx_t	*dc = &qog_drawctx[qog_drawcore ()];

	__asm__ ("" : "=l" (dc) : "0" (dc));
	return dc;
}
#define QOG_DC			qog_drawctx_this ()
#endif

// surf_t's members have the gradients' names, so they are read here, before
// those names are given to the context's members
static __inline void qog_drawctx_surfzi (qog_drawctx_t *dc, const surf_t *s)
{
	dc->qzistepu = s->d_zistepu;
	dc->qzistepv = s->d_zistepv;
	dc->qziorigin = s->d_ziorigin;
}

// the one copy's entry points, for core 0 (d_edge.c)
void c1_D_QogDrawLit (surf_t *s, espan_t *spans);
void c1_D_QogTurbGradients (msurface_t *pface);
#endif	// QOG_DRAW_SHARED

#endif
