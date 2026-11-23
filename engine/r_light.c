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
// r_light.c

#include "quakedef.h"
#include "r_local.h"

int	r_dlightframecount;


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (void)
{
	int			i,j,k;
	
//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int)(cl.time*10);
	for (j=0 ; j<MAX_LIGHTSTYLES ; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256;
			continue;
		}
		k = i % cl_lightstyle[j].length;
		k = cl_lightstyle[j].map[k] - 'a';
		k = k*22;
		d_lightstylevalue[j] = k;
	}	
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

#ifdef QOG_DLIGHT_SPARSE
/*
=============================================================================

QuakeOG: QOG_DLIGHT_SPARSE keeps id's dlightbits only for the surfaces a
light reaches in this frame, not as an int for every surface of the map.
The world's dlightframe bits (DS_IS) say which surfaces are lit, and a
small open hash maps a lit surface's number to its light bits. The hash
belongs to the counter of those bits and is emptied with them.

R_PushDlights only notes the world pass. It runs at the first question
about a lit surface in the frame (R_DlightLit), after R_RenderWorld has
stamped the visible surfaces, and marks only the world surfaces visible in
this frame. No other surface is ever drawn, so no drawn surface sees a
difference, and a frame lights far fewer surfaces (1,918 on e3m6 in the
fight tour with every surface in reach, see docs/changes.md). The brush
model pass in R_DrawBEntitiesOnList marks as id's does.

If the hash ever fills, the bits it could not take are remembered per light
(dl_lost), and R_DlightBits finds them again by walking the tree as
R_MarkLights did, with the same arithmetic, so the result is always id's.

=============================================================================
*/

#ifndef QOG_DLIGHT_SURFS
#define QOG_DLIGHT_SURFS	1024	// a power of two, one entry stays empty
#endif
#define DL_EMPTY			0xFFFF	// surface numbers are below this (model.c)
#define DL_HASH(n)			(((unsigned)(n) * 40503u) & (QOG_DLIGHT_SURFS - 1))

static unsigned short	dl_surf[QOG_DLIGHT_SURFS];
static int				dl_bits[QOG_DLIGHT_SURFS];
static int				dl_used;		// entries taken
static int				dl_lost;		// lights with a surface the hash could not take
static qboolean			dl_pending;		// the world pass waits for the first question
static int				dl_pendframe;	// its counter
static qboolean			dl_visonly;		// it marks only the surfaces visible now

#ifdef QOG_MAPFRAME_STATS
static int				dl_lit, dl_peaklit;	// lit surfaces in this frame, most seen
#endif

/*
=============
R_DlightMark

QuakeOG: id's marking of one world surface with a light's bit
=============
*/
static void R_DlightMark (msurface_t *surf, int bit)
{
	framebits_t	*lit;
	int			n, h;

	lit = MODFRAME (cl.worldmodel)->surf_dlightframe;
	n = surf - cl.worldmodel->surfaces;
	if (dl_visonly && !SURF_VISFRAME_IS (cl.worldmodel, surf, r_framecount))
		return;		// not drawn in this frame
	if (FB_TAG (lit) != r_dlightframecount)
	{
		FB_Clear (lit, r_dlightframecount);
		memset (dl_surf, 0xFF, sizeof(dl_surf));
		dl_used = 0;
		dl_lost = 0;
#ifdef QOG_MAPFRAME_STATS
		dl_lit = 0;
#endif
	}
	if (!(lit[n >> 3] & (1 << (n & 7))))
	{
		lit[n >> 3] |= (framebits_t)(1 << (n & 7));
#ifdef QOG_MAPFRAME_STATS
		if (++dl_lit > dl_peaklit)
		{
			dl_peaklit = dl_lit;
			Sys_Printf ("qog_l: lit surfaces %d (%s)\n", dl_peaklit, cl.worldmodel->name);
		}
#endif
	}
	for (h = DL_HASH (n) ; dl_surf[h] != DL_EMPTY ; h = (h + 1) & (QOG_DLIGHT_SURFS - 1))
	{
		if (dl_surf[h] == n)
		{
			dl_bits[h] |= bit;
			return;
		}
	}
	if (dl_used < QOG_DLIGHT_SURFS - 1)
	{
		dl_surf[h] = (unsigned short)n;
		dl_bits[h] = bit;
		dl_used++;
		return;
	}
	dl_lost |= bit;
}

/*
=============
R_LightReaches

QuakeOG: whether R_MarkLights, started at node, marks surface number n
=============
*/
static qboolean R_LightReaches (dlight_t *light, mnode_t *node, int n)
{
	mplane_t	*splitplane;
	float		dist;

	while (node->contents >= 0)
	{
		splitplane = node->plane;
		dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;

		if (dist > light->radius)
		{
			node = node->children[0];
			continue;
		}
		if (dist < -light->radius)
		{
			node = node->children[1];
			continue;
		}
		if (n >= node->firstsurface && n < node->firstsurface + node->numsurfaces)
			return true;
		if (R_LightReaches (light, node->children[0], n))
			return true;
		node = node->children[1];
	}
	return false;
}

/*
=============
R_DlightRunPending

QuakeOG: the world pass R_PushDlights noted. In the frame it was noted for
it marks only the surfaces R_RenderWorld found visible, otherwise all, as
id's does.
=============
*/
static void R_DlightRunPending (void)
{
	int			i, saved;
	dlight_t	*l;

	dl_pending = false;
	saved = r_dlightframecount;
	r_dlightframecount = dl_pendframe;
	dl_visonly = (r_framecount == dl_pendframe);
	l = cl_dlights;
	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		R_MarkLights ( l, 1<<i, cl.worldmodel->nodes );
	}
	dl_visonly = false;
	r_dlightframecount = saved;
}

/*
=============
R_DlightLit

QuakeOG: id's test of a surface's dlightframe against counter c
=============
*/
qboolean R_DlightLit (model_t *m, msurface_t *surf, int c)
{
	if (dl_pending)
		R_DlightRunPending ();
	return DS_IS (MODFRAME (m)->surf_dlightframe, surf - m->surfaces, c);
}

/*
=============
R_DlightBits

QuakeOG: id's dlightbits of a surface of model m in this frame. Only the
world's bits are ever set, an item box map's never are, as in id.
=============
*/
int R_DlightBits (model_t *m, msurface_t *surf)
{
	int		n, h, bits, k;

	n = surf - m->surfaces;
	if (!R_DlightLit (m, surf, r_framecount))
		return 0;
	bits = 0;
	for (h = DL_HASH (n) ; dl_surf[h] != DL_EMPTY ; h = (h + 1) & (QOG_DLIGHT_SURFS - 1))
	{
		if (dl_surf[h] == n)
		{
			bits = dl_bits[h];
			break;
		}
	}
	if (dl_lost & ~bits)
	{
		// a lost bit came from R_PushDlights on the world's tree or from
		// R_DrawBEntitiesOnList on the tree of the brush model drawn now
		for (k=0 ; k<MAX_DLIGHTS ; k++)
		{
			if (!(dl_lost & ~bits & (1<<k)))
				continue;
			if (R_LightReaches (&cl_dlights[k], cl.worldmodel->nodes, n)
				|| (m != cl.worldmodel && m->firstmodelsurface != 0
				&& R_LightReaches (&cl_dlights[k], m->nodes + m->hulls[0].firstclipnode, n)))
				bits |= 1<<k;
		}
	}
	return bits;
}
#endif	// QOG_DLIGHT_SPARSE

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	mplane_t	*splitplane;
	float		dist;
	msurface_t	*surf;
	int			i;
	
	if (node->contents < 0)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
	if (dist > light->radius)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -light->radius)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
#ifdef QOG_DLIGHT_SPARSE
		R_DlightMark (surf, bit);	// QuakeOG
#else
		// QuakeOG: id indexes the world's surfaces here even for brush
		// entities, so the world's arrays are the right ones
		if (!SURF_DLIGHTFRAME_IS (cl.worldmodel, surf, r_dlightframecount))	// QuakeOG
		{
			SURF_DLIGHTBITS (cl.worldmodel, surf) = 0;
			SURF_DLIGHTFRAME_SET (cl.worldmodel, surf, r_dlightframecount);
		}
		SURF_DLIGHTBITS (cl.worldmodel, surf) |= bit;
#endif
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
#ifdef QOG_DLIGHT_SPARSE
	// QuakeOG: the pass runs at the first question about a lit surface. A
	// pass noted for the same counter and not run yet is run now in full,
	// as id's marks of both passes add up. One for an older counter can go,
	// as id's marks for it never match again.
	if (dl_pending && dl_pendframe == r_dlightframecount)
		R_DlightRunPending ();
	dl_pending = true;
	dl_pendframe = r_dlightframecount;
	return;
#endif
	l = cl_dlights;

	for (i=0 ; i<MAX_DLIGHTS ; i++, l++)
	{
		if (l->die < cl.time || !l->radius)
			continue;
		R_MarkLights ( l, 1<<i, cl.worldmodel->nodes );
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

int RecursiveLightPoint (mnode_t *node, vec3_t start, vec3_t end)
{
	int			r;
	float		front, back, frac;
	int			side;
	mplane_t	*plane;
	vec3_t		mid;
	msurface_t	*surf;
	int			s, t, ds, dt;
	int			i;
	mtexinfo_t	*tex;
	byte		*lightmap;
	unsigned	scale;
	int			maps;

	if (node->contents < 0)
		return -1;		// didn't hit anything
	
// calculate mid point

// FIXME: optimize for axial
	plane = node->plane;
	front = DotProduct (start, plane->normal) - plane->dist;
	back = DotProduct (end, plane->normal) - plane->dist;
	side = front < 0;
	
	if ( (back < 0) == side)
		return RecursiveLightPoint (node->children[side], start, end);
	
	frac = front / (front-back);
	mid[0] = start[0] + (end[0] - start[0])*frac;
	mid[1] = start[1] + (end[1] - start[1])*frac;
	mid[2] = start[2] + (end[2] - start[2])*frac;
	
// go down front side	
	r = RecursiveLightPoint (node->children[side], start, mid);
	if (r >= 0)
		return r;		// hit something
		
	if ( (back < 0) == side )
		return -1;		// didn't hit anuthing
		
// check for impact on this node

	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (i=0 ; i<node->numsurfaces ; i++, surf++)
	{
		if (surf->flags & SURF_DRAWTILED)
			continue;	// no lightmaps

		tex = SURF_TEXINFO (cl.worldmodel, surf);	// QuakeOG
		
		s = DotProduct (mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct (mid, tex->vecs[1]) + tex->vecs[1][3];;

		if (s < surf->texturemins[0] ||
		t < surf->texturemins[1])
			continue;
		
		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];
		
		if ( ds > surf->extents[0] || dt > surf->extents[1] )
			continue;

		if (!surf->samples)
			return 0;

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		r = 0;
		if (lightmap)
		{

			lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
					maps++)
			{
				scale = d_lightstylevalue[surf->styles[maps]];
				r += *lightmap * scale;
				lightmap += ((surf->extents[0]>>4)+1) *
						((surf->extents[1]>>4)+1);
			}
			
			r >>= 8;
		}
		
		return r;
	}

// go down back side
	return RecursiveLightPoint (node->children[!side], mid, end);
}

int R_LightPoint (vec3_t p)
{
	vec3_t		end;
	int			r;
	
	if (!cl.worldmodel->lightdata)
		return 255;
	
	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;
	
	r = RecursiveLightPoint (cl.worldmodel->nodes, p, end);
	
	if (r == -1)
		r = 0;

	if (r < r_refdef.ambientlight)
		r = r_refdef.ambientlight;

	return r;
}

