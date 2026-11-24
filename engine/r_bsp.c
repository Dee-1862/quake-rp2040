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
// r_bsp.c

#include "quakedef.h"
#include "r_local.h"
#include "qog_phase.h"	// QuakeOG (Agent Z): world work counts (QOG_PHASE_XIP)

//
// current entity info
//
qboolean		insubmodel;
entity_t		*currententity;
vec3_t			modelorg, base_modelorg;
								// modelorg is the viewpoint reletive to
								// the currently rendering entity
vec3_t			r_entorigin;	// the currently rendering entity in world
								// coordinates

float			entity_rotation[3][3];

vec3_t			r_worldmodelorg;

int				r_currentbkey;

typedef enum {touchessolid, drawnode, nodrawnode} solidstate_t;

// QuakeOG: these hold one brush entity polygon after clipping against the
// world, and are reset for every polygon. Shareware peaks are 22 vertices
// and 74 edges over all three demos (qog_mem prints them). Overflow only
// drops that fragment with a console message, as in id's code.
// (MAX_BMODEL_VERTS and MAX_BMODEL_EDGES are in r_local.h, QuakeOG)
static mvertex_t	*pbverts;
static bedge_t		*pbedges;
static int			numbverts, numbedges;
int					r_qog_peakbverts, r_qog_peakbedges;	// QuakeOG: high-water marks, printed by qog_mem

static mvertex_t	*pfrontenter, *pfrontexit;

static qboolean		makeclippededge;


//===========================================================================

/*
================
R_EntityRotate
================
*/
void R_EntityRotate (vec3_t vec)
{
	vec3_t	tvec;

	VectorCopy (vec, tvec);
	vec[0] = DotProduct (entity_rotation[0], tvec);
	vec[1] = DotProduct (entity_rotation[1], tvec);
	vec[2] = DotProduct (entity_rotation[2], tvec);
}


/*
================
R_RotateBmodel
================
*/
void R_RotateBmodel (void)
{
	float	angle, s, c, temp1[3][3], temp2[3][3], temp3[3][3];

// TODO: should use a look-up table
// TODO: should really be stored with the entity instead of being reconstructed
// TODO: could cache lazily, stored in the entity
// TODO: share work with R_SetUpAliasTransform

// yaw
	angle = currententity->angles[YAW];		
	angle = angle * M_PI*2 / 360;
	s = sin(angle);
	c = cos(angle);

	temp1[0][0] = c;
	temp1[0][1] = s;
	temp1[0][2] = 0;
	temp1[1][0] = -s;
	temp1[1][1] = c;
	temp1[1][2] = 0;
	temp1[2][0] = 0;
	temp1[2][1] = 0;
	temp1[2][2] = 1;


// pitch
	angle = currententity->angles[PITCH];		
	angle = angle * M_PI*2 / 360;
	s = sin(angle);
	c = cos(angle);

	temp2[0][0] = c;
	temp2[0][1] = 0;
	temp2[0][2] = -s;
	temp2[1][0] = 0;
	temp2[1][1] = 1;
	temp2[1][2] = 0;
	temp2[2][0] = s;
	temp2[2][1] = 0;
	temp2[2][2] = c;

	R_ConcatRotations (temp2, temp1, temp3);

// roll
	angle = currententity->angles[ROLL];		
	angle = angle * M_PI*2 / 360;
	s = sin(angle);
	c = cos(angle);

	temp1[0][0] = 1;
	temp1[0][1] = 0;
	temp1[0][2] = 0;
	temp1[1][0] = 0;
	temp1[1][1] = c;
	temp1[1][2] = s;
	temp1[2][0] = 0;
	temp1[2][1] = -s;
	temp1[2][2] = c;

	R_ConcatRotations (temp1, temp3, entity_rotation);

//
// rotate modelorg and the transformation matrix
//
	R_EntityRotate (modelorg);
	R_EntityRotate (vpn);
	R_EntityRotate (vright);
	R_EntityRotate (vup);

	R_TransformFrustum ();
}


/*
================
R_RecursiveClipBPoly
================
*/
void R_RecursiveClipBPoly (bedge_t *pedges, mnode_t *pnode, msurface_t *psurf)
{
	bedge_t		*psideedges[2], *pnextedge, *ptedge;
	int			i, side, lastside;
	float		dist, frac, lastdist;
	mplane_t	*splitplane, tplane;
	mvertex_t	*pvert, *plastvert, *ptvert;
	mnode_t		*pn;

	psideedges[0] = psideedges[1] = NULL;

	makeclippededge = false;

// transform the BSP plane into model space
// FIXME: cache these?
	splitplane = pnode->plane;
	tplane.dist = splitplane->dist -
			DotProduct(r_entorigin, splitplane->normal);
	tplane.normal[0] = DotProduct (entity_rotation[0], splitplane->normal);
	tplane.normal[1] = DotProduct (entity_rotation[1], splitplane->normal);
	tplane.normal[2] = DotProduct (entity_rotation[2], splitplane->normal);

// clip edges to BSP plane
	for ( ; pedges ; pedges = pnextedge)
	{
		pnextedge = pedges->pnext;

	// set the status for the last point as the previous point
	// FIXME: cache this stuff somehow?
		plastvert = pedges->v[0];
		lastdist = DotProduct (plastvert->position, tplane.normal) -
				   tplane.dist;

		if (lastdist > 0)
			lastside = 0;
		else
			lastside = 1;

		pvert = pedges->v[1];

		dist = DotProduct (pvert->position, tplane.normal) - tplane.dist;

		if (dist > 0)
			side = 0;
		else
			side = 1;

		if (side != lastside)
		{
		// clipped
			if (numbverts >= MAX_BMODEL_VERTS)
				return;

		// generate the clipped vertex
			frac = lastdist / (lastdist - dist);
			ptvert = &pbverts[numbverts++];
			ptvert->position[0] = plastvert->position[0] +
					frac * (pvert->position[0] -
					plastvert->position[0]);
			ptvert->position[1] = plastvert->position[1] +
					frac * (pvert->position[1] -
					plastvert->position[1]);
			ptvert->position[2] = plastvert->position[2] +
					frac * (pvert->position[2] -
					plastvert->position[2]);

		// split into two edges, one on each side, and remember entering
		// and exiting points
		// FIXME: share the clip edge by having a winding direction flag?
			if (numbedges >= (MAX_BMODEL_EDGES - 1))
			{
				CON_PRINTTEXT ("Out of edges for bmodel\n");	// QuakeOG: no 4 KB buffer this deep
				return;
			}

			ptedge = &pbedges[numbedges];
			ptedge->pnext = psideedges[lastside];
			psideedges[lastside] = ptedge;
			ptedge->v[0] = plastvert;
			ptedge->v[1] = ptvert;

			ptedge = &pbedges[numbedges + 1];
			ptedge->pnext = psideedges[side];
			psideedges[side] = ptedge;
			ptedge->v[0] = ptvert;
			ptedge->v[1] = pvert;

			numbedges += 2;

			if (side == 0)
			{
			// entering for front, exiting for back
				pfrontenter = ptvert;
				makeclippededge = true;
			}
			else
			{
				pfrontexit = ptvert;
				makeclippededge = true;
			}
		}
		else
		{
		// add the edge to the appropriate side
			pedges->pnext = psideedges[side];
			psideedges[side] = pedges;
		}
	}

// if anything was clipped, reconstitute and add the edges along the clip
// plane to both sides (but in opposite directions)
	if (makeclippededge)
	{
		if (numbedges >= (MAX_BMODEL_EDGES - 2))
		{
			CON_PRINTTEXT ("Out of edges for bmodel\n");	// QuakeOG: no 4 KB buffer this deep
			return;
		}

		ptedge = &pbedges[numbedges];
		ptedge->pnext = psideedges[0];
		psideedges[0] = ptedge;
		ptedge->v[0] = pfrontexit;
		ptedge->v[1] = pfrontenter;

		ptedge = &pbedges[numbedges + 1];
		ptedge->pnext = psideedges[1];
		psideedges[1] = ptedge;
		ptedge->v[0] = pfrontenter;
		ptedge->v[1] = pfrontexit;

		numbedges += 2;
	}

// draw or recurse further
	for (i=0 ; i<2 ; i++)
	{
		if (psideedges[i])
		{
		// draw if we've reached a non-solid leaf, done if all that's left is a
		// solid leaf, and continue down the tree if it's not a leaf
			pn = pnode->children[i];

		// we're done with this branch if the node or leaf isn't in the PVS
			if (NODE_VISFRAME_IS (cl.worldmodel, pn, r_visframecount))	// QuakeOG
			{
				if (pn->contents < 0)
				{
					if (pn->contents != CONTENTS_SOLID)
					{
						r_currentbkey = LEAF_KEY (cl.worldmodel, (mleaf_t *)pn);
						R_RenderBmodelFace (psideedges[i], psurf);
					}
				}
				else
				{
					R_RecursiveClipBPoly (psideedges[i], pnode->children[i],
									  psurf);
				}
			}
		}
	}
}


/*
================
R_DrawSolidClippedSubmodelPolygons
================
*/
void R_DrawSolidClippedSubmodelPolygons (model_t *pmodel)
{
	int			i, j, lindex;
	vec_t		dot;
	msurface_t	*psurf;
	int			numsurfaces;
	mplane_t	*pplane;
#if defined(QOG_EDGES_IN_ZBUFFER)
	// QuakeOG: the clip buffers sit at the start of the z-buffer, which
	// holds no z yet. With QOG_EDGECACHE_ZBUFFER the world's edge cache was
	// there, which nothing reads after R_RenderWorld (R_EdgeDrawing, R_NewMap
	// checks the room).
	mvertex_t	*bverts = (mvertex_t *)r_ztemp_base;
	bedge_t		*bedges = (bedge_t *)(bverts + MAX_BMODEL_VERTS), *pbedge;
#elif defined(QOG_SPANS_IN_EDGES)
	// QuakeOG: the clip buffers sit in the edge table past the edge limit,
	// which only R_ScanEdges uses later (r_local.h)
	mvertex_t	*bverts = (mvertex_t *)(r_spanpool_end - SPAN_RESERVE_EDGES * sizeof(edge_t));
	bedge_t		*bedges = (bedge_t *)(bverts + MAX_BMODEL_VERTS), *pbedge;
#else
	mvertex_t	bverts[MAX_BMODEL_VERTS];
	bedge_t		bedges[MAX_BMODEL_EDGES], *pbedge;
#endif
	medge_t		*pedge, *pedges;

// FIXME: use bounding-box-based frustum clipping info?

	psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
	numsurfaces = pmodel->nummodelsurfaces;
	pedges = pmodel->edges;

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

		// copy the edges to bedges, flipping if necessary so always
		// clockwise winding
		// FIXME: if edges and vertices get caches, these assignments must move
		// outside the loop, and overflow checking must be done here
			pbverts = bverts;
			pbedges = bedges;
			numbverts = numbedges = 0;

			if (psurf->numedges > 0)
			{
				pbedge = &bedges[numbedges];
				numbedges += psurf->numedges;

				for (j=0 ; j<psurf->numedges ; j++)
				{
				   lindex = pmodel->surfedges[psurf->firstedge+j];

					if (lindex > 0)
					{
						pedge = &pedges[lindex];
						pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
						pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
					}
					else
					{
						lindex = -lindex;
						pedge = &pedges[lindex];
						pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
						pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
					}

					pbedge[j].pnext = &pbedge[j+1];
				}

				pbedge[j-1].pnext = NULL;	// mark end of edges

				R_RecursiveClipBPoly (pbedge, ENT_TOPNODE (currententity), psurf);

				// QuakeOG: high-water marks, to size bverts and bedges from measurements
				if (numbverts > r_qog_peakbverts)
					r_qog_peakbverts = numbverts;
				if (numbedges > r_qog_peakbedges)
					r_qog_peakbedges = numbedges;
			}
			else
			{
				Sys_Error ("no edges in bmodel");
			}
		}
	}
}


/*
================
R_DrawSubmodelPolygons
================
*/
void R_DrawSubmodelPolygons (model_t *pmodel, int clipflags)
{
	int			i;
	vec_t		dot;
	msurface_t	*psurf;
	int			numsurfaces;
	mplane_t	*pplane;

// FIXME: use bounding-box-based frustum clipping info?

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
			r_currentkey = LEAF_KEY (cl.worldmodel, (mleaf_t *)ENT_TOPNODE (currententity));

		// FIXME: use bounding-box-based frustum clipping info?
			R_RenderFace (psurf, clipflags);
		}
	}
}


#ifdef QOG_WORLDNODE_FAST
/*
================
R_QogCullNode

QuakeOG (Agent Z): R_RecursiveWorldNode's frustum tests without double
arithmetic. id's d holds the float dot product, and (double)dot -
(double)dist is exact in sign and zero only when the two are equal, so
d <= 0 and d >= 0 are the float comparisons dot <= dist and dot >= dist.
The box corners are converted to float once per node, not once per plane.
Returns the new clip flags, or -1 when the node is outside a plane. Not
inlined, so its locals are not in every level of the recursion.
================
*/
#if defined(__GNUC__)
#define QOG_NOINLINE	__attribute__((noinline))
#elif defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE
#endif
static QOG_NOINLINE int R_QogCullNode (mnode_t *node, int clipflags)
{
	int		i, *pindex;
	float	mm[6];
	vec3_t	acceptpt, rejectpt;

	for (i=0 ; i<6 ; i++)
		mm[i] = (float)node->minmaxs[i];

	for (i=0 ; i<4 ; i++)
	{
		if (! (clipflags & (1<<i)) )
			continue;	// don't need to clip against it

		pindex = pfrustum_indexes[i];

		rejectpt[0] = mm[pindex[0]];
		rejectpt[1] = mm[pindex[1]];
		rejectpt[2] = mm[pindex[2]];

		if (DotProduct (rejectpt, view_clipplanes[i].normal) <= view_clipplanes[i].dist)
			return -1;

		acceptpt[0] = mm[pindex[3+0]];
		acceptpt[1] = mm[pindex[3+1]];
		acceptpt[2] = mm[pindex[3+2]];

		if (DotProduct (acceptpt, view_clipplanes[i].normal) >= view_clipplanes[i].dist)
			clipflags &= ~(1<<i);	// node is entirely on screen
	}
	return clipflags;
}
#endif

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node, int clipflags)
{
	int			i, c, side, *pindex;
	vec3_t		acceptpt, rejectpt;
	mplane_t	*plane;
	msurface_t	*surf;
	marksurf_t	*mark;	// QuakeOG: a pointer or a 16-bit surface number
	mleaf_t		*pleaf;
#ifdef QOG_FLOAT_EXACT
	// QuakeOG (Agent AA): id's doubles here only ever hold a float expression
	// or the difference of two floats, and are only compared with 0 or with
	// BACKFACE_EPSILON. A float difference rounds to the same sign and zero,
	// and no float lies between 0.01 and (float)0.01, so every test is the same.
	float		d, dot;
#define QOG_BFEPS	((float)BACKFACE_EPSILON)
#else
	double		d, dot;
#define QOG_BFEPS	BACKFACE_EPSILON
#endif
#ifdef QOG_WORLDNODE_FAST
	int			facing;	// QuakeOG (Agent Z): -1 back, 1 front, 0 neither
#endif

	QOG_WC (QOG_WC_NODES);	// QuakeOG (Agent Z)
	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (!NODE_VISFRAME_IS (cl.worldmodel, node, r_visframecount))	// QuakeOG
		return;

// cull the clipping planes if not trivial accept
// FIXME: the compiler is doing a lousy job of optimizing here; it could be
//  twice as fast in ASM
#ifdef QOG_WORLDNODE_FAST
	if (clipflags)
	{
		clipflags = R_QogCullNode (node, clipflags);	// QuakeOG (Agent Z)
		if (clipflags < 0)
			return;
	}
#else
	if (clipflags)
	{
		for (i=0 ; i<4 ; i++)
		{
			if (! (clipflags & (1<<i)) )
				continue;	// don't need to clip against it

		// generate accept and reject points
		// FIXME: do with fast look-ups or integer tests based on the sign bit
		// of the floating point values

			pindex = pfrustum_indexes[i];

			rejectpt[0] = (float)node->minmaxs[pindex[0]];
			rejectpt[1] = (float)node->minmaxs[pindex[1]];
			rejectpt[2] = (float)node->minmaxs[pindex[2]];
			
			d = DotProduct (rejectpt, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d <= 0)
				return;

			acceptpt[0] = (float)node->minmaxs[pindex[3+0]];
			acceptpt[1] = (float)node->minmaxs[pindex[3+1]];
			acceptpt[2] = (float)node->minmaxs[pindex[3+2]];

			d = DotProduct (acceptpt, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d >= 0)
				clipflags &= ~(1<<i);	// node is entirely on screen
		}
	}
#endif
	
// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;
		QOG_WC (QOG_WC_LEAFS);	// QuakeOG (Agent Z)

		mark = LEAF_MARKSURFACES (cl.worldmodel, pleaf);	// QuakeOG
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				SURF_VISFRAME_SETNUM (cl.worldmodel, MARKSURF_NUM (cl.worldmodel, mark), r_framecount);	// QuakeOG: MODFRAME, QOG_MOD_FLASH, QOG_FRAMEBITS
				mark++;
			} while (--c);
		}

	// deal with model fragments in this leaf
		if (LEAF_EFRAGS (cl.worldmodel, pleaf))
		{
#ifdef QOG_LEAF16
			R_StoreLeafEfrags (pleaf);	// QuakeOG: the leaf's head may be shared
#else
			R_StoreEfrags (&LEAF_EFRAGS (cl.worldmodel, pleaf));
#endif
		}

		LEAF_KEY_SET (cl.worldmodel, pleaf, r_currentkey);	// QuakeOG: QOG_LEAF16
		r_currentkey++;		// all bmodels in a leaf share the same key
	}
	else
	{
	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
		plane = node->plane;

#ifdef QOG_WORLDNODE_FAST
	// QuakeOG (Agent Z): id's dot is a double holding a float difference,
	// compared with 0 and with the double 0.01. For a float x, x < -0.01 and
	// x > 0.01 are x < -0.01f and x > 0.01f (0.01f is the float just below
	// 0.01), so the side and the face tests need no double arithmetic.
		{
		float	fdot;

		switch (plane->type)
		{
		case PLANE_X:
			fdot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			fdot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			fdot = modelorg[2] - plane->dist;
			break;
		default:
			fdot = DotProduct (modelorg, plane->normal) - plane->dist;
			break;
		}

		side = fdot >= 0 ? 0 : 1;
		facing = fdot < -0.01f ? -1 : (fdot > 0.01f ? 1 : 0);	// id's dot < -BACKFACE_EPSILON and dot > BACKFACE_EPSILON
		}
#else
		switch (plane->type)
		{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct (modelorg, plane->normal) - plane->dist;
			break;
		}
	
		if (dot >= 0)
			side = 0;
		else
			side = 1;
#endif

	// recurse down the children, front side first
		R_RecursiveWorldNode (node->children[side], clipflags);

	// draw stuff
		c = node->numsurfaces;

		if (c)
		{
			surf = cl.worldmodel->surfaces + node->firstsurface;

#ifdef QOG_WORLDNODE_FAST
			if (facing < 0)
#else
			if (dot < -QOG_BFEPS)
#endif
			{
				do
				{
					if ((surf->flags & SURF_PLANEBACK) &&
						SURF_VISFRAME_IS (cl.worldmodel, surf, r_framecount))	// QuakeOG
					{
						if (r_drawpolys)
						{
							if (r_worldpolysbacktofront)
							{
								if (numbtofpolys < MAX_BTOFPOLYS)
								{
									pbtofpolys[numbtofpolys].clipflags =
											clipflags;
									pbtofpolys[numbtofpolys].psurf = surf;
									numbtofpolys++;
								}
							}
							else
							{
								R_RenderPoly (surf, clipflags);
							}
						}
						else
						{
							R_RenderFace (surf, clipflags);
						}
					}

					surf++;
				} while (--c);
			}
#ifdef QOG_WORLDNODE_FAST
			else if (facing > 0)
#else
			else if (dot > QOG_BFEPS)
#endif
			{
				do
				{
					if (!(surf->flags & SURF_PLANEBACK) &&
						SURF_VISFRAME_IS (cl.worldmodel, surf, r_framecount))	// QuakeOG
					{
						if (r_drawpolys)
						{
							if (r_worldpolysbacktofront)
							{
								if (numbtofpolys < MAX_BTOFPOLYS)
								{
									pbtofpolys[numbtofpolys].clipflags =
											clipflags;
									pbtofpolys[numbtofpolys].psurf = surf;
									numbtofpolys++;
								}
							}
							else
							{
								R_RenderPoly (surf, clipflags);
							}
						}
						else
						{
							R_RenderFace (surf, clipflags);
						}
					}

					surf++;
				} while (--c);
			}

		// all surfaces on the same node share the same sequence number
			r_currentkey++;
		}

	// recurse down the back side
		R_RecursiveWorldNode (node->children[!side], clipflags);
	}
}



/*
================
R_RenderWorld
================
*/
void R_RenderWorld (void)
{
	int			i;
	model_t		*clmodel;
#if MAX_BTOFPOLYS > 0
	btofpoly_t	btofpolys[MAX_BTOFPOLYS];

	pbtofpolys = btofpolys;
#else
	// QuakeOG: back to front list compiled out (r_local.h)
	if (r_worldpolysbacktofront)
		Sys_Error ("R_RenderWorld: back to front polygons need QOG_MAX_BTOFPOLYS");
	pbtofpolys = NULL;
#endif

	currententity = &cl_entities[0];
	VectorCopy (r_origin, modelorg);
	clmodel = currententity->model;
	r_pcurrentvertbase = clmodel->vertexes;

	R_RecursiveWorldNode (clmodel->nodes, 15);

// if the driver wants the polygons back to front, play the visible ones back
// in that order
	if (r_worldpolysbacktofront)
	{
		for (i=numbtofpolys-1 ; i>=0 ; i--)
		{
			R_RenderPoly (pbtofpolys[i].psurf, pbtofpolys[i].clipflags);	// QuakeOG: was btofpolys
		}
	}
}


