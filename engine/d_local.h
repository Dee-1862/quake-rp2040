/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// d_local.h:  private rasterization driver defs

#include "r_shared.h"

//
// TODO: fine-tune this; it's based on providing some overage even if there
// is a 2k-wide scan, with subdivision every 8, for 256 spans of 12 bytes each
//
#define SCANBUFFERPAD		0x1000

#define R_SKY_SMASK	0x007F0000
#define R_SKY_TMASK	0x007F0000

#define DS_SPAN_LIST_END	-128

#define SURFCACHE_SIZE_AT_320X200	600*1024

typedef struct surfcache_s
{
	struct surfcache_s	*next;
	struct surfcache_s 	**owner;		// NULL is an empty chunk of memory
	int					lightadj[MAXLIGHTMAPS]; // checked for strobe flush
	int					dlight;
	int					size;		// including header
	unsigned			width;
	unsigned			height;		// DEBUG only needed for debug
	float				mipscale;
	struct texture_s	*texture;	// checked for animating textures
	byte				data[4];	// width*height elements
} surfcache_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct sspan_s
{
	int				u, v, count;
} sspan_t;

extern cvar_t	d_subdiv16;

extern float	scale_for_mip;

extern qboolean		d_roverwrapped;
extern surfcache_t	*sc_rover;
extern surfcache_t	*d_initial_rover;

extern float	d_sdivzstepu, d_tdivzstepu, d_zistepu;
extern float	d_sdivzstepv, d_tdivzstepv, d_zistepv;
extern float	d_sdivzorigin, d_tdivzorigin, d_ziorigin;

fixed16_t	sadjust, tadjust;
fixed16_t	bbextents, bbextentt;


void D_DrawSpans8 (espan_t *pspans);
// QuakeOG: surface-cache-free world drawing (d_scanlit.c)
void D_SetupLitSurface (msurface_t *pface, int mip);
void D_DrawSpans8Lit (espan_t *pspans);
#ifdef QOG_SPANS_CORE1
// QuakeOG (Agent V): the port runs fn (arg) on the display chip's second
// core and Join waits for it (d_scanlit.c, vid_rp2040.c, vid_pc.c)
void Sys_QogCore1Start (void (*fn) (void *), void *arg);
void Sys_QogCore1Join (void);
#endif
void D_DrawSpans16 (espan_t *pspans);
void D_DrawZSpans (espan_t *pspans);
void Turbulent8 (espan_t *pspan);
void D_SpriteDrawSpans (sspan_t *pspan);

void D_DrawSkyScans8 (espan_t *pspan);
void D_DrawSkyScans16 (espan_t *pspan);

void R_ShowSubDiv (void);
void (*prealspandrawer)(void);
surfcache_t	*D_CacheSurface (msurface_t *surface, int miplevel);

extern int D_MipLevelForScale (float scale);

#if id386
extern void D_PolysetAff8Start (void);
extern void D_PolysetAff8End (void);
#endif

#if defined(QOG_RENDER_SCALE) && !defined(QOG_ZBUFFER_VIEW)
#define QOG_ZBUFFER_VIEW	// QuakeOG: a scaled view needs only a z-buffer of its own size
#endif
#ifdef QOG_ZBUFFER8
// QuakeOG (changes the picture, off in the shipped flag lists): one byte of
// depth per pixel instead of two. Depth is id's 16-bit 1/z value shifted
// right by QOG_ZBUFFER8_SHIFT and clamped to 0..255, so far surfaces keep
// coarse steps and everything nearer than the clamp ties (the reader wins
// a tie, as in id's >= tests).
#ifndef QOG_ZBUFFER8_SHIFT
#define QOG_ZBUFFER8_SHIFT	3
#endif
typedef byte	d_zval_t;
#ifdef QOG_ZBUFFER8_NEAR
// QuakeOG: two ranges. A 1/z value below 1024 (farther than 32 units) is
// shifted right by 3 as above, into 0..127. Nearer is 128 and up in steps of
// 1 << QOG_ZBUFFER8_NEAR, so the weapon and models up close keep their own
// depth order (with the single shift everything nearer than 16 units ties).
#define D_ZQ(z)	((z) < 0 ? 0 : (z) < 1024 ? (z) >> 3 : (((z) - 1024) >> QOG_ZBUFFER8_NEAR) > 127 ? 255 \
				 : 128 + (((z) - 1024) >> QOG_ZBUFFER8_NEAR))
#else
#define D_ZQ(z)	((z) < 0 ? 0 : ((z) >> QOG_ZBUFFER8_SHIFT) > 255 ? 255 : ((z) >> QOG_ZBUFFER8_SHIFT))
#endif
#else
typedef short	d_zval_t;
#define D_ZQ(z)	(z)
#endif
extern d_zval_t *d_pzbuffer;
#ifdef QOG_ZBUFFER_VIEW
// QuakeOG: the z-buffer holds only the view rectangle, in rows of the view's
// width. The VID port sets d_zbase, D_ViewChanged points d_pzbuffer before
// it so that screen coordinates (u, v) still index it as d_pzbuffer[v * d_zwidth + u].
extern d_zval_t *d_zbase;
#endif
#ifdef QOG_HUD_IN_ZBUFFER
extern int	r_zalias_row;	// QuakeOG: screen rows from here down live in the z-buffer (r_main.c)
void R_HudFrameEnd (void);	// QuakeOG: VID_Update calls it at the end of every frame
#endif
extern unsigned int d_zrowbytes, d_zwidth;

extern int	*d_pscantable;
#ifdef QOG_NO_ROWTABLES
// QuakeOG: row offsets are computed (one multiply) instead of read from
// d_scantable and zspantable (1.6 KB of RAM). Same values.
extern int	d_scanrowbytes;
#define D_SCANTABLE(v)		((v) * d_scanrowbytes)
#define D_ZSPANTABLE(v)		(d_pzbuffer + (v) * (int)d_zwidth)
#else
extern int	d_scantable[MAXHEIGHT];
#define D_SCANTABLE(v)		d_scantable[v]
#define D_ZSPANTABLE(v)		zspantable[v]
#endif

extern int	d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;

extern int	d_y_aspect_shift, d_pix_min, d_pix_max, d_pix_shift;

extern pixel_t	*d_viewbuffer;

#ifndef QOG_NO_ROWTABLES
extern d_zval_t	*zspantable[MAXHEIGHT];
#endif

extern int		d_minmip;
extern float	d_scalemip[3];

extern void (*d_drawspans) (espan_t *pspan);

