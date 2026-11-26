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
// d_core1.h -- QuakeOG, the names d_core1.c gives core 1's copy
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
// tools/core1_syms.py checks the board object against that list, a name
// missing here would be shared silently, the board link allows multiple
// definitions and the PC merges uninitialized ones.

#ifndef D_CORE1_H
#define D_CORE1_H

// With QOG_DRAW_SHARED the copy these files make is the only one both cores
// run. It keeps core 1's function names, and each value it writes is a
// member of the running core's context (d_drawctx.h) in place of core 1's
// own variable. The function names are given before the headers, so their
// declarations follow, the data names after them.
#ifdef QOG_DRAW_SHARED
#if defined(QOG_BOARDMODEL) || defined(QOG_XIPSIM) || defined(QOG_RFAST_STATS) || \
	defined(QOG_AR_STATS) || defined(QOG_AR_DUMP) || defined(QOG_LMAP_STATS)
#error "QOG_DRAW_SHARED does not have the PC's measurement sums, build without it"
#endif
#undef QOG_SPANS_CORE1		// each core draws every span of its surfaces itself
#define QOG_C1_COPY
#endif

#ifndef QOG_DRAW_SHARED
#ifdef QOG_PHASE_TIME
#define QOG_C1_TIMED	// core 1's own times (d_core1.c), the phase sums are core 0's
#endif
#undef QOG_PHASE_TIME		// the render phase sums belong to core 0
#undef QOG_PHASE_XIP
#undef QOG_RFAST_STATS		// PC measurement sums, core 0's
#undef QOG_XIPSIM			// PC measurement model, core 0's
#undef QOG_SPANS_CORE1		// core 1 draws every span of its surfaces itself
#define QOG_C1_COPY
#endif	// !QOG_DRAW_SHARED

// d_scanlit.c
#define D_QogLitInterpCheck		c1_D_QogLitInterpCheck
#define D_QogRfastStats_f		c1_D_QogRfastStats_f
#define D_QogLitCheck			c1_D_QogLitCheck
#define D_SetupLitSurface		c1_D_SetupLitSurface
#define D_DrawSpans8Lit			c1_D_DrawSpans8Lit

// r_surf.c
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

// d_edge.c
#define D_DrawPoly				c1_D_DrawPoly
#define D_MipLevelForScale		c1_D_MipLevelForScale
#define D_DrawSolidSurface		c1_D_DrawSolidSurface
#define D_CalcGradients			c1_D_CalcGradients
#define D_DrawSurfaces			c1_D_DrawSurfaces
#define D_QogDrawLit			c1_D_QogDrawLit
#define ubasestep				c1_ubasestep
#define errorterm				c1_errorterm
#define erroradjustup			c1_erroradjustup
#define erroradjustdown			c1_erroradjustdown
#define vstartscan				c1_vstartscan


#define TransformVector			c1_TransformVector

#ifdef QOG_DRAW_SHARED
#include "quakedef.h"
#include "d_local.h"
#include "r_local.h"
#include "qog_phase.h"
#include "qog_bmodel.h"
#include "qog_aqstats.h"
#include "d_drawctx.h"
#define QOG_DC_NAMES
// the render phase sums stay core 0's, now that core 0 runs this copy too
#ifdef QOG_PHASE_TIME
#define QOG_PH_ON	(!QOG_DRAWCORE)
#ifdef QOG_PHASE_XIP
#undef QOG_PH_T
#define QOG_PH_T(v)			qog_phmark_t v; if (QOG_PH_ON) qog_phase_mark (&v)
#undef QOG_PH_ADD
#define QOG_PH_ADD(ph, v)	do { if (QOG_PH_ON) qog_phase_add (ph, &(v)); } while (0)
#else
#undef QOG_PH_T
#define QOG_PH_T(v)			unsigned v = QOG_PH_ON ? qog_phase_clock () : 0
#undef QOG_PH_ADD
#define QOG_PH_ADD(ph, v)	do { if (QOG_PH_ON) qog_ph_time[ph] += qog_phase_clock () - (v); } while (0)
#endif
#undef QOG_PH_COUNT
#define QOG_PH_COUNT(c, n)	do { if (QOG_PH_ON) qog_ph_count[c] += (unsigned)(n); } while (0)
#define QOG_C1_TIMED	// core 1's own times (d_core1.c)
#endif

// the values a drawing writes, in the running core's context, and the ones
// picked by the core number
#define d_sdivzstepu            (QOG_DC->qsdivzstepu)
#define d_tdivzstepu            (QOG_DC->qtdivzstepu)
#define d_zistepu               (QOG_DC->qzistepu)
#define d_sdivzstepv            (QOG_DC->qsdivzstepv)
#define d_tdivzstepv            (QOG_DC->qtdivzstepv)
#define d_zistepv               (QOG_DC->qzistepv)
#define d_sdivzorigin           (QOG_DC->qsdivzorigin)
#define d_tdivzorigin           (QOG_DC->qtdivzorigin)
#define d_ziorigin              (QOG_DC->qziorigin)
#define sadjust                 (QOG_DC->qsadjust)
#define tadjust                 (QOG_DC->qtadjust)
#define bbextents               (QOG_DC->qbbextents)
#define bbextentt               (QOG_DC->qbbextentt)
#define miplevel                (QOG_DC->qmiplevel)
#define blocklights             (QOG_DRAWCORE ? c1_blocklights : blocklights)
#define r_drawsurf              (QOG_DC->qr_drawsurf)
#define d_qoggr                 (QOG_DC->qd_qoggr)
#define litq_surf               (QOG_DC->qlitq_surf)
#define lit_tex                 (QOG_DC->qlit_tex)
#define lit_colormap            (QOG_DC->qlit_colormap)
#define lit_lgrid               (QOG_DC->qlit_lgrid)
#define lit_lbase               (QOG_DC->qlit_lbase)
#define lit_kind                (QOG_DC->qlit_kind)
#define lit_soff16              (QOG_DC->qlit_soff16)
#define lit_toff16              (QOG_DC->qlit_toff16)
#define lit_smax                (QOG_DC->qlit_smax)
#define lit_smask               (QOG_DC->qlit_smask)
#define lit_tshift              (QOG_DC->qlit_tshift)
#define lit_tmask2              (QOG_DC->qlit_tmask2)
#define lit_ictrl0              (QOG_DC->qlit_ictrl0)
#define lit_ictrl1              (QOG_DC->qlit_ictrl1)
#define lit_tw                  (QOG_DC->qlit_tw)
#define lit_th                  (QOG_DC->qlit_th)
#define lit_soff                (QOG_DC->qlit_soff)
#define lit_toff                (QOG_DC->qlit_toff)
#define lit_lshift              (QOG_DC->qlit_lshift)
#define lit_tw16                (QOG_DC->qlit_tw16)
#define lit_th16                (QOG_DC->qlit_th16)
#define litfix_ok               (QOG_DC->qlitfix_ok)
#define litfix_uref             (QOG_DC->qlitfix_uref)
#define litfix_vref             (QOG_DC->qlitfix_vref)
#define litfix_sw               (QOG_DC->qlitfix_sw)
#define litfix_tw               (QOG_DC->qlitfix_tw)
#define litfix_stepbits         (QOG_DC->qlitfix_stepbits)
#define litfix_zsh              (QOG_DC->qlitfix_zsh)
#define litfix_d0               (QOG_DC->qlitfix_d0)
#define litfix_du               (QOG_DC->qlitfix_du)
#define litfix_dv               (QOG_DC->qlitfix_dv)
#define litfix_s0               (QOG_DC->qlitfix_s0)
#define litfix_su               (QOG_DC->qlitfix_su)
#define litfix_sv               (QOG_DC->qlitfix_sv)
#define litfix_t0               (QOG_DC->qlitfix_t0)
#define litfix_tu               (QOG_DC->qlitfix_tu)
#define litfix_tv               (QOG_DC->qlitfix_tv)
#define d_qognearzi             (QOG_DC->qnearzi)
#define currententity           (*(QOG_DRAWCORE ? &c1_currententity : &currententity))
#define vright                  (QOG_DRAWCORE ? c1_vright : vright)
#define vup                     (QOG_DRAWCORE ? c1_vup : vup)
#define vpn                     (QOG_DRAWCORE ? c1_vpn : vpn)
#define modelorg                (QOG_DRAWCORE ? c1_modelorg : modelorg)
#define transformed_modelorg    (QOG_DRAWCORE ? c1_transformed_modelorg : transformed_modelorg)
#else
// core 1's own data
#define d_qognearzi				c1_d_qognearzi
#define liti_regs				c1_liti_regs
#define litfast_stats			c1_litfast_stats
#define r_drawsurf				c1_r_drawsurf
#define blocklights				c1_blocklights
#define qog_lms					c1_qog_lms		// The earlier PC light grid counts
#define transformed_modelorg	c1_transformed_modelorg
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
#define currententity			c1_currententity
#define vright					c1_vright
#define vup						c1_vup
#define vpn						c1_vpn
#define modelorg				c1_modelorg		// The earlier Fast gradient rows read it once a view
#endif

#ifdef QOG_BOARDMODEL
// Earlier work, the board cost model's PC builds keep core 1's work counts apart
// (port/pc/boardmodel_pc.c adds both cores' sums)
#define qog_bm_count			c1_qog_bm_count
#ifdef QOG_C1_TIMED
#define QOG_C1_PHCOUNT
#endif
#endif

#endif
