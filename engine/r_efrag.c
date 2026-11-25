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
// r_efrag.c

#include "quakedef.h"
#include "r_local.h"
#include "qog_bmodel.h"	// QuakeOG (Agent AS): board cost model counts (QOG_BOARDMODEL)
#include "qog_aqstats.h"	// QuakeOG (Agent AQ): front end work counts (QOG_AQ_STATS)

mnode_t	*r_pefragtopnode;
#ifdef QOG_ENTITY_NOTOPNODE
mnode_t	*r_entitytopnode;	// QuakeOG (Agent AD): the top node of the bmodel being drawn (render.h)
#endif


//===========================================================================

/*
===============================================================================

					ENTITY FRAGMENT FUNCTIONS

===============================================================================
*/

efrag_t		**lastlink;

vec3_t		r_emins, r_emaxs;

entity_t	*r_addent;


/*
================
R_RemoveEfrags

Call when removing an object from the world or moving it to another position
================
*/
void R_RemoveEfrags (entity_t *ent)
{
	efrag_t		*ef, *old, *walk, **prev;
	
	ef = ent->efrag;
	
	while (ef)
	{
		prev = &LEAF_EFRAGS (cl.worldmodel, ef->leaf);
		while (1)
		{
			walk = *prev;
			if (!walk)
				break;
			if (walk == ef)
			{	// remove this fragment
				*prev = ef->leafnext;
				break;
			}
			else
				prev = &walk->leafnext;
		}
				
		old = ef;
		ef = ef->entnext;
		
	// put it on the free list
		old->entnext = cl.free_efrags;
		cl.free_efrags = old;
	}
	
	ent->efrag = NULL; 
}

/*
===================
R_SplitEntityOnNode
===================
*/
void R_SplitEntityOnNode (mnode_t *node)
{
	efrag_t		*ef;
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;
	
	if (node->contents == CONTENTS_SOLID)
	{
		return;
	}
	
// add an efrag if the node is a leaf

	if ( node->contents < 0)
	{
		if (!r_pefragtopnode)
			r_pefragtopnode = node;

		leaf = (mleaf_t *)node;

// grab an efrag off the free list
		ef = cl.free_efrags;
		if (!ef)
		{
			Con_Printf ("Too many efrags!\n");
			return;		// no free fragments...
		}
		cl.free_efrags = cl.free_efrags->entnext;

		ef->entity = r_addent;
		
// add the entity link	
		*lastlink = ef;
		lastlink = &ef->entnext;
		ef->entnext = NULL;
		
// set the leaf links
		ef->leaf = leaf;
#ifdef QOG_LEAF16
		R_EfragLeafSlot (leaf);	// QuakeOG: give the leaf a head
#endif
		ef->leafnext = LEAF_EFRAGS (cl.worldmodel, leaf);
		LEAF_EFRAGS (cl.worldmodel, leaf) = ef;
			
		return;
	}
	
// NODE_MIXED

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);
	
	if (sides == 3)
	{
	// split on this plane
	// if this is the first splitter of this bmodel, remember it
		if (!r_pefragtopnode)
			r_pefragtopnode = node;
	}
	
// recurse down the contacted sides
	if (sides & 1)
		R_SplitEntityOnNode (node->children[0]);
		
	if (sides & 2)
		R_SplitEntityOnNode (node->children[1]);
}


#ifdef QOG_BMSPLIT_INT
/*
QuakeOG (Agent AQ): BOX_ON_PLANE_SIDE on an axial plane compares two floats
twice. For floats that are neither NaN nor denormal, a <= b is the integer
compare of their bits mapped to the order of the real numbers (both zeros
as 0), so no float call is needed. Denormals (the board's float routines
take them as zero) and NaNs take the float compares.
*/
#if defined(__GNUC__)
typedef unsigned int __attribute__((may_alias))	r_qogebits_t;
#else
typedef unsigned int							r_qogebits_t;
#endif

static int R_QogEKey (unsigned b)
{
	return (b & 0x80000000) ? -(int)(b & 0x7FFFFFFF) : (int)b;
}

static int R_QogEPlain (unsigned b)
{
	unsigned	e = b & 0x7F800000;

	return e != 0x7F800000 && (e || !(b & 0x007FFFFF));
}

static int R_QogBoxAxialSides (mplane_t *p)
{
	unsigned	d = *(r_qogebits_t *)&p->dist;
	unsigned	lo = *(r_qogebits_t *)&r_emins[p->type];
	unsigned	hi = *(r_qogebits_t *)&r_emaxs[p->type];

	if (!R_QogEPlain (d) || !R_QogEPlain (lo) || !R_QogEPlain (hi))
		return BOX_ON_PLANE_SIDE (r_emins, r_emaxs, p);
	if (R_QogEKey (d) <= R_QogEKey (lo))
		return 1;
	if (R_QogEKey (d) >= R_QogEKey (hi))
		return 2;
	return 3;
}
#endif

/*
===================
R_SplitEntityOnNode2
===================
*/
void R_SplitEntityOnNode2 (mnode_t *node)
{
	mplane_t	*splitplane;
	int			sides;

	QOG_AQ (AQ_BSPLIT, 1);
	if (!NODE_VISFRAME_IS (cl.worldmodel, node, r_visframecount))	// QuakeOG
		return;
	
	if (node->contents < 0)
	{
		if (node->contents != CONTENTS_SOLID)
			r_pefragtopnode = node; // we've reached a non-solid leaf, so it's
									//  visible and not BSP clipped
		return;
	}
	
	splitplane = node->plane;
#ifdef QOG_BMSPLIT_INT
	if (splitplane->type < 3)
	{
		QOG_BM (QOG_BM_BMSPLITINT, 1);	// QuakeOG (Agent AQ)
		sides = R_QogBoxAxialSides (splitplane);
	}
	else
	{
		QOG_BM (QOG_BM_BMSPLIT, 1);	// QuakeOG (Agent AS)
		sides = BoxOnPlaneSide (r_emins, r_emaxs, splitplane);
	}
#else
	QOG_BM (QOG_BM_BMSPLIT, 1);	// QuakeOG (Agent AS)
	sides = BOX_ON_PLANE_SIDE(r_emins, r_emaxs, splitplane);
#endif
	
	if (sides == 3)
	{
	// remember first splitter
		r_pefragtopnode = node;
		return;
	}
	
// not split yet; recurse down the contacted side
	if (sides & 1)
		R_SplitEntityOnNode2 (node->children[0]);
	else
		R_SplitEntityOnNode2 (node->children[1]);
}


/*
===========
R_AddEfrags
===========
*/
void R_AddEfrags (entity_t *ent)
{
	model_t		*entmodel;
	int			i;
		
	if (!ent->model)
		return;

	if (ent == cl_entities)
		return;		// never add the world

	r_addent = ent;
			
	lastlink = &ent->efrag;
	r_pefragtopnode = NULL;
	
	entmodel = ent->model;

	for (i=0 ; i<3 ; i++)
	{
		r_emins[i] = ent->origin[i] + entmodel->mins[i];
		r_emaxs[i] = ent->origin[i] + entmodel->maxs[i];
	}

	R_SplitEntityOnNode (cl.worldmodel->nodes);

#ifndef QOG_ENTITY_NOTOPNODE	// QuakeOG (Agent AD): never read, see ENT_TOPNODE in render.h
	ent->topnode = r_pefragtopnode;
#endif
}


/*
================
R_StoreEfrags

// FIXME: a lot of this goes away with edge-based
================
*/
void R_StoreEfrags (efrag_t **ppefrag)
{
	entity_t	*pent;
	model_t		*clmodel;
	efrag_t		*pefrag;


	while ((pefrag = *ppefrag) != NULL)
	{
		pent = pefrag->entity;
		clmodel = pent->model;

		switch (clmodel->type)
		{
		case mod_alias:
		case mod_brush:
		case mod_sprite:
			pent = pefrag->entity;

			if ((pent->visframe != r_framecount) &&
				(cl_numvisedicts < MAX_VISEDICTS))
			{
				QOG_AQ (AQ_EFRAGS, 1);
				cl_visedicts[cl_numvisedicts++] = pent;

			// mark that we've recorded this entity for this frame
				pent->visframe = r_framecount;
			}

			ppefrag = &pefrag->leafnext;
			break;

		default:	
			Sys_Error ("R_StoreEfrags: Bad entity type %d\n", clmodel->type);
		}
	}
}

#ifdef QOG_LEAF16
/*
================
R_EfragLeafSlot

QuakeOG: give a world leaf that is about to get an efrag its own list head,
or the shared last head once the others are taken (see efragheads_t).
================
*/
void R_EfragLeafSlot (mleaf_t *leaf)
{
	byte			*slot;
	efragheads_t	*h;

	slot = &MODFRAME (cl.worldmodel)->leaf_efrags[leaf - cl.worldmodel->leafs];
	if (*slot)
		return;
	h = EFRAG_HDR (MODFRAME (cl.worldmodel)->leaf_efrags);
	if (h->used < QOG_EFRAG_HEADS)
		h->used++;
	*slot = h->used - 1;	// the last head, once reached, is shared
#ifdef QOG_MAPFRAME_STATS
	{
		static int	peak;

		if (h->used > peak)
		{
			peak = h->used;
			Sys_Printf ("qog_l: efrag heads %d (%s)\n", peak, cl.worldmodel->name);
		}
	}
#endif
}

/*
================
R_StoreLeafEfrags

QuakeOG: R_StoreEfrags for one leaf. In the shared head's list it passes
over the efrags of other leafs, the leaf's own come in id's order.
================
*/
void R_StoreLeafEfrags (mleaf_t *leaf)
{
	byte		*slots;
	efrag_t		*pefrag;
	entity_t	*pent;

	slots = MODFRAME (cl.worldmodel)->leaf_efrags;
	if (slots[leaf - cl.worldmodel->leafs] != QOG_EFRAG_HEADS - 1)
	{
		R_StoreEfrags (&LEAF_EFRAGS (cl.worldmodel, leaf));
		return;
	}
	for (pefrag = LEAF_EFRAGS (cl.worldmodel, leaf) ; pefrag ; pefrag = pefrag->leafnext)
	{
		if (pefrag->leaf != leaf)
			continue;
		pent = pefrag->entity;
		switch (pent->model->type)
		{
		case mod_alias:
		case mod_brush:
		case mod_sprite:
			if ((pent->visframe != r_framecount) &&
				(cl_numvisedicts < MAX_VISEDICTS))
			{
				QOG_AQ (AQ_EFRAGS, 1);
				cl_visedicts[cl_numvisedicts++] = pent;
				pent->visframe = r_framecount;
			}
			break;

		default:
			Sys_Error ("R_StoreLeafEfrags: Bad entity type %d\n", pent->model->type);
		}
	}
}
#endif


