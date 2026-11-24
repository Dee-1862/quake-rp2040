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

	if (r_lastvertvalid)
	{
		u0 = r_u1;
		v0 = r_v1;
		lzi0 = r_lzi1;
		ceilv0 = r_ceilv1;
	}
	else
	{
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

	if (r_lzi1 > lzi0)
		lzi0 = r_lzi1;

	if (lzi0 > r_nearzi)	// for mipmap finding
		r_nearzi = lzi0;

// for right edges, all we want is the effect on 1/z
	if (r_nearzionly)
		return;

	r_emitted = 1;

	r_ceilv1 = (int) QOG_CEILF(r_v1);	// QuakeOG: QOG_FLOAT_EXACT


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

	if (clip)
	{
		do
		{
			d0 = DotProduct (pv0->position, clip->normal) - clip->dist;
			d1 = DotProduct (pv1->position, clip->normal) - clip->dist;

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

	pedge_t = EC_EDGE (currententity->model, r_pedge);	// QuakeOG

	if (!pedge_t->surfs[0])
		pedge_t->surfs[0] = surface_p - surfaces;
	else
		pedge_t->surfs[1] = surface_p - surfaces;

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
	if ((edge_p + fa->numedges + 4) >= edge_max)
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
	surface_p->nearzi = r_nearzi;
	surface_p->flags = fa->flags;
	surface_p->insubmodel = insubmodel;
	surface_p->spanstate = 0;
#ifndef QOG_SURF_T_TINY
	surface_p->entity = currententity;
#endif
	surface_p->key = r_currentkey++;
	surface_p->spans = SPAN_NONE;	// QuakeOG

	pplane = SURF_PLANE (currententity->model, fa);	// QuakeOG
// FIXME: cache this?
	TransformVector (pplane->normal, p_normal);
// FIXME: cache this?
	distinv = QOG_FRECIP (pplane->dist - DotProduct (modelorg, pplane->normal));	// QuakeOG: QOG_FLOAT_EXACT

	surface_p->d_zistepu = p_normal[0] * xscaleinv * distinv;
	surface_p->d_zistepv = -p_normal[1] * yscaleinv * distinv;
	surface_p->d_ziorigin = p_normal[2] * distinv -
			xcenter * surface_p->d_zistepu -
			ycenter * surface_p->d_zistepv;

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
	surface_p->nearzi = r_nearzi;
	surface_p->flags = psurf->flags;
	surface_p->insubmodel = true;
	surface_p->spanstate = 0;
#ifndef QOG_SURF_T_TINY
	surface_p->entity = currententity;
#endif
	surface_p->key = r_currentbkey;
	surface_p->spans = SPAN_NONE;	// QuakeOG

	pplane = SURF_PLANE (currententity->model, psurf);	// QuakeOG
// FIXME: cache this?
	TransformVector (pplane->normal, p_normal);
// FIXME: cache this?
	distinv = QOG_FRECIP (pplane->dist - DotProduct (modelorg, pplane->normal));	// QuakeOG: QOG_FLOAT_EXACT

	surface_p->d_zistepu = p_normal[0] * xscaleinv * distinv;
	surface_p->d_zistepv = -p_normal[1] * yscaleinv * distinv;
	surface_p->d_ziorigin = p_normal[2] * distinv -
			xcenter * surface_p->d_zistepu -
			ycenter * surface_p->d_zistepv;

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

