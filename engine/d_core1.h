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
// d_core1.h -- QuakeOG (Agent AM): the names d_core1.c gives core 1's copy
// of the lit surface drawing (d_scanlit.c, r_surf.c and d_edge.c compiled a
// second time, QOG_DRAW_CORE1). Only d_core1.c includes this, before any
// other header.
//
// Every value that drawing writes, and every value it reads that core 0's
// drawing writes at the same time (a brush entity's transform), gets a name
// of its own here, and so does every function those files define, so core
// 1 never shares them with core 0. What both cores read and nothing writes
// while the view's surfaces are drawn (the frame buffer and z-buffer
// pointers, the view's scale and centre, the texture and light data, the
// light styles and dynamic lights, r_qogfast) keeps its name.
// tools/core1_syms.py checks the board object against that list: a name
// missing here would be shared silently, the board link allows multiple
// definitions and the PC merges uninitialized ones.

#ifndef D_CORE1_H
#define D_CORE1_H

#ifdef QOG_PHASE_TIME
#define QOG_C1_TIMED	// core 1's own times (d_core1.c), the phase sums are core 0's
#endif
#undef QOG_PHASE_TIME		// the render phase sums belong to core 0
#undef QOG_PHASE_XIP
#undef QOG_RFAST_STATS		// PC measurement sums, core 0's
#undef QOG_XIPSIM			// PC measurement model, core 0's
#undef QOG_SPANS_CORE1		// core 1 draws every span of its surfaces itself
#define QOG_C1_COPY

// d_scanlit.c
#define D_QogLitInterpCheck		c1_D_QogLitInterpCheck
#define D_QogRfastStats_f		c1_D_QogRfastStats_f
#define D_QogLitCheck			c1_D_QogLitCheck
#define D_SetupLitSurface		c1_D_SetupLitSurface
#define D_DrawSpans8Lit			c1_D_DrawSpans8Lit
#define d_qognearzi				c1_d_qognearzi
#define liti_regs				c1_liti_regs
#define litfast_stats			c1_litfast_stats

// r_surf.c
#define r_drawsurf				c1_r_drawsurf
#define lightleft				c1_lightleft
#define sourcesstep				c1_sourcesstep
#define blocksize				c1_blocksize
#define sourcetstep				c1_sourcetstep
#define lightdelta				c1_lightdelta
#define lightdeltastep			c1_lightdeltastep
#define lightright				c1_lightright
#define lightleftstep			c1_lightleftstep
#define lightrightstep			c1_lightrightstep
#define blockdivshift			c1_blockdivshift
#define blockdivmask			c1_blockdivmask
#define prowdestbase			c1_prowdestbase
#define pbasesource				c1_pbasesource
#define surfrowbytes			c1_surfrowbytes
#define r_lightptr				c1_r_lightptr
#define r_stepback				c1_r_stepback
#define r_lightwidth			c1_r_lightwidth
#define r_numhblocks			c1_r_numhblocks
#define r_numvblocks			c1_r_numvblocks
#define r_source				c1_r_source
#define r_sourcemax				c1_r_sourcemax
#define blocklights				c1_blocklights
#define R_AddDynamicLights		c1_R_AddDynamicLights
#define R_BuildLightMap			c1_R_BuildLightMap
#define R_TextureAnimation		c1_R_TextureAnimation
#define R_DrawSurface			c1_R_DrawSurface
#define R_DrawSurfaceBlock8_mip0	c1_R_DrawSurfaceBlock8_mip0
#define R_DrawSurfaceBlock8_mip1	c1_R_DrawSurfaceBlock8_mip1
#define R_DrawSurfaceBlock8_mip2	c1_R_DrawSurfaceBlock8_mip2
#define R_DrawSurfaceBlock8_mip3	c1_R_DrawSurfaceBlock8_mip3
#define R_DrawSurfaceBlock16	c1_R_DrawSurfaceBlock16
#define R_GenTurbTile			c1_R_GenTurbTile
#define R_GenTurbTile16			c1_R_GenTurbTile16
#define R_GenTile				c1_R_GenTile
#define qog_lms					c1_qog_lms		// Agent AL's PC light grid counts

// d_edge.c
#define D_DrawPoly				c1_D_DrawPoly
#define D_MipLevelForScale		c1_D_MipLevelForScale
#define D_DrawSolidSurface		c1_D_DrawSolidSurface
#define D_CalcGradients			c1_D_CalcGradients
#define D_DrawSurfaces			c1_D_DrawSurfaces
#define D_QogDrawLit			c1_D_QogDrawLit
#define transformed_modelorg	c1_transformed_modelorg
#define ubasestep				c1_ubasestep
#define errorterm				c1_errorterm
#define erroradjustup			c1_erroradjustup
#define erroradjustdown			c1_erroradjustdown
#define vstartscan				c1_vstartscan

// d_vars.c and d_local.h: the surface's gradients
#define d_sdivzstepu			c1_d_sdivzstepu
#define d_tdivzstepu			c1_d_tdivzstepu
#define d_zistepu				c1_d_zistepu
#define d_sdivzstepv			c1_d_sdivzstepv
#define d_tdivzstepv			c1_d_tdivzstepv
#define d_zistepv				c1_d_zistepv
#define d_sdivzorigin			c1_d_sdivzorigin
#define d_tdivzorigin			c1_d_tdivzorigin
#define d_ziorigin				c1_d_ziorigin
#define sadjust					c1_sadjust
#define tadjust					c1_tadjust
#define bbextents				c1_bbextents
#define bbextentt				c1_bbextentt
#define cacheblock				c1_cacheblock
#define cachewidth				c1_cachewidth

// r_main.c, r_misc.c: what a brush entity's surface changes on core 0
#define currententity			c1_currententity
#define vright					c1_vright
#define vup						c1_vup
#define vpn						c1_vpn
#define modelorg				c1_modelorg		// Agent AL's Fast gradient rows read it once a view
#define TransformVector			c1_TransformVector

#ifdef QOG_BOARDMODEL
// Agent AS: the board cost model's PC builds keep core 1's work counts apart
// (port/pc/boardmodel_pc.c adds both cores' sums)
#define qog_bm_count			c1_qog_bm_count
#ifdef QOG_C1_TIMED
#define QOG_C1_PHCOUNT
#endif
#endif

#endif
