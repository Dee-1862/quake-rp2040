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

#ifndef __MODEL__
#define __MODEL__

#include "modelgen.h"
#include "spritegn.h"

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vec3_t		position;
} mvertex_t;

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2


// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct mplane_s
{
	vec3_t	normal;
	float	dist;
	byte	type;			// for texture axis selection and fast side tests
	byte	signbits;		// signx + signy<<1 + signz<<1
	byte	pad[2];
} mplane_t;

typedef struct texture_s
{
	char		name[16];
	unsigned	width, height;
	int			anim_total;				// total tenths in sequence ( 0 = no)
	int			anim_min, anim_max;		// time for this frame min <=time< max
	struct texture_s *anim_next;		// in the animation sequence
	struct texture_s *alternate_anims;	// bmodels in frmae 1 use these
	byte		*mips[MIPLEVELS];		// QuakeOG: pixels of the four mip levels, in RAM or read in place from flash
} texture_t;


#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWSPRITE		8
#define SURF_DRAWTURB		0x10
#define SURF_DRAWTILED		0x20
#define SURF_DRAWBACKGROUND	0x40

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	unsigned short	v[2];
	// QuakeOG: cachededgeoffset moved to model_t.edge_cache (RAM), so medge_t
	// matches the file's dedge_t and can be read in place from flash
} medge_t;

typedef struct
{
	float		vecs[2][4];
	float		mipadjust;
	texture_t	*texture;
	int			flags;
} mtexinfo_t;

typedef struct msurface_s
{
	// QuakeOG: visframe, dlightframe and dlightbits moved to RAM arrays in
	// model_t (SURF_VISFRAME and friends) so surfaces can be read only

#ifdef QOG_SURF_INDEX
	// QuakeOG: the file's 16-bit plane and texinfo numbers instead of
	// pointers, use SURF_PLANE and SURF_TEXINFO with the owning model
	unsigned short	planenum;
	unsigned short	texinfonum;
#else
	mplane_t	*plane;
#endif
#ifdef QOG_SURF_PACKED
	// QuakeOG: flags fit 16 bits (SURF_* up to 0x40) and numedges is the
	// file's 16-bit count, so the two share one word (msurface_t 36 to 32 B)
	unsigned short	flags;
	short		numedges;	// are backwards edges

	int			firstedge;	// look up in model->surfedges[], negative numbers
#else
	int			flags;

	int			firstedge;	// look up in model->surfedges[], negative numbers
	int			numedges;	// are backwards edges
#endif

#ifdef QOG_SURFCACHE
// surface generation data
	struct surfcache_s	*cachespots[MIPLEVELS];
#endif

	short		texturemins[2];
	short		extents[2];

#ifndef QOG_SURF_INDEX
	mtexinfo_t	*texinfo;
#endif
	
// lighting info
	byte		styles[MAXLIGHTMAPS];
	byte		*samples;		// [numstyles*surfsize]
} msurface_t;

// QuakeOG: a surface's plane and texinfo; m is the model that owns it
#ifdef QOG_SURF_INDEX
#define SURF_PLANE(m, s)	((m)->planes + (s)->planenum)
#define SURF_TEXINFO(m, s)	((m)->texinfo + (s)->texinfonum)
#else
#define SURF_PLANE(m, s)	((s)->plane)
#define SURF_TEXINFO(m, s)	((s)->texinfo)
#endif

// QuakeOG: with QOG_MARKSURF16 a mark surface is the file's 16-bit surface
// number (read in place from flash when possible) instead of a pointer.
// The file format stores 16-bit numbers, so no map loses anything.
#ifdef QOG_MARKSURF16
typedef unsigned short	marksurf_t;
#define MARKSURF_NUM(m, mk)	(*(mk))
#else
typedef struct msurface_s	*marksurf_t;
#define MARKSURF_NUM(m, mk)	(*(mk) - (m)->surfaces)
#endif

typedef struct mnode_s
{
// common with leaf
	int			contents;		// 0, to differentiate from leafs
	// QuakeOG: visframe moved to model_t (NODE_VISFRAME)

	short		minmaxs[6];		// for bounding box culling

	struct mnode_s	*parent;

// node specific
	mplane_t	*plane;
	struct mnode_s	*children[2];	

	unsigned short		firstsurface;
	unsigned short		numsurfaces;
} mnode_t;



typedef struct mleaf_s
{
// common with node
	int			contents;		// wil be a negative contents number
	// QuakeOG: visframe moved to model_t (NODE_VISFRAME)

	short		minmaxs[6];		// for bounding box culling

	struct mnode_s	*parent;

// leaf specific
	byte		*compressed_vis;
	// QuakeOG: efrags and key moved to model_t (LEAF_EFRAGS, LEAF_KEY)

#ifdef QOG_LEAF_PACKED
	// QuakeOG: the file's 16-bit first index and count (mleaf_t 36 to 32 B),
	// use LEAF_MARKSURFACES
	unsigned short	firstmarksurface;
	unsigned short	nummarksurfaces;
#else
	marksurf_t	*firstmarksurface;	// QuakeOG: see marksurf_t
	int			nummarksurfaces;
#endif
	byte		ambient_sound_level[NUM_AMBIENTS];
} mleaf_t;

// QuakeOG: a leaf's first mark surface in model m
#ifdef QOG_LEAF_PACKED
#define LEAF_MARKSURFACES(m, l)	((m)->marksurfaces + (l)->firstmarksurface)
#else
#define LEAF_MARKSURFACES(m, l)	((l)->firstmarksurface)
#endif

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct
{
	dclipnode_t	*clipnodes;
	mplane_t	*planes;
	int			firstclipnode;
	int			lastclipnode;
	vec3_t		clip_mins;
	vec3_t		clip_maxs;
} hull_t;

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/


// FIXME: shorten these?
typedef struct mspriteframe_s
{
	int		width;
	int		height;
	void	*pcachespot;			// remove?
	float	up, down, left, right;
	byte	pixels[4];
} mspriteframe_t;

typedef struct
{
	int				numframes;
	float			*intervals;
	mspriteframe_t	*frames[1];
} mspritegroup_t;

typedef struct
{
	spriteframetype_t	type;
	mspriteframe_t		*frameptr;
} mspriteframedesc_t;

typedef struct
{
	int					type;
	int					maxwidth;
	int					maxheight;
	int					numframes;
	float				beamlength;		// remove?
	void				*cachespot;		// remove?
	mspriteframedesc_t	frames[1];
} msprite_t;


/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

typedef struct
{
	aliasframetype_t	type;
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
	char				name[16];
} maliasframedesc_t;

typedef struct
{
	aliasskintype_t		type;
	void				*pcachespot;
	int					skin;
} maliasskindesc_t;

typedef struct
{
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
} maliasgroupframedesc_t;

typedef struct
{
	int						numframes;
	int						intervals;
	maliasgroupframedesc_t	frames[1];
} maliasgroup_t;

typedef struct
{
	int					numskins;
	int					intervals;
	maliasskindesc_t	skindescs[1];
} maliasskingroup_t;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct mtriangle_s {
	int					facesfront;
	int					vertindex[3];
} mtriangle_t;

typedef struct {
	int					model;
	int					stverts;
	int					skindesc;
	int					triangles;
	// QuakeOG: when the model was loaded from flash, the bulk data (frame
	// vertices, skin pixels, triangles) stays there and those offsets are
	// relative to flashbase, an absolute pointer that stays valid when the
	// cache moves this header. NULL: everything is relative to the header.
	byte				*flashbase;
	maliasframedesc_t	frames[1];
} aliashdr_t;

// QuakeOG: address of frame vertices, skin pixels or triangles
#define ALIAS_BULK(hdr, ofs) \
	(((hdr)->flashbase ? (hdr)->flashbase : (byte *)(hdr)) + (ofs))

//===================================================================

//
// Whole model
//

typedef enum {mod_brush, mod_sprite, mod_alias} modtype_t;

#define	EF_ROCKET	1			// leave a trail
#define	EF_GRENADE	2			// leave a trail
#define	EF_GIB		4			// leave a trail
#define	EF_ROTATE	8			// rotate (bonus items)
#define	EF_TRACER	16			// green split trail
#define	EF_ZOMGIB	32			// small blood trail
#define	EF_TRACER2	64			// orange split trail + rotate
#define	EF_TRACER3	128			// purple trail

typedef struct model_s
{
	char		name[MAX_QPATH];
	qboolean	needload;		// bmodels and sprites don't cache normally

	modtype_t	type;
	int			numframes;
	synctype_t	synctype;
	
	int			flags;

//
// volume occupied by the model
//		
	vec3_t		mins, maxs;
	float		radius;

//
// brush model
//
	int			firstmodelsurface, nummodelsurfaces;

	int			numsubmodels;
	dmodel_t	*submodels;

	int			numplanes;
	mplane_t	*planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t		*leafs;

	int			numvertexes;
	mvertex_t	*vertexes;

	int			numedges;
	medge_t		*edges;

	int			numnodes;
	mnode_t		*nodes;

	int			numtexinfo;
	mtexinfo_t	*texinfo;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			numsurfedges;
	int			*surfedges;

	int			numclipnodes;
	dclipnode_t	*clipnodes;

	int			nummarksurfaces;
	marksurf_t	*marksurfaces;		// QuakeOG: see marksurf_t

	hull_t		hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t	**textures;

	byte		*visdata;
	byte		*lightdata;
	char		*entities;

//
// additional model data
//
	cache_user_t	cache;		// only access through Mod_Extradata

// QuakeOG: the per-frame state id kept inside the BSP structures, moved out
// so those structures can be read only (flash). Indexed like the arrays
// above; inline submodels share their world's arrays, as they share its
// surfaces, nodes and leafs. Use the accessor macros below.
	int				*surf_visframe;		// [numsurfaces]
	int				*surf_dlightframe;	// [numsurfaces]
	int				*surf_dlightbits;	// [numsurfaces]
	int				*node_visframe;		// [numnodes]
	int				*leaf_visframe;		// [numleafs]
	efrag_t			**leaf_efrags;		// [numleafs]
	int				*leaf_key;			// [numleafs]
#ifdef QOG_EDGECACHE16
	unsigned short	*edge_cache;		// [numedges] 16-bit form, see r_draw.c
#else
	unsigned int	*edge_cache;		// [numedges] id's cachededgeoffset
#endif

} model_t;

#define SURF_VISFRAME(m, s)		((m)->surf_visframe[(s) - (m)->surfaces])
#define SURF_DLIGHTFRAME(m, s)	((m)->surf_dlightframe[(s) - (m)->surfaces])
#define SURF_DLIGHTBITS(m, s)	((m)->surf_dlightbits[(s) - (m)->surfaces])
#define LEAF_EFRAGS(m, l)		((m)->leaf_efrags[(l) - (m)->leafs])
#define LEAF_KEY(m, l)			((m)->leaf_key[(l) - (m)->leafs])
#define EDGE_CACHE(m, e)		((m)->edge_cache[(e) - (m)->edges])
// QuakeOG: first frame of the 16-bit edge cache's window, an int stored just
// before the array so inline submodels share it with their world
#define EDGE_CACHE_START(m)		(((int *)(m)->edge_cache)[-1])
// nodes and leafs share id's node pointer type; contents < 0 means a leaf
#define NODE_VISFRAME(m, n)		(*((n)->contents < 0 \
	? &(m)->leaf_visframe[(mleaf_t *)(n) - (m)->leafs] \
	: &(m)->node_visframe[(n) - (m)->nodes]))

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, qboolean crash);
void	*Mod_Extradata (model_t *mod);	// handles caching
void	Mod_TouchModel (char *name);

mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
byte	*Mod_LeafPVS (mleaf_t *leaf, model_t *model);

#endif	// __MODEL__
