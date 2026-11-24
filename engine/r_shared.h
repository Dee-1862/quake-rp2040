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
#ifndef GLQUAKE
// r_shared.h: general refresh-related stuff shared between the refresh and the
// driver

// FIXME: clean up and move into d_iface.h

#ifndef _R_SHARED_H_
#define _R_SHARED_H_

#define	MAXVERTS	16					// max points in a surface polygon
#define MAXWORKINGVERTS	(MAXVERTS+4)	// max points in an intermediate
										//  polygon (while processing)
// !!! if this is changed, it must be changed in d_ifacea.h too !!!
// QuakeOG: the board's screen is fixed at 320x200, and every per-row or
// per-column table (scan tables, edge lists, model and sprite span lists,
// the warp tables, the turbulence sine tables) is sized from these.
// D_ViewChanged stops with an error if the video mode is larger.
#ifdef QOG_MAXHEIGHT
#define	MAXHEIGHT		QOG_MAXHEIGHT
#else
#define	MAXHEIGHT		1024
#endif
#ifdef QOG_MAXWIDTH
#define	MAXWIDTH		QOG_MAXWIDTH
#else
#define	MAXWIDTH		1280
#endif
#define MAXDIMENSION	((MAXHEIGHT > MAXWIDTH) ? MAXHEIGHT : MAXWIDTH)

#define SIN_BUFFER_SIZE	(MAXDIMENSION+CYCLE)

#define INFINITE_DISTANCE	0x10000		// distance that's always guaranteed to
										//  be farther away than anything in
										//  the scene

//===================================================================

extern void	R_DrawLine (polyvert_t *polyvert0, polyvert_t *polyvert1);

extern int		cachewidth;
extern pixel_t	*cacheblock;
extern int		screenwidth;

extern	float	pixelAspect;

extern int		r_drawnpolycount;

extern cvar_t	r_clearcolor;

// QuakeOG: with QOG_CONST_RTABLES the turbulence tables are constants in flash
#ifdef QOG_CONST_RTABLES
#define QOG_RTABLE_CONST	const
#else
#define QOG_RTABLE_CONST
#endif
extern QOG_RTABLE_CONST int	sintable[SIN_BUFFER_SIZE];
extern QOG_RTABLE_CONST int	intsintable[SIN_BUFFER_SIZE];

extern	vec3_t	vup, base_vup;
extern	vec3_t	vpn, base_vpn;
extern	vec3_t	vright, base_vright;
extern	entity_t		*currententity;

// QuakeOG: these live on the stack (R_EdgeDrawing, R_ScanEdges). Measured
// peaks over the shareware demos and levels: 1030 edges, 435 surfaces.
// Overflowing edges or surfaces drops polygons (id counts r_outofedges);
// a smaller span buffer only flushes to the screen more often.
#ifdef QOG_NUMSTACKEDGES
#define NUMSTACKEDGES		QOG_NUMSTACKEDGES
#else
#define NUMSTACKEDGES		2400
#endif
#define	MINEDGES			NUMSTACKEDGES
#ifdef QOG_NUMSTACKSURFACES
#define NUMSTACKSURFACES	QOG_NUMSTACKSURFACES
#else
#define NUMSTACKSURFACES	800
#endif
#define MINSURFACES			NUMSTACKSURFACES
#ifdef QOG_MAXSPANS
#define	MAXSPANS			QOG_MAXSPANS
#else
#define	MAXSPANS			3000
#endif

// !!! if this is changed, it must be changed in asm_draw.h too !!!
#ifdef QOG_ESPAN_T_PACKED
// QuakeOG: 8 bytes, not 16. u, v and count are screen coordinates (at
// most MAXWIDTH), and every span lives in R_ScanEdges' span buffer, so the
// link is a 16-bit byte offset from r_spanbase, one entry before the
// buffer (SPAN_NONE is 0, which never addresses a span). surf_t.spans uses
// the same form.
typedef unsigned short	espanref_t;
#define SPAN_NONE		0
extern byte	*r_spanbase;
#define SPAN_PTR(r)		((espan_t *)(r_spanbase + (r)))
#define SPAN_REF(p)		((espanref_t)((byte *)(p) - r_spanbase))

typedef struct espan_s
{
	short			u, v, count;
	espanref_t		pnext;
} espan_t;
#else
typedef struct espan_s	*espanref_t;
#define SPAN_NONE		NULL
#define SPAN_PTR(r)		(r)
#define SPAN_REF(p)		(p)

typedef struct espan_s
{
	int				u, v, count;
	struct espan_s	*pnext;
} espan_t;
#endif

// FIXME: compress, make a union if that will help
// insubmodel is only 1, flags is fewer than 32, spanstate could be a byte
#ifdef QOG_SURF_T_SMALL
// QuakeOG: 40 bytes (needs QOG_ESPAN_T_PACKED). The active surface stack
// links are indices into surfaces[] (fewer than 65536, R_NewMap checks).
// last_u is a screen column, flags are SURF_* (at most 0x40), insubmodel
// is 0 or 1. key stays an int: the background surface uses 0x7FFFFFFF.
typedef unsigned short	surfref_t;
#define SURF_PTR(r)		(surfaces + (r))
#define SURF_REF(p)		((surfref_t)((p) - surfaces))

#ifdef QOG_SURF_T_TINY
// QuakeOG (Agent S): 32 bytes. The key is 16-bit (R_RenderFace and
// R_RenderBmodelFace stop with an error past 0xFFFE, 0xFFFF is the
// background's), spanstate a signed byte (it counts the open leading edges
// of one surface on a scan line). A world surface (insubmodel 0) belongs to
// cl_entities[0], a brush entity's to cl_visedicts[entref]. The face is a
// 16-bit index into the entity model's surfaces (0xFFFF for none).
typedef struct surf_s
{
	surfref_t	next;				// active surface stack in r_edge.c
	surfref_t	prev;				// used in r_edge.c for active surf stack
	espanref_t	spans;				// linked list of spans to draw
	short		last_u;				// set during tracing
	unsigned short	key;			// sorting key (BSP order)
	unsigned short	dataref;		// the face, see SURF_DATA
	signed char	spanstate;			// 0 = not in span, 1 = in span, -1 = inverted
	byte		insubmodel;
	byte		flags;				// currentface flags
	byte		entref;				// the brush entity, see SURF_ENTITY
	float		nearzi;				// nearest 1/z on surface, for mipmapping
	float		d_ziorigin, d_zistepu, d_zistepv;
} surf_t;
#define SURF_KEY_BACK		0xFFFF
#define SURF_KEY_MAX		0xFFFE
#define SURF_ENTITY(s)		((s)->insubmodel ? cl_visedicts[(s)->entref] : &cl_entities[0])
#define SURF_DATA(s)		((s)->dataref == 0xFFFF ? NULL : \
							 (void *)(SURF_ENTITY (s)->model->surfaces + (s)->dataref))
extern int	r_qog_visedict;		// r_main.c, the brush entity R_DrawBEntitiesOnList draws
#else
typedef struct surf_s
{
	surfref_t	next;				// active surface stack in r_edge.c
	surfref_t	prev;				// used in r_edge.c for active surf stack
	espanref_t	spans;				// linked list of spans to draw
	short		last_u;				// set during tracing
	short		spanstate;			// 0 = not in span, 1 = in span, -1 = inverted
	byte		insubmodel;
	byte		flags;				// currentface flags
	int			key;				// sorting key (BSP order)
	void		*data;				// associated data like msurface_t
	entity_t	*entity;
	float		nearzi;				// nearest 1/z on surface, for mipmapping
	float		d_ziorigin, d_zistepu, d_zistepv;
} surf_t;
#endif
#else
#define SURF_PTR(r)		(r)
#define SURF_REF(p)		(p)

typedef struct surf_s
{
	struct surf_s	*next;			// active surface stack in r_edge.c
	struct surf_s	*prev;			// used in r_edge.c for active surf stack
	espanref_t	spans;				// pointer to linked list of spans to draw
									// (QuakeOG: espanref_t, see espan_t)
	int			key;				// sorting key (BSP order)
	int			last_u;				// set during tracing
#ifdef QOG_SURF_T_PACKED
	// QuakeOG: id padded this to 64 bytes for its x86 assembly, which no
	// QuakeOG build uses. spanstate only counts overlapping edges of one
	// surface and insubmodel is 0 or 1, so they share a word (52 bytes).
	short		spanstate;			// 0 = not in span, 1 = in span, -1 = inverted
	short		insubmodel;
#else
	int			spanstate;			// 0 = not in span
									// 1 = in span
									// -1 = in inverted span (end before
									//  start)
#endif
	int			flags;				// currentface flags
	void		*data;				// associated data like msurface_t
	entity_t	*entity;
	float		nearzi;				// nearest 1/z on surface, for mipmapping
#ifndef QOG_SURF_T_PACKED
	qboolean	insubmodel;
#endif
	float		d_ziorigin, d_zistepu, d_zistepv;

#ifndef QOG_SURF_T_PACKED
	int			pad[2];				// to 64 bytes
#endif
} surf_t;
#endif

extern	surf_t	*surfaces, *surface_p, *surf_max;
#ifndef SURF_ENTITY
#define SURF_KEY_BACK		0x7FFFFFFF
#define SURF_ENTITY(s)		((s)->entity)
#define SURF_DATA(s)		((s)->data)
#endif

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack.
// surfaces[0] is a dummy, because index 0 is used to indicate no surface
//  attached to an edge_t

//===================================================================

extern vec3_t	sxformaxis[4];	// s axis transformed into viewspace
extern vec3_t	txformaxis[4];	// t axis transformed into viewspac

extern vec3_t	modelorg, base_modelorg;

extern	float	xcenter, ycenter;
extern	float	xscale, yscale;
extern	float	xscaleinv, yscaleinv;
extern	float	xscaleshrink, yscaleshrink;

extern	int d_lightstylevalue[256]; // 8.8 frac of base light value

extern void TransformVector (vec3_t in, vec3_t out);
extern void SetUpForLineScan(fixed8_t startvertu, fixed8_t startvertv,
	fixed8_t endvertu, fixed8_t endvertv);

extern int	r_skymade;
extern void R_MakeSky (void);

extern int	ubasestep, errorterm, erroradjustup, erroradjustdown;

// flags in finalvert_t.flags
#define ALIAS_LEFT_CLIP				0x0001
#define ALIAS_TOP_CLIP				0x0002
#define ALIAS_RIGHT_CLIP			0x0004
#define ALIAS_BOTTOM_CLIP			0x0008
#define ALIAS_Z_CLIP				0x0010
// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define ALIAS_ONSEAM				0x0020	// also defined in modelgen.h;
											//  must be kept in sync
#define ALIAS_XY_CLIP_MASK			0x000F

// !!! if this is changed, it must be changed in asm_draw.h too !!!
#ifdef QOG_EDGE_T_PACKED
// QuakeOG: 24 bytes, not 32. Edges live only in the edge table of one
// R_EdgeDrawing call, so the links are 16-bit byte offsets from r_edgebase
// (EDGE_NONE is 0, which never addresses an edge, see R_EdgeDrawing). The
// four sentinel edges sit in front of r_edges in the same buffer, so they
// have offsets too. owner is only compared with the world medge_t being
// drawn (R_RenderFace's cache check), so it is the medge_t's index in the
// world's edge array, or EDGE_OWNER_NONE for anything else (other models'
// edges and the clip edge tedge). R_NewMap checks that both forms fit.
typedef unsigned short	edgeref_t;
#define EDGE_NONE		0
#define EDGE_OWNER_NONE	0xFFFF
extern byte	*r_edgebase;
#define EDGE_PTR(r)		((edge_t *)(r_edgebase + (r)))
#define EDGE_REF(p)		((edgeref_t)((byte *)(p) - r_edgebase))

typedef struct edge_s
{
	fixed16_t		u;
	fixed16_t		u_step;
	edgeref_t		prev, next;
	unsigned short	surfs[2];
	edgeref_t		nextremove;
	unsigned short	owner;
	float			nearzi;
} edge_t;
#else
typedef struct edge_s	*edgeref_t;
#define EDGE_NONE		NULL
#define EDGE_PTR(r)		(r)
#define EDGE_REF(p)		(p)

typedef struct edge_s
{
	fixed16_t		u;
	fixed16_t		u_step;
	struct edge_s	*prev, *next;
	unsigned short	surfs[2];
	struct edge_s	*nextremove;
	float			nearzi;
	medge_t			*owner;
} edge_t;
#endif

// QuakeOG: the z-buffer as short-lived memory (QOG_EDGES_IN_ZBUFFER,
// QOG_EDGECACHE_ZBUFFER). VID_Init sets r_ztemp_base and r_ztemp_bytes to
// the whole z-buffer allocation (a multiple of 4 bytes, 4-byte aligned).
// Who owns its bytes when:
//  - from the end of one R_RenderView to the start of the next: the first
//    320 x 48 bytes hold the status bar rows with QOG_HUD_IN_ZBUFFER (while
//    the 2D overlay is drawn and the frame is shown), the rest is free
//  - R_RenderView up to R_EdgeDrawing: free
//  - R_EdgeDrawing: the edge cache, the brush clip buffers and the edge
//    table, see R_EdgeDrawing and R_QogScanRows
//  - from R_EdgeDrawing's first drawn span to the end of R_RenderView: z
#if defined(QOG_EDGES_IN_ZBUFFER) || defined(QOG_EDGECACHE_ZBUFFER)
#define QOG_ZTEMP
extern byte	*r_ztemp_base;
extern int	r_ztemp_bytes;
#endif

#endif	// _R_SHARED_H_

#endif	// GLQUAKE
