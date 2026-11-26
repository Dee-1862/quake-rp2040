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
// d_core1.c -- QuakeOG, core 1's copy of the lit surface drawing
// for the two-core pipeline of the world's surfaces (QOG_DRAW_CORE1, see
// d_edge.c). id's code is compiled a second time here, unchanged, with
// core 1's own names for what it writes (d_core1.h), so both cores can
// each draw a whole surface at the same time with the same statements.
// With QOG_DRAW_SHARED (the board's default) this compilation is the only
// one the cores run, with a context per core (d_drawctx.h).

#ifdef QOG_DRAW_CORE1

#include "d_core1.h"
#include "d_scanlit.c"	// r_surf.c and d_edge.c in d_core1s.c and d_core1e.c (id's headers include once per file)

#if !defined(QOG_LITSPAN_ZFUSE) || defined(QOG_SURFCACHE)
#error "QOG_DRAW_CORE1 needs the lit spans to write their own z (QOG_LITSPAN_ZFUSE, no surface cache)"
#endif
#ifndef QOG_EDGES_IN_ZBUFFER
#error "QOG_DRAW_CORE1 is written for R_ScanEdges with the edges in the z-buffer"
#endif

#ifdef QOG_DRAW_SHARED
// core 1's copies of the view values core 0's brush entity surfaces change,
// D_QogPipeBegin sets them to the world's
entity_t	*c1_currententity;
vec3_t		c1_vright, c1_vup, c1_vpn, c1_modelorg, c1_transformed_modelorg;

unsigned	c1_blocklights[18*18];

qog_drawctx_t	qog_drawctx[2];

#ifdef _WIN32
__declspec(thread) int	qog_pc_drawcore;
#endif
#else
// core 1's copies (d_core1.h), the names d_vars.c, r_main.c and r_misc.c
// give core 0's
float		d_sdivzstepu, d_tdivzstepu, d_zistepu;
float		d_sdivzstepv, d_tdivzstepv, d_zistepv;
float		d_sdivzorigin, d_tdivzorigin, d_ziorigin;
pixel_t		*cacheblock;
int			cachewidth;
entity_t	*currententity;
vec3_t		vright, vup, vpn, modelorg;
#endif

// id's TransformVector (r_misc.c) with core 1's view vectors
void TransformVector (vec3_t in, vec3_t out)
{
#ifdef QOG_DRAW_SHARED
	const float	*r = vright, *u = vup, *n = vpn;	// the core picked once

	out[0] = DotProduct(in,r);
	out[1] = DotProduct(in,u);
	out[2] = DotProduct(in,n);
#else
	out[0] = DotProduct(in,vright);
	out[1] = DotProduct(in,vup);
	out[2] = DotProduct(in,vpn);
#endif
}

#ifdef QOG_C1_TIMED
unsigned qog_phase_clock (void);
extern unsigned	qog_c1_stat[];	// qog_phase.c, QOG_C1_ in qog_phase.h
#endif

/*
==============
D_QogC1Draw

core 1's copy of D_QogDrawLit (the PC's tests call it on the game's thread)
==============
*/
void D_QogC1Draw (surf_t *s, espan_t *spans)
{
#if defined(QOG_DRAW_SHARED) && defined(_WIN32)
	qog_pc_drawcore = 1;
	D_QogDrawLit (s, spans);
	qog_pc_drawcore = 0;
#else
	D_QogDrawLit (s, spans);
#endif
}

/*
==============
D_QogC1Worker

Core 1's job while R_ScanEdges runs (d_edge.c D_QogPipeBegin), draw each
surface core 0 hands on or a flush shares, wait for more, return once core
0 says the view's surfaces are done.
==============
*/
void D_QogC1Worker (void *unused)
{
	surf_t		*s;
	espan_t		*sp;
#ifdef QOG_C1_TIMED
	unsigned	t0, t1;

	t0 = qog_phase_clock ();
#endif
	(void)unused;
#if defined(QOG_DRAW_SHARED) && defined(_WIN32)
	qog_pc_drawcore = 1;
#endif
	for (;;)
	{
		if (D_QogPipePop (1, &s, &sp))
		{
#ifdef QOG_C1_TIMED
			t1 = qog_phase_clock ();
			qog_c1_stat[QOG_C1_WAITT] += t1 - t0;
#endif
			D_QogDrawLit (s, sp);
			D_QogPipeDone ();
#ifdef QOG_C1_TIMED
			t0 = qog_phase_clock ();
			qog_c1_stat[QOG_C1_DRAWT] += t0 - t1;
			qog_c1_stat[QOG_C1_C1SURFS]++;
#endif
			continue;
		}
		if (D_QogPipeEnded ())
			break;
		Sys_QogCore1Idle ();
	}
#ifdef QOG_C1_TIMED
	qog_c1_stat[QOG_C1_WAITT] += qog_phase_clock () - t0;
#endif
}

#endif	// QOG_DRAW_CORE1
