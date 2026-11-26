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

// r_draw.c

#include "quakedef.h"
#include "r_local.h"

// QuakeOG (Agent AA): for a float x, x < 0.01 (a double) is x <= (float)0.01,
// the largest float below 0.01, so QOG_FLOAT_EXACT compares in single precision
#ifdef QOG_FLOAT_EXACT
#define QOG_NEARCLIPPED(x)	((x) <= (float)NEAR_CLIP)
#else
#define QOG_NEARCLIPPED(x)	((x) < NEAR_CLIP)
#endif
#include "d_local.h"	// FIXME: shouldn't need to include this
#include "qog_phase.h"	// QuakeOG (Agent Z): world work counts (QOG_PHASE_XIP)
#include "qog_aqstats.h"	// QuakeOG (Agent AQ): front end work counts (QOG_AQ_STATS)

#define MAXLEFTCLIPEDGES		100

// !!! if these are changed, they must be changed in asm_draw.h too !!!
#define FULLY_CLIPPED_CACHED	0x80000000
#define FRAMECOUNT_MASK			0x7FFFFFFF

unsigned int	cacheoffset;

// QuakeOG: id's cachededgeoffset per model edge (model_t.edge_cache) is a
// byte offset into r_edges, or FULLY_CLIPPED_CACHED plus the frame count,
// or 0x7FFFFFFF for "not cacheable". With QOG_EDGECACHE16 it is 16 bits:
// an edge index (below 0x7FFF), 0x7FFF for "not cacheable", or 0x8000 plus
// the frame number counted from a window start kept in front of the array
// (EDGE_CACHE_START). When a model's window reaches 0x7FFF frames the array
// is filled with EC16_NEVER and the window restarts, so a fully clipped
// entry matches only in the frame that wrote it, exactly as in id's code.
// Entries from earlier frames never match in id's code (an edge below
// edge_p owned by this medge_t was emitted this frame, which rewrote its
// entry), so clearing them changes nothing.
#ifdef QOG_EDGECACHE16
#define EC16_CLIPPED	0x8000
#define EC16_NOCACHE	0x7FFF
#define EC16_NEVER		0xFFFF
#ifndef EC16_WINDOW		// frames per window, at most 0x7FFF (smaller only to test)
#define EC16_WINDOW		0x7FFF
#endif
#ifdef QOG_EDGECACHE_ZBUFFER
// QuakeOG: the world's cache is in the z-buffer (or on the hunk, R_NewMap)
// and R_EdgeDrawing fills it with EC16_NEVER every frame, so a fully
// clipped entry carries frame 0 and matches only in the frame that wrote
// it. Only world faces read the cache (insubmodel is false only while the
// world is drawn), so brush entity faces do not store in it.
#define r_qog_ecframe	0
#else
static int		r_qog_ecframe;	// r_framecount minus the model's window start

static void R_QogEdgeCacheWindow (model_t *m)
{
	int		*start = &EDGE_CACHE_START (m);

	if (edge_max - r_edges > EC16_NOCACHE)
		Sys_Error ("R_RenderFace: %d edges, the 16-bit edge cache allows %d",
			(int)(edge_max - r_edges), EC16_NOCACHE);
	if (r_framecount - *start >= EC16_WINDOW || r_framecount < *start)
	{
		*start = r_framecount;
		memset (MODFRAME (m)->edge_cache, 0xFF, (m->numedges + 1) * sizeof(*MODFRAME (m)->edge_cache));	// QuakeOG: MODFRAME, QOG_MOD_FLASH
	}
	r_qog_ecframe = r_framecount - *start;
}
#endif

static unsigned short R_QogEdgeCacheValue (unsigned int offset)
{
	if (offset & FULLY_CLIPPED_CACHED)	// always this frame's
		return EC16_CLIPPED | r_qog_ecframe;
	if (offset == 0x7FFFFFFF)
		return EC16_NOCACHE;
	return offset / sizeof(edge_t);
}

#ifdef QOG_EDGECACHE_ZBUFFER
#define EC_CACHE(m, e)			(r_qog_edgecache[(e) - (m)->edges])
#define EC_STORE(m, e, v)		do { if (!insubmodel) EC_CACHE (m, e) = R_QogEdgeCacheValue (v); } while (0)
#else
#define EC_CACHE(m, e)			EDGE_CACHE (m, e)
#define EC_STORE(m, e, v)		(EDGE_CACHE (m, e) = R_QogEdgeCacheValue (v))
#endif
#define EC_IS_CLIPPED(m, e)		(EC_CACHE (m, e) & EC16_CLIPPED)
#define EC_CLIPPED_NOW(m, e)	((EC_CACHE (m, e) & ~EC16_CLIPPED) == r_qog_ecframe)
#define EC_EDGE(m, e)			(r_edges + EC_CACHE (m, e))
#define EC_VALID(m, e)			((edge_p - r_edges) > EC_CACHE (m, e))
#else
#define EC_IS_CLIPPED(m, e)		(EDGE_CACHE (m, e) & FULLY_CLIPPED_CACHED)
#define EC_CLIPPED_NOW(m, e)	((EDGE_CACHE (m, e) & FRAMECOUNT_MASK) == r_framecount)
#define EC_EDGE(m, e)			((edge_t *)((unsigned long)r_edges + EDGE_CACHE (m, e)))
#define EC_VALID(m, e)			(((unsigned long)edge_p - (unsigned long)r_edges) > EDGE_CACHE (m, e))
#define EC_STORE(m, e, v)		(EDGE_CACHE (m, e) = (v))
#endif

#ifdef QOG_EDGE_T_PACKED
// QuakeOG: edge_t.owner is a world medge_t index, see r_shared.h. Only
// world edges are ever looked up in the cache (insubmodel is false only
// while the world is drawn), and they always encode to their index, so the
// comparison gives the same answer as id's pointer comparison.
medge_t			*r_qog_owneredges;		// world edges, set in R_EdgeDrawing
unsigned int	r_qog_ownerbytes;		// their size in bytes

static unsigned short R_QogEdgeOwner (medge_t *pedge)
{
	unsigned int	ofs = (byte *)pedge - (byte *)r_qog_owneredges;

	if (ofs >= r_qog_ownerbytes)
		return EDGE_OWNER_NONE;
	return ofs / sizeof(medge_t);
}
#define EDGE_OWNER(p)	R_QogEdgeOwner (p)
#else
#define EDGE_OWNER(p)	(p)
#endif

int			c_faceclip;					// number of faces clipped

zpointdesc_t	r_zpointdesc;

polydesc_t		r_polydesc;



clipplane_t	*entity_clipplanes;
clipplane_t	view_clipplanes[4];
clipplane_t	world_clipplanes[16];

medge_t			*r_pedge;

qboolean		r_leftclipped, r_rightclipped;
static qboolean	makeleftedge, makerightedge;
qboolean		r_nearzionly;

#ifdef QOG_CONST_RTABLES
// QuakeOG: the turbulence tables never change after R_InitTurb, so they are
// precomputed constants in flash (r_turbtab.h, checked at startup on the PC)
#include "r_turbtab.h"
#if SIN_BUFFER_SIZE != QOG_TURBTAB_SIZE
#error "QOG_CONST_RTABLES: r_turbtab.h is generated for MAXWIDTH 320 and CYCLE 128"
#endif
const int	sintable[SIN_BUFFER_SIZE] = {
#define QOG_TURBTAB_SINTABLE
#include "r_turbtab.h"
#undef QOG_TURBTAB_SINTABLE
};
const int	intsintable[SIN_BUFFER_SIZE] = {
#define QOG_TURBTAB_INTSINTABLE
#include "r_turbtab.h"
#undef QOG_TURBTAB_INTSINTABLE
};
#else
int		sintable[SIN_BUFFER_SIZE];
int		intsintable[SIN_BUFFER_SIZE];
#endif

mvertex_t	r_leftenter, r_leftexit;
mvertex_t	r_rightenter, r_rightexit;

typedef struct
{
	float	u,v;
	int		ceilv;
} evert_t;

int				r_emitted;
float			r_nearzi;
float			r_u1, r_v1, r_lzi1;
int				r_ceilv1;

qboolean	r_lastvertvalid;


#ifdef QOG_VERTCACHE
/*
QuakeOG (Agent Z): world vertices projected once a frame. A world vertex
is shared by several edges, and every edge that is not in the edge cache
projects its end points again (on E1M1 1,060 projections a view of 474
vertices). While R_RenderWorld runs, a direct-mapped cache keeps each
projected world vertex's screen u and v, 1/z and ceil(v), computed by the
same statements as id's R_EmitEdge, so a hit gives the same bits. Brush
entity faces (insubmodel) and clipped points never use it, since they are
projected with another origin or are not world vertices. The cache lives in
memory nothing else uses while the world's edges are made (the frame
buffer's 3D view rows past the edge cache, or the z-buffer between the edge
cache and the edge table) and is emptied every frame.
*/
#ifdef QOG_VERTCACHE_CLIPDIST
/*
QuakeOG (Agent Z): with QOG_VERTCACHE_CLIPDIST each entry also keeps the
vertex's distance to each of the four view planes, as R_ClipEdge computes
it (DotProduct (position, normal) - dist), so an edge end point shared by
several faces is measured against a plane once a frame. A distance not yet
known holds the bits QOG_VC_NODIST, a projection not yet made the ceilv
QOG_VC_NOPROJ (screen rows never reach it). A distance whose bits happen to
be QOG_VC_NODIST (a NaN) is computed again, with the same result.
*/
#define QOG_VC_NODIST	0x7FC0DEADu
#define QOG_VC_NOPROJ	0x7FFF
typedef union
{
	float			f;
	unsigned int	bits;
} r_qogdist_t;
#endif

typedef struct
{
	unsigned short	tag;	// world vertex number, 0xFFFF empty
	short			ceilv;
	float			u, v, lzi;
#ifdef QOG_VERTCACHE_CLIPDIST
	r_qogdist_t		d[4];	// to view_clipplanes[0..3]
#endif
} r_qogvert_t;

#ifndef QOG_VERTCACHE_MAX
#define QOG_VERTCACHE_MAX	4096	// entries, a power of two (smaller only to test)
#endif

static r_qogvert_t	*r_qog_vertcache;	// NULL when off
static unsigned		r_qog_vertmask;
static byte			*r_qog_vertbase;	// the world's vertices
static unsigned		r_qog_vertbytes;	//   and their size, at most 0xFFFF vertices

#ifdef QOG_FRONT_FAST
#if !defined(QOG_VERTCACHE_CLIPDIST) || !defined(QOG_RFAST)
#error "QOG_FRONT_FAST needs QOG_VERTCACHE_CLIPDIST and QOG_RFAST"
#endif
/*
QuakeOG (Agent AJ): the Fast mode (r_fast 1) front end in integers.

The RP2040 has no FPU, so each float operation of id's projection is a
call of tens of cycles (about 1,750 cycles a point), while its multiply
takes one cycle and its divider eight. In Fast mode the world and brush
model front end works in fixed point:
 - a position less the view origin, in 1/1024 units within 128 units of
   the eye on each axis and in 1/32 units out to 4096, is multiplied by
   xscale vright and yscale vup (scaled by a power of two that keeps the
   products in 32 bits) and by vpn in 1/8192. The origin's rounding goes
   in as a constant per view, so the view does not move in steps.
 - z is taken to 14 bits, and u and v come from two integer divides, in
   1/256 pixel, with id's clamps. ceil (v) is an integer step.
 - the edge's u and u_step in 12.20 come from integer divides of the
   1/256 pixel ends.
 - the nearest 1/z of an edge and of a face is kept as the smallest z
   (0x7FFFFFFF - z in the float's bits, so 0 still means none) and turned
   into id's float 1/z once per surface.
 - R_ClipEdge takes the plane sides from integer distances (the view
   planes pass through the eye) and interpolates the clipped point with
   id's floats from their ratio.
 - a face's 1/z gradients come from its plane normal in 1/16384 against
   three rows made once per view or entity.
Points land within about 0.02 pixel of id's on average, so edges and span
ends move by a pixel only where id's value lies that close to a pixel
boundary (0.07% of the pixels of the test shots with the exact spans).
Points out of the integer range, at the eye or far off the view take
id's float projection. The constants per view or brush entity live in a
header after the vertex cache entries (memory nothing else uses before
the spans are drawn), so no static RAM is used.
*/
typedef struct
{
	int		on;					// set up for the current entity
	int		org[3];				// modelorg in 1/1024 units
	int		mx[3], my[3], mz[3];	// xscale vright 2^xl, yscale vup 2^yl, vpn 2^13
	int		cx, cy, cz;			// the origin's rounding through the same rows
	int		xsh, ysh;			// 21 - xl, 21 - yl
	int		xlim, ylim;			// a |x|, |y| from which the point is far off the view
	int		xc, yc;				// xcenter, ycenter in 1/256 pixel
	int		umin, umax, vmin, vmax;	// id's clamps in 1/256 pixel
	int		cn[4][4];			// view_clipplanes normals in 1/8192 and the origin's rounding
	int		gr[3][3];			// the face gradient rows (R_QogFaceGradFast), 2^gs times
	float	gk[3];				//   and 2^-(gs + 14) for each
#ifdef QOG_FX_REUSE
	float	vec[9];				// vright, vup, vpn of the values above (Agent AQ)
	float	xs, ys;				// xscale 2^xl, yscale 2^yl
#endif
#ifdef QOG_WORLDNODE_INT
	int		plon;				// the world's view planes below are set
	int		pl[4][4];			// view_clipplanes normal and dist in 1/16384
#endif
} r_qogfx_t;

// the header follows the vertex cache entries (R_QogVertCacheBegin)
#define R_QOGFX			((r_qogfx_t *)(r_qog_vertcache + r_qog_vertmask + 1))
#ifdef QOG_FRONT_FAST_ALWAYS
#define R_QOGFX_ON()	(r_qog_vertcache && R_QOGFX->on)
#else
#define R_QOGFX_ON()	(r_qogfast && r_qog_vertcache && R_QOGFX->on)
#endif
#define QOG_FX_NEARZ	655		// NEAR_CLIP in 1/65536 units
#define QOG_FX_LMAX		(1 << 17)	// |position - origin| per axis: 128 units in 1/1024, 4096 in 1/32
#define QOG_FX_RESERVE	sizeof(r_qogfx_t)
#ifndef QOG_VERTCACHE_FASTMAX
#define QOG_VERTCACHE_FASTMAX	128	// entries in Fast mode (R_QogVertCacheBegin)
#endif
// an int that may alias a float (the Fast state kept in id's float globals)
#if defined(__GNUC__)
typedef int __attribute__((may_alias))	r_qogaint_t;
#else
typedef int								r_qogaint_t;
#endif
#define QOG_FI(f)		(*(r_qogaint_t *)&(f))
#else
#define QOG_FX_RESERVE	0
#endif

#ifdef QOG_EDGECACHE_TAIL
/* QuakeOG (Agent AP): the first byte after the vertex cache and its Fast
   header in the frame buffer, where R_QogEdgeCacheOut may put the edge cache */
byte *R_QogVertCacheTop (void)
{
	if (!r_qog_vertcache)
		return vid.buffer;
	return (byte *)(r_qog_vertcache + r_qog_vertmask + 1) + QOG_FX_RESERVE;
}
#endif

/* called by R_EdgeDrawing before R_RenderWorld, after the edge cache is set */
void R_QogVertCacheBegin (void)
{
	model_t		*world = cl_entities[0].model;
	byte		*lo, *hi;
	unsigned	n, i, verts;

	r_qog_vertcache = NULL;
	if (!world || !r_qog_edgecache)
		return;
#ifdef QOG_RTURBO
	extern byte	*r_qog_turbocachelo, *r_qog_turbocachehi;

	// QuakeOG (Agent AO): a Turbo view placed the edge cache in the frame
	// buffer and gives the vertex cache this room (r_qogturbo.c R_QogTurboCaches)
	if (r_qog_turbocachehi)
	{
		lo = r_qog_turbocachelo;
		hi = r_qog_turbocachehi;
	}
	else
#endif
#ifdef QOG_EDGECACHE_TAIL
	if (r_qog_ectail)
	{
		// QuakeOG (Agent AP): the edge cache is at the end of the z-buffer
		// region, the vertex cache takes the frame's first rows and leaves
		// room after it for the edge cache, should it have to move there
		// (R_QogEdgeCacheOut)
		lo = vid.buffer;
		hi = vid.buffer + r_refdef.vrect.height * vid.rowbytes - r_qog_edgecachebytes - 4;
	}
	else
#endif
#ifdef QOG_EDGECACHE_FRAME
	if ((byte *)r_qog_edgecache == vid.buffer)
	{
		// R_EdgeDrawing has checked that the 3D view covers these rows
		lo = vid.buffer + r_qog_edgecachebytes;
		hi = vid.buffer + r_refdef.vrect.height * vid.rowbytes;
	}
	else
#endif
	{
		lo = r_ztemp_base;
		if ((byte *)r_qog_edgecache == r_ztemp_base)
			lo += r_qog_edgecachebytes;
		hi = (byte *)r_edgesentinels;
	}
	lo = (byte *)(((size_t)lo + 3) & ~(size_t)3);
	if (hi <= lo + QOG_FX_RESERVE)	// QuakeOG (Agent AJ): the Fast header
		return;
	n = (unsigned)(hi - lo - QOG_FX_RESERVE) / sizeof(r_qogvert_t);
	if (n > QOG_VERTCACHE_MAX)
		n = QOG_VERTCACHE_MAX;
#ifdef QOG_FRONT_FAST
	// QuakeOG (Agent AJ): in Fast mode a miss costs a few hundred cycles,
	// not 1,750, so fewer entries do: the cache then ends in a higher frame
	// buffer row, which the last frame's scanout leaves sooner
	if (r_qogfast && n > QOG_VERTCACHE_FASTMAX)
		n = QOG_VERTCACHE_FASTMAX;
#endif
	while (n & (n - 1))
		n &= n - 1;
	if (n < 64)
		return;
#ifdef QOG_VID_SCANRACE
	VID_QogScanFree (lo, lo + n * sizeof(r_qogvert_t) + QOG_FX_RESERVE);	// QuakeOG (Agent AK): the last scanout may still read these rows (Agent AJ: and the Fast header after them)
#endif
	for (i = 0 ; i < n ; i++)
		((r_qogvert_t *)lo)[i].tag = 0xFFFF;
	verts = world->numvertexes < 0xFFFF ? world->numvertexes : 0xFFFF;
	r_qog_vertbase = (byte *)world->vertexes;
	r_qog_vertbytes = verts * sizeof(mvertex_t);
	r_qog_vertmask = n - 1;
	r_qog_vertcache = (r_qogvert_t *)lo;
#ifdef QOG_FRONT_FAST
	R_QOGFX->on = 0;	// QuakeOG (Agent AJ): R_QogFxSetup turns it on
#ifdef QOG_WORLDNODE_INT
	R_QOGFX->plon = 0;
#endif
#endif
}

void R_QogVertCacheEnd (void)
{
	r_qog_vertcache = NULL;
}

#ifdef QOG_VERTCACHE_CLIPDIST
/* the entry of a world vertex, emptied if it held another one, or NULL */
static r_qogvert_t *R_QogVertEntry (mvertex_t *pv)
{
	r_qogvert_t	*e;
	unsigned	ofs, n;

	if (!r_qog_vertcache || insubmodel)
		return NULL;
	ofs = (unsigned)((byte *)pv - r_qog_vertbase);
	if (ofs >= r_qog_vertbytes)
		return NULL;
	n = ofs / sizeof(mvertex_t);
	e = &r_qog_vertcache[n & r_qog_vertmask];
	if (e->tag != n)
	{
		e->tag = (unsigned short)n;
		e->ceilv = QOG_VC_NOPROJ;
		e->d[0].bits = e->d[1].bits = e->d[2].bits = e->d[3].bits = QOG_VC_NODIST;
	}
	return e;
}

/* R_ClipEdge's distance of a point to a clip plane, through the cache for
   world vertices and the view planes */
static float R_QogClipDist (mvertex_t *pv, clipplane_t *clip)
{
	r_qogvert_t	*e;
	unsigned	k = (unsigned)((byte *)clip - (byte *)view_clipplanes);
	float		d;

	if (k < sizeof(clipplane_t) * 4 && (e = R_QogVertEntry (pv)) != NULL)
	{
		k /= sizeof(clipplane_t);
		if (e->d[k].bits != QOG_VC_NODIST)
			return e->d[k].f;
		QOG_WC (QOG_WC_CLIPDOT);
		d = DotProduct (pv->position, clip->normal) - clip->dist;
		e->d[k].f = d;
		return d;
	}
	QOG_WC (QOG_WC_CLIPDOT);
	return DotProduct (pv->position, clip->normal) - clip->dist;
}
#endif

/* id's projection of one point in R_EmitEdge, through the cache for world
   vertices */
static void R_QogProject (mvertex_t *pv, float *pu, float *pvv, float *plzi, int *pceilv)
{
	vec3_t		local, transformed;
	float		*world;
	float		scale, lzi, u, v;
	int			ceilv;
	r_qogvert_t	*e = NULL;
	unsigned	ofs, n = 0;

#ifdef QOG_VERTCACHE_CLIPDIST
	e = R_QogVertEntry (pv);
	if (e && e->ceilv != QOG_VC_NOPROJ)
	{
		*pu = e->u;
		*pvv = e->v;
		*plzi = e->lzi;
		*pceilv = e->ceilv;
		return;
	}
	(void)ofs;
#else
	if (r_qog_vertcache && !insubmodel)
	{
		ofs = (unsigned)((byte *)pv - r_qog_vertbase);
		if (ofs < r_qog_vertbytes)
		{
			n = ofs / sizeof(mvertex_t);
			e = &r_qog_vertcache[n & r_qog_vertmask];
			if (e->tag == n)
			{
				*pu = e->u;
				*pvv = e->v;
				*plzi = e->lzi;
				*pceilv = e->ceilv;
				return;
			}
		}
	}
#endif

	QOG_WC (QOG_WC_PROJECT);	// QuakeOG (Agent Z)
	world = &pv->position[0];

// transform and project
	VectorSubtract (world, modelorg, local);
	TransformVector (local, transformed);

	if (QOG_NEARCLIPPED (transformed[2]))	// QuakeOG: QOG_FLOAT_EXACT
		transformed[2] = NEAR_CLIP;

	lzi = QOG_FRECIP (transformed[2]);	// QuakeOG: QOG_FLOAT_EXACT

// FIXME: build x/yscale into transform?
	scale = xscale * lzi;
	u = (xcenter + scale*transformed[0]);
	if (u < r_refdef.fvrectx_adj)
		u = r_refdef.fvrectx_adj;
	if (u > r_refdef.fvrectright_adj)
		u = r_refdef.fvrectright_adj;

	scale = yscale * lzi;
	v = (ycenter - scale*transformed[1]);
	if (v < r_refdef.fvrecty_adj)
		v = r_refdef.fvrecty_adj;
	if (v > r_refdef.fvrectbottom_adj)
		v = r_refdef.fvrectbottom_adj;

	ceilv = (int) QOG_CEILF(v);	// QuakeOG: QOG_FLOAT_EXACT

	if (e)
	{
#ifndef QOG_VERTCACHE_CLIPDIST	// else R_QogVertEntry set the tag
		e->tag = (unsigned short)n;
#endif
		e->ceilv = (short)ceilv;
		e->u = u;
		e->v = v;
		e->lzi = lzi;
	}
	*pu = u;
	*pvv = v;
	*plzi = lzi;
	*pceilv = ceilv;
}

#ifdef QOG_FRONT_FAST
/* QuakeOG (Agent AJ): the Fast front end, see r_qogfx_t above */

static int R_QogRound (float f)
{
	return f >= 0 ? (int)(f + 0.5f) : -(int)(0.5f - f);
}

/* the bit length of v (0 for 0), without a count leading zeros instruction */
static int R_QogBits (unsigned v)
{
	int	n = 0;

	if (v >= 1u << 16) { v >>= 16; n += 16; }
	if (v >= 1u << 8) { v >>= 8; n += 8; }
	if (v >= 1u << 4) { v >>= 4; n += 4; }
	if (v >= 1u << 2) { v >>= 2; n += 2; }
	if (v >= 1u << 1) { v >>= 1; n += 1; }
	return n + (int)v;
}

/* f * 1024 rounded, for |f| below 8192, else 2^26 */
static int R_QogQ10 (float f)
{
	unsigned	b, m;
	int			e, q;

	b = (unsigned)QOG_FI (f);
	e = (int)((b >> 23) & 0xFF) - 127;	// |f| = 1.m * 2^e
	if (e < -11)
		return 0;						// |f * 1024| below 0.5
	if (e >= 13)
		return 1 << 26;
	m = (b & 0x7FFFFF) | 0x800000;		// 1.m * 2^23
	q = (int)((m + (1u << (12 - e))) >> (13 - e));
	return (b & 0x80000000) ? -q : q;
}

/* a point less the origin: in 1/1024 units within 256 units on each axis
   (returns 1), else in 1/32 units (returns 0), or out of range (-1) */
static int R_QogLocal (mvertex_t *pv, int *l)
{
	r_qogfx_t	*fx = R_QOGFX;

	l[0] = R_QogQ10 (pv->position[0]) - fx->org[0];
	l[1] = R_QogQ10 (pv->position[1]) - fx->org[1];
	l[2] = R_QogQ10 (pv->position[2]) - fx->org[2];
	if ((unsigned)(l[0] + QOG_FX_LMAX) < 2 * QOG_FX_LMAX &&
		(unsigned)(l[1] + QOG_FX_LMAX) < 2 * QOG_FX_LMAX &&
		(unsigned)(l[2] + QOG_FX_LMAX) < 2 * QOG_FX_LMAX)
		return 1;
	l[0] = (l[0] + 16) >> 5;
	l[1] = (l[1] + 16) >> 5;
	l[2] = (l[2] + 16) >> 5;
	if ((unsigned)(l[0] + QOG_FX_LMAX) < 2 * QOG_FX_LMAX &&
		(unsigned)(l[1] + QOG_FX_LMAX) < 2 * QOG_FX_LMAX &&
		(unsigned)(l[2] + QOG_FX_LMAX) < 2 * QOG_FX_LMAX)
		return 0;
	return -1;
}

/*
The constants of the current view (R_RenderWorld) or brush entity
(R_DrawBEntitiesOnList, after R_RotateBmodel), from modelorg, vright, vup,
vpn and the view's projection values. Off when not in Fast mode, without
a vertex cache, or for values the integer ranges do not cover.
*/
void R_QogFxSetup (void)
{
	r_qogfx_t	*fx;
	vec3_t		r;
	float		xs, ys, gr[3][3];
	int			i, k, xl, yl;

#ifdef QOG_FX_REUSE
	int			same;
#endif

	if (!r_qog_vertcache)
		return;
	fx = R_QOGFX;
#ifdef QOG_FX_REUSE
	/*
	QuakeOG (Agent AQ): a brush entity that does not turn has the world's
	vectors (or those of the entity before), and everything below but the
	origin's part comes from the vectors, the view planes' normals (made
	from the vectors) and the view's projection values. When the vectors
	have the bits of the last complete setup, only the origin's part is
	made again, with the same statements, so the values are the same.
	*/
	same = fx->on && insubmodel && !memcmp (fx->vec, vright, sizeof(vec3_t)) &&
		   !memcmp (fx->vec + 3, vup, sizeof(vec3_t)) && !memcmp (fx->vec + 6, vpn, sizeof(vec3_t));
#endif
	fx->on = 0;
#ifdef QOG_WORLDNODE_INT
	// QuakeOG (Agent AJ): the world's view planes for R_QogCullNode, in
	// both render modes (its integer tests decide only with a margin)
#ifdef QOG_BMODEL_BBOXINT
	// QuakeOG (Agent AQ): the world's planes stay for the brush entities'
	// box tests (r_main.c R_QogBoxPlaneInt), only the world's setup sets them
	if (!insubmodel)
#endif
	fx->plon = 0;
	if (!insubmodel)
	{
		for (i=0 ; i<4 ; i++)
		{
			if (view_clipplanes[i].dist > 65536 || view_clipplanes[i].dist < -65536)
				break;
			fx->pl[i][0] = R_QogRound (view_clipplanes[i].normal[0] * 16384);
			fx->pl[i][1] = R_QogRound (view_clipplanes[i].normal[1] * 16384);
			fx->pl[i][2] = R_QogRound (view_clipplanes[i].normal[2] * 16384);
			fx->pl[i][3] = R_QogRound (view_clipplanes[i].dist * 16384);
		}
		fx->plon = i == 4;
	}
#endif
#ifndef QOG_FRONT_FAST_ALWAYS	// measurement: the integer front end with the exact spans
	if (!r_qogfast)
		return;
#endif
#ifdef QOG_EDGES_IN_ZBUFFER
	// brush entities clip their polygons in buffers at the start of the
	// z-buffer (r_bsp.c), which must not reach the header
	if (insubmodel && (byte *)(fx + 1) > r_ztemp_base &&
		(byte *)fx < r_ztemp_base + MAX_BMODEL_VERTS * sizeof(mvertex_t) + MAX_BMODEL_EDGES * sizeof(bedge_t))
		return;
#endif
	for (i=0 ; i<3 ; i++)
	{
		if (modelorg[i] > 8000 || modelorg[i] < -8000)
			return;
		fx->org[i] = R_QogRound (modelorg[i] * 1024);
		r[i] = fx->org[i] * (1.0f / 1024) - modelorg[i];
	}
#ifdef QOG_FX_REUSE
	if (same)
	{
		fx->cx = R_QogRound (DotProduct (r, vright) * fx->xs * 1024);
		fx->cy = R_QogRound (DotProduct (r, vup) * fx->ys * 1024);
		fx->cz = R_QogRound (DotProduct (r, vpn) * (8192.0f * 1024));
		for (i=0 ; i<4 ; i++)
			fx->cn[i][3] = R_QogRound (DotProduct (r, view_clipplanes[i].normal) * 262144);
		fx->on = 1;
		return;
	}
#endif
	// the largest power of two that keeps xscale * 2^xl within 8192
	if (xscale > 8192 || yscale > 8192 || xscale < 1.0f / 512 || yscale < 1.0f / 512)
		return;
	for (xl = 0, xs = xscale ; xl < 21 && xs * 2 <= 8192 ; xl++)
		xs *= 2;
	for (yl = 0, ys = yscale ; yl < 21 && ys * 2 <= 8192 ; yl++)
		ys *= 2;
	for (i=0 ; i<3 ; i++)
	{
		fx->mx[i] = R_QogRound (vright[i] * xs);
		fx->my[i] = R_QogRound (vup[i] * ys);
		fx->mz[i] = R_QogRound (vpn[i] * 8192);
	}
	fx->cx = R_QogRound (DotProduct (r, vright) * xs * 1024);
	fx->cy = R_QogRound (DotProduct (r, vup) * ys * 1024);
	fx->cz = R_QogRound (DotProduct (r, vpn) * (8192.0f * 1024));
	fx->xsh = 21 - xl;
	fx->ysh = 21 - yl;
	fx->xlim = 1 << (31 - fx->xsh);
	fx->ylim = 1 << (31 - fx->ysh);
	fx->xc = R_QogRound (xcenter * 256);
	fx->yc = R_QogRound (ycenter * 256);
	fx->umin = R_QogRound (r_refdef.fvrectx_adj * 256);
	fx->umax = R_QogRound (r_refdef.fvrectright_adj * 256);
	fx->vmin = R_QogRound (r_refdef.fvrecty_adj * 256);
	fx->vmax = R_QogRound (r_refdef.fvrectbottom_adj * 256);
	// the view planes pass through the eye (R_TransformFrustum), so a
	// point's distance is normal . (position - modelorg), in 1/262144
	for (i=0 ; i<4 ; i++)
	{
		fx->cn[i][0] = R_QogRound (view_clipplanes[i].normal[0] * 8192);
		fx->cn[i][1] = R_QogRound (view_clipplanes[i].normal[1] * 8192);
		fx->cn[i][2] = R_QogRound (view_clipplanes[i].normal[2] * 8192);
		fx->cn[i][3] = R_QogRound (DotProduct (r, view_clipplanes[i].normal) * 262144);
	}
	// the face gradients' rows: d_zistepu = distinv vright . n / xscale,
	// d_zistepv = -distinv vup . n / yscale and d_ziorigin = distinv
	// (vpn - xcenter vright / xscale + ycenter vup / yscale) . n
	for (i=0 ; i<3 ; i++)
	{
		gr[0][i] = vright[i] * xscaleinv;
		gr[1][i] = -vup[i] * yscaleinv;
		gr[2][i] = vpn[i] - xcenter * xscaleinv * vright[i] + ycenter * yscaleinv * vup[i];
	}
	for (k=0 ; k<3 ; k++)
	{
		float	m = 0, sc = 1;

		for (i=0 ; i<3 ; i++)
			if (gr[k][i] > m || -gr[k][i] > m)
				m = gr[k][i] > 0 ? gr[k][i] : -gr[k][i];
		if (m > 65536 || m < 1.0f / 65536)
			return;
		// 2^gs with m 2^gs in [32768, 65536), the dot products stay below 2^30.8
		while (m * sc >= 65536)
			sc *= 0.5f;
		while (m * sc < 32768)
			sc *= 2;
		for (i=0 ; i<3 ; i++)
			fx->gr[k][i] = R_QogRound (gr[k][i] * sc);
		fx->gk[k] = 1.0f / (sc * 16384);
	}
#ifdef QOG_FX_REUSE
	memcpy (fx->vec, vright, sizeof(vec3_t));
	memcpy (fx->vec + 3, vup, sizeof(vec3_t));
	memcpy (fx->vec + 6, vpn, sizeof(vec3_t));
	fx->xs = xs;
	fx->ys = ys;
#endif
	fx->on = 1;
}

#ifdef QOG_BMODEL_BBOXINT
/* the world's view planes in integers for the brush entities, or NULL */
const int *R_QogFxWorldPlanes (void)
{
	if (!r_qog_vertcache || !R_QOGFX->plon)
		return NULL;
	return &R_QOGFX->pl[0][0];
}
#endif

#ifdef QOG_WORLDNODE_INT
/* the world's view planes in integers for R_QogCullNode, or NULL */
const int *R_QogFxPlanes (void)
{
	if (!r_qog_vertcache || insubmodel || !R_QOGFX->plon)
		return NULL;
	return &R_QOGFX->pl[0][0];
}
#endif

/*
id's float projection, with the result in the Fast front end's units:
o[0] u and o[1] v in 1/256 pixel, o[2] z in 1/65536 units, o[3] ceil (v).
Only for points out of the integer range, so not inlined.
*/
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void R_QogProjectFloatFx (mvertex_t *pv, int *o)
{
	vec3_t	local, transformed;
	float	lzi, u, v;

	VectorSubtract (pv->position, modelorg, local);
	TransformVector (local, transformed);
	if (QOG_NEARCLIPPED (transformed[2]))
		transformed[2] = NEAR_CLIP;
	lzi = QOG_FRECIP (transformed[2]);
	u = (xcenter + xscale * lzi * transformed[0]);
	if (u < r_refdef.fvrectx_adj)
		u = r_refdef.fvrectx_adj;
	if (u > r_refdef.fvrectright_adj)
		u = r_refdef.fvrectright_adj;
	v = (ycenter - yscale * lzi * transformed[1]);
	if (v < r_refdef.fvrecty_adj)
		v = r_refdef.fvrecty_adj;
	if (v > r_refdef.fvrectbottom_adj)
		v = r_refdef.fvrectbottom_adj;
	o[0] = R_QogRound (u * 256);
	o[1] = R_QogRound (v * 256);
	o[2] = transformed[2] < 30000 ? R_QogRound (transformed[2] * 65536) : 0x7FFFFFFE;
	if (o[2] < QOG_FX_NEARZ)
		o[2] = QOG_FX_NEARZ;
	o[3] = (o[1] + 255) >> 8;
}

/*
A point in integers. Returns 0 for id's float projection instead: out of
the integer range, at or behind id's near clip (where id clamps z and u, v
depend on the rounding of everything) or far past the view's sides (only
points no clip has seen, whose u or v id clamps).
*/
static int R_QogProjectFx (mvertex_t *pv, int *o)
{
	r_qogfx_t	*fx = R_QOGFX;
	int			l[3], x, y, z, zn, r, u, v, near, cx, cy, cz;

	near = R_QogLocal (pv, l);
	if (near < 0)
		return 0;
	cx = fx->cx;
	cy = fx->cy;
	cz = fx->cz;
	if (!near)
	{
		cx >>= 5;
		cy >>= 5;
		cz >>= 5;
	}
	// z in 1/2^23 units near, 1/2^18 far, x and y in the same ratio
	z = fx->mz[0] * l[0] + fx->mz[1] * l[1] + fx->mz[2] * l[2] + cz;
	if (z <= (near ? QOG_FX_NEARZ * 128 : QOG_FX_NEARZ * 4))
		return 0;
	x = fx->mx[0] * l[0] + fx->mx[1] * l[1] + fx->mx[2] * l[2] + cx;
	y = fx->my[0] * l[0] + fx->my[1] * l[1] + fx->my[2] * l[2] + cy;

	// z to 14 bits, so the quotients below are 1/256 pixel
	zn = z;
	r = R_QogBits ((unsigned)z) - 14;
	if (r > 0)
	{
		zn >>= r;
		x >>= r;
		y >>= r;
	}
	if (x >= fx->xlim || x <= -fx->xlim || y >= fx->ylim || y <= -fx->ylim)
		return 0;

	u = fx->xc + x * (1 << fx->xsh) / zn;
	if (u < fx->umin)
		u = fx->umin;
	if (u > fx->umax)
		u = fx->umax;

	v = fx->yc - y * (1 << fx->ysh) / zn;
	if (v < fx->vmin)
		v = fx->vmin;
	if (v > fx->vmax)
		v = fx->vmax;

	o[0] = u;
	o[1] = v;
	o[2] = near ? z >> 7 : z >> 2;	// 1/65536 units
	o[3] = (v + 255) >> 8;	// ceil (v)
	return 1;
}

/* a point through the vertex cache (world vertices) */
static void R_QogProjectFast (mvertex_t *pv, int *o)
{
	r_qogvert_t	*e = R_QogVertEntry (pv);

	if (e && e->ceilv != QOG_VC_NOPROJ)
	{
		// the floats' bits hold the ints in Fast mode
		o[0] = QOG_FI (e->u);
		o[1] = QOG_FI (e->v);
		o[2] = QOG_FI (e->lzi);
		o[3] = e->ceilv;
		return;
	}
	QOG_WC (QOG_WC_PROJECT);
	if (!R_QogProjectFx (pv, o))
	{
		QOG_AQ (AQ_FPROJFLT, 1);
		R_QogProjectFloatFx (pv, o);
	}
#ifdef QOG_FRONT_FAST_CHECK
	// measurement (PC): the integer point against id's float one
	{
		static int	n, maxdu, maxdv, ceildiff, zbad;
		static double	sdu, sdv;
		int			f[4], du, dv;

		R_QogProjectFloatFx (pv, f);
		du = o[0] - f[0];
		dv = o[1] - f[1];
		if (du < 0) du = -du;
		if (dv < 0) dv = -dv;
		sdu += du; sdv += dv;
		if (du > maxdu) maxdu = du;
		if (dv > maxdv) maxdv = dv;
		if (o[3] != f[3]) ceildiff++;
		if (o[2] > f[2] + f[2] / 1000 + 2 || o[2] < f[2] - f[2] / 1000 - 2) zbad++;
		if (++n % 200000 == 0)
			Con_Printf ("fxcheck %d points, max du %d dv %d (1/256 px), ceil v differs %d, z off by 0.1%% %d mean du %.2f dv %.2f\n", n, maxdu, maxdv, ceildiff, zbad, sdu / n, sdv / n);
	}
#endif
	if (e)
	{
		QOG_FI (e->u) = o[0];
		QOG_FI (e->v) = o[1];
		QOG_FI (e->lzi) = o[2];
		e->ceilv = (short)o[3];
	}
}

/* a float variable's bits as an int, and back (the Fast state in id's globals) */
static int R_QogGetI (float *f)
{
	return QOG_FI (*f);
}

static void R_QogSetI (float *f, int i)
{
	QOG_FI (*f) = i;
}

/* the nearest 1/z of a face from the Fast key (0x7FFFFFFF - z), as id's float */
static float R_QogNearzi (void)
{
	int	key = R_QogGetI (&r_nearzi);

	if (key <= 0)
		return 0;
	return 65536.0f / (float)(0x7FFFFFFF - key);
}

/* f * 16384 rounded, for |f| at most 1 (a plane normal's component) */
static int R_QogQ14 (float f)
{
	unsigned	b, m;
	int			e, q;

	b = (unsigned)QOG_FI (f);
	e = (int)((b >> 23) & 0xFF) - 127;	// |f| = 1.m * 2^e
	if (e < -15)
		return 0;
	if (e > 0)
		e = 0;							// not a unit normal: 1
	m = (b & 0x7FFFFF) | 0x800000;
	q = (int)((m + (1u << (8 - e))) >> (9 - e));
	return (b & 0x80000000) ? -q : q;
}

#ifdef QOG_FGRAD_AXIAL
/*
QuakeOG (Agent AQ): the same gradients with fewer float calls and the same
bits. On an axial plane (one normal component +-1, two +-0) id's
DotProduct (modelorg, normal) is exactly modelorg[k] normal[k] when that is
not zero, else a zero that is -0 only when all three products are -0
(round to nearest adds zeros that way), so its bits need no float call
(R_QogAxialDotF, any modelorg component not finite or denormal takes id's
arithmetic). gk is a power of two (R_QogFxSetup), so distinv gk is exact
and g (distinv gk) rounds as (g gk) distinv: g gk is made by adding to the
exponent of (float) g (R_QogScale2), one multiply instead of two.
*/
static int R_QogAxialDotF (const float *v, const float *n, float *out)
{
	const r_qogaint_t	*b = (const r_qogaint_t *)v, *nb = (const r_qogaint_t *)n;
	unsigned			e, q;
	int					i, k;

	if (((unsigned)nb[0] & 0x7FFFFFFF) == 0x3F800000 && !(((unsigned)nb[1] | (unsigned)nb[2]) & 0x7FFFFFFF))
		k = 0;
	else if (((unsigned)nb[1] & 0x7FFFFFFF) == 0x3F800000 && !(((unsigned)nb[0] | (unsigned)nb[2]) & 0x7FFFFFFF))
		k = 1;
	else if (((unsigned)nb[2] & 0x7FFFFFFF) == 0x3F800000 && !(((unsigned)nb[0] | (unsigned)nb[1]) & 0x7FFFFFFF))
		k = 2;
	else
		return 0;
	for (i=0 ; i<3 ; i++)
	{
		e = (unsigned)b[i] & 0x7F800000;
		if (e == 0x7F800000 || (e == 0 && ((unsigned)b[i] & 0x007FFFFF)))
			return 0;	// not finite, or denormal
	}
	if ((unsigned)b[k] & 0x7FFFFFFF)
		q = (unsigned)b[k] ^ ((unsigned)nb[k] & 0x80000000);
	else
		q = ((unsigned)(b[0] ^ nb[0]) & (unsigned)(b[1] ^ nb[1]) & (unsigned)(b[2] ^ nb[2])) & 0x80000000;
	QOG_FI (*out) = (int)q;
	return 1;
}

/* f times p, p a power of two, for results that stay normal floats */
static float R_QogScale2 (float f, float p)
{
	if (QOG_FI (f) & 0x7FFFFFFF)
		QOG_FI (f) += (QOG_FI (p) & 0x7F800000) - (127 << 23);
	return f;
}
#endif

/*
A face's 1/z gradients in Fast mode: the plane normal in 1/16384 against
rows made once per view or entity (R_QogFxSetup), so the face costs the
distance's reciprocal, three int dot products and six float multiplies
instead of id's 30 float operations.
*/
static void R_QogFaceGradFast (mplane_t *pplane)
{
	r_qogfx_t	*fx = R_QOGFX;
	int			n0, n1, n2, g;
	float		distinv;

	QOG_AQ (AQ_FGRAD, 1);
#ifdef QOG_FGRAD_AXIAL
	{
		float	dot;

		if (!R_QogAxialDotF (modelorg, pplane->normal, &dot))
			dot = DotProduct (modelorg, pplane->normal);
		distinv = QOG_FRECIP (pplane->dist - dot);
	}
#else
	distinv = QOG_FRECIP (pplane->dist - DotProduct (modelorg, pplane->normal));
#endif
	n0 = R_QogQ14 (pplane->normal[0]);
	n1 = R_QogQ14 (pplane->normal[1]);
	n2 = R_QogQ14 (pplane->normal[2]);
#ifdef QOG_FGRAD_AXIAL
	g = fx->gr[0][0] * n0 + fx->gr[0][1] * n1 + fx->gr[0][2] * n2;
	surface_p->d_zistepu = R_QogScale2 ((float)g, fx->gk[0]) * distinv;
	g = fx->gr[1][0] * n0 + fx->gr[1][1] * n1 + fx->gr[1][2] * n2;
	surface_p->d_zistepv = R_QogScale2 ((float)g, fx->gk[1]) * distinv;
	g = fx->gr[2][0] * n0 + fx->gr[2][1] * n1 + fx->gr[2][2] * n2;
	surface_p->d_ziorigin = R_QogScale2 ((float)g, fx->gk[2]) * distinv;
#else
	g = fx->gr[0][0] * n0 + fx->gr[0][1] * n1 + fx->gr[0][2] * n2;
	surface_p->d_zistepu = (float)g * (distinv * fx->gk[0]);
	g = fx->gr[1][0] * n0 + fx->gr[1][1] * n1 + fx->gr[1][2] * n2;
	surface_p->d_zistepv = (float)g * (distinv * fx->gk[1]);
	g = fx->gr[2][0] * n0 + fx->gr[2][1] * n1 + fx->gr[2][2] * n2;
	surface_p->d_ziorigin = (float)g * (distinv * fx->gk[2]);
#endif
#ifdef QOG_FGRAD_CHECK
	// PC check build: the values of the code above against AJ's statements
	{
		static unsigned	n, bad;
		float			di = QOG_FRECIP (pplane->dist - DotProduct (modelorg, pplane->normal)), v[3];

		v[0] = (float)(fx->gr[0][0] * n0 + fx->gr[0][1] * n1 + fx->gr[0][2] * n2) * (di * fx->gk[0]);
		v[1] = (float)(fx->gr[1][0] * n0 + fx->gr[1][1] * n1 + fx->gr[1][2] * n2) * (di * fx->gk[1]);
		v[2] = (float)(fx->gr[2][0] * n0 + fx->gr[2][1] * n1 + fx->gr[2][2] * n2) * (di * fx->gk[2]);
		n++;
		if (QOG_FI (v[0]) != QOG_FI (surface_p->d_zistepu) || QOG_FI (v[1]) != QOG_FI (surface_p->d_zistepv) ||
			QOG_FI (v[2]) != QOG_FI (surface_p->d_ziorigin))
			bad++;
		if ((n & 0x3FFF) == 0)
			Sys_Printf ("qog_fgradcheck %u faces, %u differ\n", n, bad);
	}
#endif
}

/*
R_EmitEdge in Fast mode: the same edges, with the points and the stepping
in integers. r_u1, r_v1 and r_lzi1 hold u, v (1/256 pixel) and z as ints,
r_nearzi and edge->nearzi the key 0x7FFFFFFF - z.
*/
static void R_QogEmitEdgeFast (mvertex_t *pv0, mvertex_t *pv1)
{
	edge_t	*edge, *pcheck;
	int		u_check, v, v2, side, key, zmin;
	int		p0[4], p1[4], ua, va, du, dv, a, rem, ustep, u;

	QOG_WC (QOG_WC_EMITEDGE);
	if (r_lastvertvalid)
	{
		p0[0] = R_QogGetI (&r_u1);
		p0[1] = R_QogGetI (&r_v1);
		p0[2] = R_QogGetI (&r_lzi1);
		p0[3] = r_ceilv1;
	}
	else
		R_QogProjectFast (pv0, p0);
	R_QogProjectFast (pv1, p1);
	R_QogSetI (&r_u1, p1[0]);
	R_QogSetI (&r_v1, p1[1]);
	R_QogSetI (&r_lzi1, p1[2]);

	zmin = p0[2] < p1[2] ? p0[2] : p1[2];
	key = 0x7FFFFFFF - zmin;
	if (key > R_QogGetI (&r_nearzi))	// for mipmap finding
		R_QogSetI (&r_nearzi, key);

// for right edges, all we want is the effect on 1/z
	if (r_nearzionly)
		return;

	r_emitted = 1;
	r_ceilv1 = p1[3];

// create the edge
	if (p0[3] == r_ceilv1)
	{
	// we cache unclipped horizontal edges as fully clipped
		if (cacheoffset != 0x7FFFFFFF)
		{
			cacheoffset = FULLY_CLIPPED_CACHED |
					(r_framecount & FRAMECOUNT_MASK);
		}
		return;		// horizontal edge
	}

	side = p0[3] > r_ceilv1;

	edge = edge_p++;
	edge->owner = EDGE_OWNER (r_pedge);
	R_QogSetI (&edge->nearzi, key);

	if (side == 0)
	{
	// trailing edge (go from p1 to p2)
		v = p0[3];
		v2 = r_ceilv1 - 1;
		edge->surfs[0] = surface_p - surfaces;
		edge->surfs[1] = 0;
		ua = p0[0];
		va = p0[1];
		du = p1[0] - p0[0];
		dv = p1[1] - p0[1];
	}
	else
	{
	// leading edge (go from p2 to p1)
		v2 = p0[3] - 1;
		v = r_ceilv1;
		edge->surfs[0] = 0;
		edge->surfs[1] = surface_p - surfaces;
		ua = p1[0];
		va = p1[1];
		du = p0[0] - p1[0];
		dv = p0[1] - p1[1];
	}

	// dv > 0 (the ceilings differ). The slope in 20.12 and its 12.20 step,
	// limited to 1024 pixels a line (such an edge covers one line, which is
	// never stepped)
	if (du >= dv * 1024 || -du >= dv * 1024)
	{
		a = du > 0 ? (1 << 22) : -(1 << 22);
		ustep = a * 256;
	}
	else
	{
		a = du * 4096 / dv;
		rem = du * 4096 - a * dv;
		ustep = a * 256 + rem * 256 / dv;
	}
	u = ua * 4096 + ((v << 8) - va) * a;	// at the first line, 12.20 (1/256 times 1/4096)

	edge->u_step = ustep;
	edge->u = u + 0xFFFFF;

// we need to do this to avoid stepping off the edges if a very nearly
// horizontal edge is less than epsilon above a scan, and numeric error causes
// it to incorrectly extend to the scan, and the extension of the line goes off
// the edge of the screen
	if (edge->u < r_refdef.vrect_x_adj_shift20)
		edge->u = r_refdef.vrect_x_adj_shift20;
	if (edge->u > r_refdef.vrectright_adj_shift20)
		edge->u = r_refdef.vrectright_adj_shift20;

//
// sort the edge in normally
//
	u_check = edge->u;
	if (edge->surfs[0])
		u_check++;	// sort trailers after leaders

	if (!newedges[v] || EDGE_PTR(newedges[v])->u >= u_check)
	{
		edge->next = newedges[v];
		newedges[v] = EDGE_REF(edge);
	}
	else
	{
		pcheck = EDGE_PTR(newedges[v]);
		while (pcheck->next && EDGE_PTR(pcheck->next)->u < u_check)
			pcheck = EDGE_PTR(pcheck->next);
		edge->next = pcheck->next;
		pcheck->next = EDGE_REF(edge);
	}

	edge->nextremove = removeedges[v2];
	removeedges[v2] = EDGE_REF(edge);
}

/*
A point's distance to view plane k in Fast mode, in 1/262144 units, from
the same integer position as the projection (through the vertex cache's
distance slots for world vertices, which then hold ints).
*/
static int R_QogClipDistFx (mvertex_t *pv, int k)
{
	r_qogfx_t	*fx = R_QOGFX;
	r_qogvert_t	*e = R_QogVertEntry (pv);
	int			l0, l1, l2, d;

	if (e && e->d[k].bits != QOG_VC_NODIST)
		return (int)e->d[k].bits;
	QOG_WC (QOG_WC_CLIPDOT);
	// in 1/32 units, so every point's distance has the same scale
	l0 = (R_QogQ10 (pv->position[0]) - fx->org[0] + 16) >> 5;
	l1 = (R_QogQ10 (pv->position[1]) - fx->org[1] + 16) >> 5;
	l2 = (R_QogQ10 (pv->position[2]) - fx->org[2] + 16) >> 5;
	// far points (past 4096 units on an axis) are pulled in, only their
	// distance's sign matters for them
	if (l0 >= QOG_FX_LMAX) l0 = QOG_FX_LMAX - 1;
	if (l0 <= -QOG_FX_LMAX) l0 = 1 - QOG_FX_LMAX;
	if (l1 >= QOG_FX_LMAX) l1 = QOG_FX_LMAX - 1;
	if (l1 <= -QOG_FX_LMAX) l1 = 1 - QOG_FX_LMAX;
	if (l2 >= QOG_FX_LMAX) l2 = QOG_FX_LMAX - 1;
	if (l2 <= -QOG_FX_LMAX) l2 = 1 - QOG_FX_LMAX;
	d = fx->cn[k][0] * l0 + fx->cn[k][1] * l1 + fx->cn[k][2] * l2 + fx->cn[k][3];
	if (e)
	{
		e->d[k].bits = (unsigned)d;	// never QOG_VC_NODIST, |d| is below 2^30.8
#ifdef QOG_CLIPDIST_FILL4
		/*
		QuakeOG (Agent AQ): the point's position is already in 1/32 units,
		so its distances to the other view planes cost three multiplies
		each. A world vertex is asked against about 3.4 planes a view on
		average (several faces share it), so the other unknown slots are
		filled now with the same statement: the same values, later asked
		for, without three more conversions of its position.
		*/
		{
			int		j;

			QOG_WC (QOG_WC_CLIPFILL);
			for (j=0 ; j<4 ; j++)
				if (e->d[j].bits == QOG_VC_NODIST)
					e->d[j].bits = (unsigned)(fx->cn[j][0] * l0 + fx->cn[j][1] * l1 + fx->cn[j][2] * l2 + fx->cn[j][3]);
		}
#endif
	}
	return d;
}

/*
R_ClipEdge in Fast mode: the plane sides from the integer distances, the
clipped point by id's float interpolation with their ratio.
*/
static void R_QogClipEdgeFast (mvertex_t *pv0, mvertex_t *pv1, clipplane_t *clip)
{
	int			d0, d1, k;
	float		f;
	mvertex_t	clipvert;

	QOG_WC (QOG_WC_CLIPEDGE);
	if (clip)
	{
		do
		{
			k = clip - view_clipplanes;
			d0 = R_QogClipDistFx (pv0, k);
			d1 = R_QogClipDistFx (pv1, k);

			if (d0 >= 0)
			{
			// point 0 is unclipped
				if (d1 >= 0)
				{
				// both points are unclipped
					continue;
				}

			// only point 1 is clipped

			// we don't cache clipped edges
				cacheoffset = 0x7FFFFFFF;

				QOG_AQ (AQ_FCLIPVERT, 1);
				f = (float)d0 / ((float)d0 - (float)d1);
				clipvert.position[0] = pv0->position[0] +
						f * (pv1->position[0] - pv0->position[0]);
				clipvert.position[1] = pv0->position[1] +
						f * (pv1->position[1] - pv0->position[1]);
				clipvert.position[2] = pv0->position[2] +
						f * (pv1->position[2] - pv0->position[2]);

				if (clip->leftedge)
				{
					r_leftclipped = true;
					r_leftexit = clipvert;
				}
				else if (clip->rightedge)
				{
					r_rightclipped = true;
					r_rightexit = clipvert;
				}

				R_QogClipEdgeFast (pv0, &clipvert, clip->next);
				return;
			}
			else
			{
			// point 0 is clipped
				if (d1 < 0)
				{
				// both points are clipped
				// we do cache fully clipped edges
					if (!r_leftclipped)
						cacheoffset = FULLY_CLIPPED_CACHED |
								(r_framecount & FRAMECOUNT_MASK);
					return;
				}

			// only point 0 is clipped
				r_lastvertvalid = false;

			// we don't cache partially clipped edges
				cacheoffset = 0x7FFFFFFF;

				QOG_AQ (AQ_FCLIPVERT, 1);
				f = (float)d0 / ((float)d0 - (float)d1);
				clipvert.position[0] = pv0->position[0] +
						f * (pv1->position[0] - pv0->position[0]);
				clipvert.position[1] = pv0->position[1] +
						f * (pv1->position[1] - pv0->position[1]);
				clipvert.position[2] = pv0->position[2] +
						f * (pv1->position[2] - pv0->position[2]);

				if (clip->leftedge)
				{
					r_leftclipped = true;
					r_leftenter = clipvert;
				}
				else if (clip->rightedge)
				{
					r_rightclipped = true;
					r_rightenter = clipvert;
				}

				R_QogClipEdgeFast (&clipvert, pv1, clip->next);
				return;
			}
		} while ((clip = clip->next) != NULL);
	}

// add the edge
	R_QogEmitEdgeFast (pv0, pv1);
}
#endif	// QOG_FRONT_FAST
#endif


#if	!id386

/*
================
R_EmitEdge
================
*/
void R_EmitEdge (mvertex_t *pv0, mvertex_t *pv1)
{
	edge_t	*edge, *pcheck;
	int		u_check;
	float	u, u_step;
	vec3_t	local, transformed;
	float	*world;
	int		v, v2, ceilv0;
	float	scale, lzi0, u0, v0;
	int		side;
#ifdef QOG_VERTCACHE
	int		ceilv1;
#endif

#ifdef QOG_FRONT_FAST
	if (R_QOGFX_ON ())
	{
		R_QogEmitEdgeFast (pv0, pv1);	// QuakeOG (Agent AJ)
		return;
	}
#endif
	QOG_WC (QOG_WC_EMITEDGE);	// QuakeOG (Agent Z)
	if (r_lastvertvalid)
	{
		u0 = r_u1;
		v0 = r_v1;
		lzi0 = r_lzi1;
		ceilv0 = r_ceilv1;
	}
#ifdef QOG_VERTCACHE
	else
		R_QogProject (pv0, &u0, &v0, &lzi0, &ceilv0);	// QuakeOG (Agent Z)

	R_QogProject (pv1, &r_u1, &r_v1, &r_lzi1, &ceilv1);
#else
	else
	{
		QOG_WC (QOG_WC_PROJECT);	// QuakeOG (Agent Z)
		world = &pv0->position[0];
	
	// transform and project
		VectorSubtract (world, modelorg, local);
		TransformVector (local, transformed);
	
		if (QOG_NEARCLIPPED (transformed[2]))	// QuakeOG: QOG_FLOAT_EXACT
			transformed[2] = NEAR_CLIP;
	
		lzi0 = QOG_FRECIP (transformed[2]);	// QuakeOG: QOG_FLOAT_EXACT
	
	// FIXME: build x/yscale into transform?
		scale = xscale * lzi0;
		u0 = (xcenter + scale*transformed[0]);
		if (u0 < r_refdef.fvrectx_adj)
			u0 = r_refdef.fvrectx_adj;
		if (u0 > r_refdef.fvrectright_adj)
			u0 = r_refdef.fvrectright_adj;
	
		scale = yscale * lzi0;
		v0 = (ycenter - scale*transformed[1]);
		if (v0 < r_refdef.fvrecty_adj)
			v0 = r_refdef.fvrecty_adj;
		if (v0 > r_refdef.fvrectbottom_adj)
			v0 = r_refdef.fvrectbottom_adj;
	
		ceilv0 = (int) QOG_CEILF(v0);	// QuakeOG: QOG_FLOAT_EXACT
	}

	QOG_WC (QOG_WC_PROJECT);	// QuakeOG (Agent Z)
	world = &pv1->position[0];

// transform and project
	VectorSubtract (world, modelorg, local);
	TransformVector (local, transformed);

	if (QOG_NEARCLIPPED (transformed[2]))	// QuakeOG: QOG_FLOAT_EXACT
		transformed[2] = NEAR_CLIP;

	r_lzi1 = QOG_FRECIP (transformed[2]);	// QuakeOG: QOG_FLOAT_EXACT

	scale = xscale * r_lzi1;
	r_u1 = (xcenter + scale*transformed[0]);
	if (r_u1 < r_refdef.fvrectx_adj)
		r_u1 = r_refdef.fvrectx_adj;
	if (r_u1 > r_refdef.fvrectright_adj)
		r_u1 = r_refdef.fvrectright_adj;

	scale = yscale * r_lzi1;
	r_v1 = (ycenter - scale*transformed[1]);
	if (r_v1 < r_refdef.fvrecty_adj)
		r_v1 = r_refdef.fvrecty_adj;
	if (r_v1 > r_refdef.fvrectbottom_adj)
		r_v1 = r_refdef.fvrectbottom_adj;
#endif

	if (r_lzi1 > lzi0)
		lzi0 = r_lzi1;

	if (lzi0 > r_nearzi)	// for mipmap finding
		r_nearzi = lzi0;

// for right edges, all we want is the effect on 1/z
	if (r_nearzionly)
		return;

	r_emitted = 1;

#ifdef QOG_VERTCACHE
	r_ceilv1 = ceilv1;	// QuakeOG (Agent Z): ceil(r_v1), from R_QogProject
#else
	r_ceilv1 = (int) QOG_CEILF(r_v1);	// QuakeOG: QOG_FLOAT_EXACT
#endif


// create the edge
	if (ceilv0 == r_ceilv1)
	{
	// we cache unclipped horizontal edges as fully clipped
		if (cacheoffset != 0x7FFFFFFF)
		{
			cacheoffset = FULLY_CLIPPED_CACHED |
					(r_framecount & FRAMECOUNT_MASK);
		}

		return;		// horizontal edge
	}

	side = ceilv0 > r_ceilv1;

	edge = edge_p++;

	edge->owner = EDGE_OWNER (r_pedge);	// QuakeOG

	edge->nearzi = lzi0;

	if (side == 0)
	{
	// trailing edge (go from p1 to p2)
		v = ceilv0;
		v2 = r_ceilv1 - 1;

		edge->surfs[0] = surface_p - surfaces;
		edge->surfs[1] = 0;

		u_step = ((r_u1 - u0) / (r_v1 - v0));
		u = u0 + ((float)v - v0) * u_step;
	}
	else
	{
	// leading edge (go from p2 to p1)
		v2 = ceilv0 - 1;
		v = r_ceilv1;

		edge->surfs[0] = 0;
		edge->surfs[1] = surface_p - surfaces;

		u_step = ((u0 - r_u1) / (v0 - r_v1));
		u = r_u1 + ((float)v - r_v1) * u_step;
	}

	edge->u_step = u_step*0x100000;
	edge->u = u*0x100000 + 0xFFFFF;

// we need to do this to avoid stepping off the edges if a very nearly
// horizontal edge is less than epsilon above a scan, and numeric error causes
// it to incorrectly extend to the scan, and the extension of the line goes off
// the edge of the screen
// FIXME: is this actually needed?
	if (edge->u < r_refdef.vrect_x_adj_shift20)
		edge->u = r_refdef.vrect_x_adj_shift20;
	if (edge->u > r_refdef.vrectright_adj_shift20)
		edge->u = r_refdef.vrectright_adj_shift20;

//
// sort the edge in normally
//
	u_check = edge->u;
	if (edge->surfs[0])
		u_check++;	// sort trailers after leaders

// QuakeOG: links through EDGE_PTR/EDGE_REF (r_shared.h)
	if (!newedges[v] || EDGE_PTR(newedges[v])->u >= u_check)
	{
		edge->next = newedges[v];
		newedges[v] = EDGE_REF(edge);
	}
	else
	{
		pcheck = EDGE_PTR(newedges[v]);
		while (pcheck->next && EDGE_PTR(pcheck->next)->u < u_check)
			pcheck = EDGE_PTR(pcheck->next);
		edge->next = pcheck->next;
		pcheck->next = EDGE_REF(edge);
	}

	edge->nextremove = removeedges[v2];
	removeedges[v2] = EDGE_REF(edge);
}


/*
================
R_ClipEdge
================
*/
void R_ClipEdge (mvertex_t *pv0, mvertex_t *pv1, clipplane_t *clip)
{
	float		d0, d1, f;
	mvertex_t	clipvert;

#ifdef QOG_FRONT_FAST
	if (R_QOGFX_ON ())
	{
		R_QogClipEdgeFast (pv0, pv1, clip);	// QuakeOG (Agent AJ)
		return;
	}
#endif
	QOG_WC (QOG_WC_CLIPEDGE);	// QuakeOG (Agent Z)
	if (clip)
	{
		do
		{
#ifdef QOG_VERTCACHE_CLIPDIST
			d0 = R_QogClipDist (pv0, clip);	// QuakeOG (Agent Z)
			d1 = R_QogClipDist (pv1, clip);
#else
			QOG_WC (QOG_WC_CLIPDOT);	// QuakeOG (Agent Z)
			QOG_WC (QOG_WC_CLIPDOT);
			d0 = DotProduct (pv0->position, clip->normal) - clip->dist;
			d1 = DotProduct (pv1->position, clip->normal) - clip->dist;
#endif

			if (d0 >= 0)
			{
			// point 0 is unclipped
				if (d1 >= 0)
				{
				// both points are unclipped
					continue;
				}

			// only point 1 is clipped

			// we don't cache clipped edges
				cacheoffset = 0x7FFFFFFF;

				QOG_AQ (AQ_FCLIPVERT, 1);
				f = d0 / (d0 - d1);
				clipvert.position[0] = pv0->position[0] +
						f * (pv1->position[0] - pv0->position[0]);
				clipvert.position[1] = pv0->position[1] +
						f * (pv1->position[1] - pv0->position[1]);
				clipvert.position[2] = pv0->position[2] +
						f * (pv1->position[2] - pv0->position[2]);

				if (clip->leftedge)
				{
					r_leftclipped = true;
					r_leftexit = clipvert;
				}
				else if (clip->rightedge)
				{
					r_rightclipped = true;
					r_rightexit = clipvert;
				}

				R_ClipEdge (pv0, &clipvert, clip->next);
				return;
			}
			else
			{
			// point 0 is clipped
				if (d1 < 0)
				{
				// both points are clipped
				// we do cache fully clipped edges
					if (!r_leftclipped)
						cacheoffset = FULLY_CLIPPED_CACHED |
								(r_framecount & FRAMECOUNT_MASK);
					return;
				}

			// only point 0 is clipped
				r_lastvertvalid = false;

			// we don't cache partially clipped edges
				cacheoffset = 0x7FFFFFFF;

				QOG_AQ (AQ_FCLIPVERT, 1);
				f = d0 / (d0 - d1);
				clipvert.position[0] = pv0->position[0] +
						f * (pv1->position[0] - pv0->position[0]);
				clipvert.position[1] = pv0->position[1] +
						f * (pv1->position[1] - pv0->position[1]);
				clipvert.position[2] = pv0->position[2] +
						f * (pv1->position[2] - pv0->position[2]);

				if (clip->leftedge)
				{
					r_leftclipped = true;
					r_leftenter = clipvert;
				}
				else if (clip->rightedge)
				{
					r_rightclipped = true;
					r_rightenter = clipvert;
				}

				R_ClipEdge (&clipvert, pv1, clip->next);
				return;
			}
		} while ((clip = clip->next) != NULL);
	}

// add the edge
	R_EmitEdge (pv0, pv1);
}

#endif	// !id386


/*
================
R_EmitCachedEdge
================
*/
void R_EmitCachedEdge (void)
{
	edge_t		*pedge_t;

	QOG_WC (QOG_WC_CACHED);	// QuakeOG (Agent Z)
	pedge_t = EC_EDGE (currententity->model, r_pedge);	// QuakeOG

	if (!pedge_t->surfs[0])
		pedge_t->surfs[0] = surface_p - surfaces;
	else
		pedge_t->surfs[1] = surface_p - surfaces;

#ifdef QOG_FRONT_FAST
	if (R_QOGFX_ON ())
	{
		// QuakeOG (Agent AJ): Fast keys (0x7FFFFFFF - z) as ints
		if (R_QogGetI (&pedge_t->nearzi) > R_QogGetI (&r_nearzi))
			R_QogSetI (&r_nearzi, R_QogGetI (&pedge_t->nearzi));
	}
	else
#endif
	if (pedge_t->nearzi > r_nearzi)	// for mipmap finding
		r_nearzi = pedge_t->nearzi;

	r_emitted = 1;
}


/*
================
R_RenderFace
================
*/
void R_RenderFace (msurface_t *fa, int clipflags)
{
	int			i, lindex;
	unsigned	mask;
	mplane_t	*pplane;
	float		distinv;
	vec3_t		p_normal;
	medge_t		*pedges, tedge;
	clipplane_t	*pclip;

	QOG_WC (QOG_WC_FACES);	// QuakeOG (Agent Z)
#if defined(QOG_EDGECACHE16) && !defined(QOG_EDGECACHE_ZBUFFER)
	R_QogEdgeCacheWindow (currententity->model);	// QuakeOG
#endif

// skip out if no more surfs
	if ((surface_p) >= surf_max)
	{
		r_outofsurfaces++;
		return;
	}

// ditto if not enough edges left, or switch to auxedges if possible
	if ((edge_p + fa->numedges + 4) >= edge_max
#ifdef QOG_EDGECACHE_TAIL
		// QuakeOG (Agent AP): the table met the edge cache, which moves away
		&& !(R_QogEdgeCacheOut () && (edge_p + fa->numedges + 4) < edge_max)
#endif
		)
	{
		r_outofedges += fa->numedges;
		return;
	}

	c_faceclip++;

// set up clip planes
	pclip = NULL;

	for (i=3, mask = 0x08 ; i>=0 ; i--, mask >>= 1)
	{
		if (clipflags & mask)
		{
			view_clipplanes[i].next = pclip;
			pclip = &view_clipplanes[i];
		}
	}

// push the edges through
	r_emitted = 0;
	r_nearzi = 0;
	r_nearzionly = false;
	makeleftedge = makerightedge = false;
	pedges = currententity->model->edges;
	r_lastvertvalid = false;

	for (i=0 ; i<fa->numedges ; i++)
	{
		lindex = currententity->model->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];

		// if the edge is cached, we can just reuse the edge
			if (!insubmodel)
			{
				if (EC_IS_CLIPPED (currententity->model, r_pedge))	// QuakeOG
				{
					if (EC_CLIPPED_NOW (currententity->model, r_pedge))
					{
						r_lastvertvalid = false;
						continue;
					}
				}
				else
				{
					if (EC_VALID (currententity->model, r_pedge) &&	// QuakeOG
						EC_EDGE (currententity->model, r_pedge)->owner == EDGE_OWNER (r_pedge))
					{
						R_EmitCachedEdge ();
						r_lastvertvalid = false;
						continue;
					}
				}
			}

		// assume it's cacheable
			cacheoffset = (byte *)edge_p - (byte *)r_edges;
			r_leftclipped = r_rightclipped = false;
			R_ClipEdge (&r_pcurrentvertbase[r_pedge->v[0]],
						&r_pcurrentvertbase[r_pedge->v[1]],
						pclip);
			EC_STORE (currententity->model, r_pedge, cacheoffset);	// QuakeOG

			if (r_leftclipped)
				makeleftedge = true;
			if (r_rightclipped)
				makerightedge = true;
			r_lastvertvalid = true;
		}
		else
		{
			lindex = -lindex;
			r_pedge = &pedges[lindex];
		// if the edge is cached, we can just reuse the edge
			if (!insubmodel)
			{
				if (EC_IS_CLIPPED (currententity->model, r_pedge))	// QuakeOG
				{
					if (EC_CLIPPED_NOW (currententity->model, r_pedge))
					{
						r_lastvertvalid = false;
						continue;
					}
				}
				else
				{
				// it's cached if the cached edge is valid and is owned
				// by this medge_t
					if (EC_VALID (currententity->model, r_pedge) &&	// QuakeOG
						EC_EDGE (currententity->model, r_pedge)->owner == EDGE_OWNER (r_pedge))
					{
						R_EmitCachedEdge ();
						r_lastvertvalid = false;
						continue;
					}
				}
			}

		// assume it's cacheable
			cacheoffset = (byte *)edge_p - (byte *)r_edges;
			r_leftclipped = r_rightclipped = false;
			R_ClipEdge (&r_pcurrentvertbase[r_pedge->v[1]],
						&r_pcurrentvertbase[r_pedge->v[0]],
						pclip);
			EC_STORE (currententity->model, r_pedge, cacheoffset);	// QuakeOG

			if (r_leftclipped)
				makeleftedge = true;
			if (r_rightclipped)
				makerightedge = true;
			r_lastvertvalid = true;
		}
	}

// if there was a clip off the left edge, add that edge too
// FIXME: faster to do in screen space?
// FIXME: share clipped edges?
	if (makeleftedge)
	{
		r_pedge = &tedge;
		r_lastvertvalid = false;
		R_ClipEdge (&r_leftexit, &r_leftenter, pclip->next);
	}

// if there was a clip off the right edge, get the right r_nearzi
	if (makerightedge)
	{
		r_pedge = &tedge;
		r_lastvertvalid = false;
		r_nearzionly = true;
		R_ClipEdge (&r_rightexit, &r_rightenter, view_clipplanes[1].next);
	}

// if no edges made it out, return without posting the surface
	if (!r_emitted)
		return;

	r_polycount++;

#ifdef QOG_SURF_T_TINY
	// QuakeOG (Agent S): the face as an index into the entity model's surfaces
	if (r_currentkey > SURF_KEY_MAX)
		Sys_Error ("R_RenderFace: surface key %d, 16-bit keys allow %d", r_currentkey, SURF_KEY_MAX);
	surface_p->dataref = fa - currententity->model->surfaces;
	surface_p->entref = r_qog_visedict;
#else
	surface_p->data = (void *)fa;
#endif
#ifdef QOG_FRONT_FAST
	surface_p->nearzi = R_QOGFX_ON () ? R_QogNearzi () : r_nearzi;	// QuakeOG (Agent AJ)
#else
	surface_p->nearzi = r_nearzi;
#endif
	surface_p->flags = fa->flags;
	surface_p->insubmodel = insubmodel;
	surface_p->spanstate = 0;
#ifndef QOG_SURF_T_TINY
	surface_p->entity = currententity;
#endif
	surface_p->key = r_currentkey++;
	surface_p->spans = SPAN_NONE;	// QuakeOG

	pplane = SURF_PLANE (currententity->model, fa);	// QuakeOG
#ifdef QOG_FRONT_FAST
	if (R_QOGFX_ON ())
		R_QogFaceGradFast (pplane);	// QuakeOG (Agent AJ)
	else
#endif
	{
// FIXME: cache this?
	TransformVector (pplane->normal, p_normal);
// FIXME: cache this?
	distinv = QOG_FRECIP (pplane->dist - DotProduct (modelorg, pplane->normal));	// QuakeOG: QOG_FLOAT_EXACT

	surface_p->d_zistepu = p_normal[0] * xscaleinv * distinv;
	surface_p->d_zistepv = -p_normal[1] * yscaleinv * distinv;
	surface_p->d_ziorigin = p_normal[2] * distinv -
			xcenter * surface_p->d_zistepu -
			ycenter * surface_p->d_zistepv;
	}

//JDC	VectorCopy (r_worldmodelorg, surface_p->modelorg);
	surface_p++;
}


/*
================
R_RenderBmodelFace
================
*/
void R_RenderBmodelFace (bedge_t *pedges, msurface_t *psurf)
{
	int			i;
	unsigned	mask;
	mplane_t	*pplane;
	float		distinv;
	vec3_t		p_normal;
	medge_t		tedge;
	clipplane_t	*pclip;

// skip out if no more surfs
	if (surface_p >= surf_max)
	{
		r_outofsurfaces++;
		return;
	}

// ditto if not enough edges left, or switch to auxedges if possible
	if ((edge_p + psurf->numedges + 4) >= edge_max)
	{
		r_outofedges += psurf->numedges;
		return;
	}

	c_faceclip++;

// this is a dummy to give the caching mechanism someplace to write to
	r_pedge = &tedge;
	QOG_AQ (AQ_BFACES, 1);

// set up clip planes
	pclip = NULL;

	for (i=3, mask = 0x08 ; i>=0 ; i--, mask >>= 1)
	{
		if (r_clipflags & mask)
		{
			view_clipplanes[i].next = pclip;
			pclip = &view_clipplanes[i];
		}
	}

// push the edges through
	r_emitted = 0;
	r_nearzi = 0;
	r_nearzionly = false;
	makeleftedge = makerightedge = false;
// FIXME: keep clipped bmodel edges in clockwise order so last vertex caching
// can be used?
	r_lastvertvalid = false;

	for ( ; pedges ; pedges = pedges->pnext)
	{
		r_leftclipped = r_rightclipped = false;
		R_ClipEdge (pedges->v[0], pedges->v[1], pclip);

		if (r_leftclipped)
			makeleftedge = true;
		if (r_rightclipped)
			makerightedge = true;
	}

// if there was a clip off the left edge, add that edge too
// FIXME: faster to do in screen space?
// FIXME: share clipped edges?
	if (makeleftedge)
	{
		r_pedge = &tedge;
		R_ClipEdge (&r_leftexit, &r_leftenter, pclip->next);
	}

// if there was a clip off the right edge, get the right r_nearzi
	if (makerightedge)
	{
		r_pedge = &tedge;
		r_nearzionly = true;
		R_ClipEdge (&r_rightexit, &r_rightenter, view_clipplanes[1].next);
	}

// if no edges made it out, return without posting the surface
	if (!r_emitted)
		return;

	r_polycount++;

#ifdef QOG_SURF_T_TINY
	// QuakeOG (Agent S): the face as an index into the entity model's surfaces
	if (r_currentbkey > SURF_KEY_MAX)
		Sys_Error ("R_RenderBmodelFace: surface key %d, 16-bit keys allow %d", r_currentbkey, SURF_KEY_MAX);
	surface_p->dataref = psurf - currententity->model->surfaces;
	surface_p->entref = r_qog_visedict;
#else
	surface_p->data = (void *)psurf;
#endif
#ifdef QOG_FRONT_FAST
	surface_p->nearzi = R_QOGFX_ON () ? R_QogNearzi () : r_nearzi;	// QuakeOG (Agent AJ)
#else
	surface_p->nearzi = r_nearzi;
#endif
	surface_p->flags = psurf->flags;
	surface_p->insubmodel = true;
	surface_p->spanstate = 0;
#ifndef QOG_SURF_T_TINY
	surface_p->entity = currententity;
#endif
	surface_p->key = r_currentbkey;
	surface_p->spans = SPAN_NONE;	// QuakeOG

	pplane = SURF_PLANE (currententity->model, psurf);	// QuakeOG
#ifdef QOG_FRONT_FAST
	if (R_QOGFX_ON ())
		R_QogFaceGradFast (pplane);	// QuakeOG (Agent AJ)
	else
#endif
	{
// FIXME: cache this?
	TransformVector (pplane->normal, p_normal);
// FIXME: cache this?
	distinv = QOG_FRECIP (pplane->dist - DotProduct (modelorg, pplane->normal));	// QuakeOG: QOG_FLOAT_EXACT

	surface_p->d_zistepu = p_normal[0] * xscaleinv * distinv;
	surface_p->d_zistepv = -p_normal[1] * yscaleinv * distinv;
	surface_p->d_ziorigin = p_normal[2] * distinv -
			xcenter * surface_p->d_zistepu -
			ycenter * surface_p->d_zistepv;
	}

//JDC	VectorCopy (r_worldmodelorg, surface_p->modelorg);
	surface_p++;
}


/*
================
R_RenderPoly
================
*/
void R_RenderPoly (msurface_t *fa, int clipflags)
{
	int			i, lindex, lnumverts, s_axis, t_axis;
	float		dist, lastdist, lzi, scale, u, v, frac;
	unsigned	mask;
	vec3_t		local, transformed;
	clipplane_t	*pclip;
	medge_t		*pedges;
	mplane_t	*pplane;
	mvertex_t	verts[2][100];	//FIXME: do real number
	polyvert_t	pverts[100];	//FIXME: do real number, safely
	int			vertpage, newverts, newpage, lastvert;
	qboolean	visible;

// FIXME: clean this up and make it faster
// FIXME: guard against running out of vertices

	s_axis = t_axis = 0;	// keep compiler happy

// set up clip planes
	pclip = NULL;

	for (i=3, mask = 0x08 ; i>=0 ; i--, mask >>= 1)
	{
		if (clipflags & mask)
		{
			view_clipplanes[i].next = pclip;
			pclip = &view_clipplanes[i];
		}
	}

// reconstruct the polygon
// FIXME: these should be precalculated and loaded off disk
	pedges = currententity->model->edges;
	lnumverts = fa->numedges;
	vertpage = 0;

	for (i=0 ; i<lnumverts ; i++)
	{
		lindex = currententity->model->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			verts[0][i] = r_pcurrentvertbase[r_pedge->v[0]];
		}
		else
		{
			r_pedge = &pedges[-lindex];
			verts[0][i] = r_pcurrentvertbase[r_pedge->v[1]];
		}
	}

// clip the polygon, done if not visible
	while (pclip)
	{
		lastvert = lnumverts - 1;
		lastdist = DotProduct (verts[vertpage][lastvert].position,
							   pclip->normal) - pclip->dist;

		visible = false;
		newverts = 0;
		newpage = vertpage ^ 1;

		for (i=0 ; i<lnumverts ; i++)
		{
			dist = DotProduct (verts[vertpage][i].position, pclip->normal) -
					pclip->dist;

			if ((lastdist > 0) != (dist > 0))
			{
				frac = dist / (dist - lastdist);
				verts[newpage][newverts].position[0] =
						verts[vertpage][i].position[0] +
						((verts[vertpage][lastvert].position[0] -
						  verts[vertpage][i].position[0]) * frac);
				verts[newpage][newverts].position[1] =
						verts[vertpage][i].position[1] +
						((verts[vertpage][lastvert].position[1] -
						  verts[vertpage][i].position[1]) * frac);
				verts[newpage][newverts].position[2] =
						verts[vertpage][i].position[2] +
						((verts[vertpage][lastvert].position[2] -
						  verts[vertpage][i].position[2]) * frac);
				newverts++;
			}

			if (dist >= 0)
			{
				verts[newpage][newverts] = verts[vertpage][i];
				newverts++;
				visible = true;
			}

			lastvert = i;
			lastdist = dist;
		}

		if (!visible || (newverts < 3))
			return;

		lnumverts = newverts;
		vertpage ^= 1;
		pclip = pclip->next;
	}

// transform and project, remembering the z values at the vertices and
// r_nearzi, and extract the s and t coordinates at the vertices
	pplane = SURF_PLANE (currententity->model, fa);	// QuakeOG
	switch (pplane->type)
	{
	case PLANE_X:
	case PLANE_ANYX:
		s_axis = 1;
		t_axis = 2;
		break;
	case PLANE_Y:
	case PLANE_ANYY:
		s_axis = 0;
		t_axis = 2;
		break;
	case PLANE_Z:
	case PLANE_ANYZ:
		s_axis = 0;
		t_axis = 1;
		break;
	}

	r_nearzi = 0;

	for (i=0 ; i<lnumverts ; i++)
	{
	// transform and project
		VectorSubtract (verts[vertpage][i].position, modelorg, local);
		TransformVector (local, transformed);

		if (QOG_NEARCLIPPED (transformed[2]))	// QuakeOG: QOG_FLOAT_EXACT
			transformed[2] = NEAR_CLIP;

		lzi = QOG_FRECIP (transformed[2]);	// QuakeOG: QOG_FLOAT_EXACT

		if (lzi > r_nearzi)	// for mipmap finding
			r_nearzi = lzi;

	// FIXME: build x/yscale into transform?
		scale = xscale * lzi;
		u = (xcenter + scale*transformed[0]);
		if (u < r_refdef.fvrectx_adj)
			u = r_refdef.fvrectx_adj;
		if (u > r_refdef.fvrectright_adj)
			u = r_refdef.fvrectright_adj;

		scale = yscale * lzi;
		v = (ycenter - scale*transformed[1]);
		if (v < r_refdef.fvrecty_adj)
			v = r_refdef.fvrecty_adj;
		if (v > r_refdef.fvrectbottom_adj)
			v = r_refdef.fvrectbottom_adj;

		pverts[i].u = u;
		pverts[i].v = v;
		pverts[i].zi = lzi;
		pverts[i].s = verts[vertpage][i].position[s_axis];
		pverts[i].t = verts[vertpage][i].position[t_axis];
	}

// build the polygon descriptor, including fa, r_nearzi, and u, v, s, t, and z
// for each vertex
	r_polydesc.numverts = lnumverts;
	r_polydesc.nearzi = r_nearzi;
	r_polydesc.pcurrentface = fa;
	r_polydesc.pverts = pverts;

// draw the polygon
	D_DrawPoly ();
}


/*
================
R_ZDrawSubmodelPolys
================
*/
void R_ZDrawSubmodelPolys (model_t *pmodel)
{
	int			i, numsurfaces;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;

	psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
	numsurfaces = pmodel->nummodelsurfaces;

	for (i=0 ; i<numsurfaces ; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = SURF_PLANE (pmodel, psurf);	// QuakeOG

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
		// FIXME: use bounding-box-based frustum clipping info?
			R_RenderPoly (psurf, 15);
		}
	}
}

