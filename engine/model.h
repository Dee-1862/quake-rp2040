/* Changed by the QuakeOG project, 2026 (last change November 2026). The git history has the details. */
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
	byte		*mips[MIPLEVELS];		// QuakeOG, pixels of the four mip levels, in RAM or read in place from flash
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
	// QuakeOG, cachededgeoffset moved to model_t.edge_cache (RAM), so medge_t
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
	// QuakeOG, visframe, dlightframe and dlightbits moved to RAM arrays in
	// model_t (SURF_VISFRAME and friends) so surfaces can be read only

#ifdef QOG_SURF_INDEX
	// QuakeOG, the file's 16-bit plane and texinfo numbers instead of
	// pointers, use SURF_PLANE and SURF_TEXINFO with the owning model
	unsigned short	planenum;
	unsigned short	texinfonum;
#else
	mplane_t	*plane;
#endif
#ifdef QOG_SURF_PACKED
	// QuakeOG, flags fit 16 bits (SURF_* up to 0x40) and numedges is the
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

// QuakeOG, a surface's plane and texinfo. M is the model that owns it
#ifdef QOG_SURF_INDEX
#define SURF_PLANE(m, s)	((m)->planes + (s)->planenum)
#define SURF_TEXINFO(m, s)	((m)->texinfo + (s)->texinfonum)
#else
#define SURF_PLANE(m, s)	((s)->plane)
#define SURF_TEXINFO(m, s)	((s)->texinfo)
#endif

// QuakeOG, with QOG_MARKSURF16 a mark surface is the file's 16-bit surface
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
	// QuakeOG, visframe moved to model_t (NODE_VISFRAME)

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
	// QuakeOG, visframe moved to model_t (NODE_VISFRAME)

	short		minmaxs[6];		// for bounding box culling

	struct mnode_s	*parent;

// leaf specific
	byte		*compressed_vis;
	// QuakeOG, efrags and key moved to model_t (LEAF_EFRAGS, LEAF_KEY)

#ifdef QOG_LEAF_PACKED
	// QuakeOG, the file's 16-bit first index and count (mleaf_t 36 to 32 B),
	// use LEAF_MARKSURFACES
	unsigned short	firstmarksurface;
	unsigned short	nummarksurfaces;
#else
	marksurf_t	*firstmarksurface;	// QuakeOG, see marksurf_t
	int			nummarksurfaces;
#endif
	byte		ambient_sound_level[NUM_AMBIENTS];
} mleaf_t;

// QuakeOG, a leaf's first mark surface in model m
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
	// QuakeOG, when the model was loaded from flash, the bulk data (frame
	// vertices, skin pixels, triangles) stays there and those offsets are
	// relative to flashbase, an absolute pointer that stays valid when the
	// cache moves this header. NULL, everything is relative to the header.
	byte				*flashbase;
	maliasframedesc_t	frames[1];
} aliashdr_t;

// QuakeOG, address of frame vertices, skin pixels or triangles
#define ALIAS_BULK(hdr, ofs) \
	(((hdr)->flashbase ? (hdr)->flashbase : (byte *)(hdr)) + (ofs))

//===================================================================

//
// Whole model
//

typedef enum {mod_brush, mod_sprite, mod_alias} modtype_t;

// QuakeOG, id's per-frame stamps (a surface's or node's visframe, a
// surface's dlightframe) are only ever compared for equality with a counter
// that never goes back (r_framecount, r_visframecount, r_dlightframecount),
// so a stamp from an earlier value never matches again. With QOG_FRAME16 a
// stamp is 16 bits, counted from a base kept in front of its array with
// the array's length (FS_BASE, FS_COUNT). A write whose counter is a whole
// window past the base first refills the array with 0xFFFF, which never
// matches, and moves the base to that counter. Reads of a counter outside
// the window match nothing, as no stamp can hold it. A fresh array is all
// zero with base 0, exactly like id's zeroed int array. Results are
// identical to id's for every counter value. FS_IS and FS_SET evaluate
// their arguments more than once, pass plain expressions only.
#ifdef QOG_FRAME16
typedef unsigned short	framestamp_t;
#ifndef QOG_FRAME16_WINDOW		// at most 0xFFFF, smaller only to test the refill
#define QOG_FRAME16_WINDOW	0xFFFF
#endif
#define FS_BASE(a)		(((int *)(a))[-1])
#define FS_COUNT(a)		(((int *)(a))[-2])
#define FS_REL(a, c)	((unsigned)((c) - FS_BASE (a)))
#define FS_IS(a, i, c)	(FS_REL (a, c) < QOG_FRAME16_WINDOW && (a)[i] == FS_REL (a, c))
#define FS_SET(a, i, c)	((void)(FS_REL (a, c) >= QOG_FRAME16_WINDOW && (FS_Refill ((a), (c)), 1)), \
	(a)[i] = (framestamp_t)FS_REL (a, c))
void	FS_Refill (framestamp_t *stamps, int counter);
#else
typedef int				framestamp_t;
#define FS_IS(a, i, c)	((a)[i] == (c))
#define FS_SET(a, i, c)	((a)[i] = (c))
#endif

// QuakeOG, with QOG_FRAMEBITS (visframe stamps) and QOG_DLIGHT_SPARSE (the
// dlightframe stamp) a stamp array is one bit per entry plus one tag, the
// counter the bits belong to (FB_TAG, kept in front of the bits with their
// byte count FB_BYTES). Bit i set means id's stamp i equals the tag. A
// write with a counter other than the tag first clears every bit and takes
// that counter as the tag, as id's stamps from an older counter never match
// again. A fresh array has tag 0 and every bit set, like id's zeroed
// stamps. Every read uses the counter of the latest write or a later one
// (the counters only grow), so the results equal id's. FB_IS and FB_SET
// evaluate their arguments more than once, pass plain expressions only.
#if defined(QOG_FRAMEBITS) || defined(QOG_DLIGHT_SPARSE)
typedef unsigned char	framebits_t;
#define FB_TAG(a)		(((int *)(a))[-1])
#define FB_BYTES(a)		(((int *)(a))[-2])
#define FB_IS(a, i, c)	(FB_TAG (a) == (c) && ((a)[(i) >> 3] & (1 << ((i) & 7))))
#define FB_SET(a, i, c)	((void)(FB_TAG (a) != (c) && (FB_Clear ((a), (c)), 1)), \
	(a)[(i) >> 3] |= (framebits_t)(1 << ((i) & 7)))
void	FB_Clear (framebits_t *bits, int counter);
#endif

// QuakeOG, the type and accessors of the visframe stamps (surfaces, nodes,
// leafs) and of the dlightframe stamps
#ifdef QOG_FRAMEBITS
typedef framebits_t		visstamp_t;
#define VS_IS			FB_IS
#define VS_SET			FB_SET
#else
typedef framestamp_t	visstamp_t;
#define VS_IS			FS_IS
#define VS_SET			FS_SET
#endif
#ifdef QOG_DLIGHT_SPARSE
typedef framebits_t		dlstamp_t;
#define DS_IS			FB_IS
#define DS_SET			FB_SET
#else
typedef framestamp_t	dlstamp_t;
#define DS_IS			FS_IS
#define DS_SET			FS_SET
#endif

#ifdef QOG_LEAF16
// QuakeOG, with QOG_LEAF16 a leaf's efrag list head is one of the world's
// QOG_EFRAG_HEADS heads, kept in front of a byte per leaf that selects it
// (EFRAG_HDR). Byte 0 selects head 0, which stays NULL, a leaf gets its own
// head when it first gets an efrag (R_EfragLeafSlot). Once the heads run
// out, every further leaf shares the last head, and R_StoreLeafEfrags skips
// the efrags of other leafs in that list, so each leaf still sees its own
// efrags in id's order. LEAF_EFRAGS stays a pointer lvalue as in id.
#ifndef QOG_EFRAG_HEADS
#define QOG_EFRAG_HEADS		256		// at most 256, the last one shared
#endif
typedef struct
{
	struct efrag_s	*heads[QOG_EFRAG_HEADS];
	int				used;			// heads given out, head 0 included
} efragheads_t;
#define EFRAG_HDR(slots)	((efragheads_t *)(slots) - 1)
void	R_EfragLeafSlot (mleaf_t *leaf);		// r_efrag.c
void	R_StoreLeafEfrags (mleaf_t *leaf);
#endif

#define	EF_ROCKET	1			// leave a trail
#define	EF_GRENADE	2			// leave a trail
#define	EF_GIB		4			// leave a trail
#define	EF_ROTATE	8			// rotate (bonus items)
#define	EF_TRACER	16			// green split trail
#define	EF_ZOMGIB	32			// small blood trail
#define	EF_TRACER2	64			// orange split trail + rotate
#define	EF_TRACER3	128			// purple trail

#ifdef QOG_MOD_FLASH
// QuakeOG, with QOG_MOD_FLASH a baked model_t is used in place in the flash
// image, so it cannot hold the RAM pointers of the per-frame arrays (end of
// model_t). It holds an index into mod_frames (model.c) instead, 0 for a
// world map and its inline models, its own index for an item box map
// (maps/b_*.bsp) that is loaded next to the world.
typedef struct modframe_s
{
	visstamp_t		*surf_visframe;		// [numsurfaces]
	dlstamp_t		*surf_dlightframe;	// [numsurfaces]
	int				*surf_dlightbits;	// [numsurfaces], NULL with QOG_DLIGHT_SPARSE
	visstamp_t		*node_visframe;		// [numnodes]
	visstamp_t		*leaf_visframe;		// [numleafs]
#ifdef QOG_LEAF16
	byte			*leaf_efrags;		// [numleafs] head numbers, see efragheads_t
	unsigned short	*leaf_key;			// [numleafs]
#else
	efrag_t			**leaf_efrags;		// [numleafs]
	int				*leaf_key;			// [numleafs]
#endif
#ifdef QOG_EDGECACHE16
	unsigned short	*edge_cache;		// [numedges] 16-bit form, see r_draw.c
#else
	unsigned int	*edge_cache;		// [numedges] id's cachededgeoffset
#endif
} modframe_t;

// 0 the world, 1 to QOG_MOD_FRAMES_BAKED - 1 baked item boxes (numbered by
// qog_bake), the rest item boxes loaded the normal way
#define QOG_MOD_FRAMES_BAKED	16
#define QOG_MOD_FRAMES			32
extern modframe_t	mod_frames[QOG_MOD_FRAMES];
#define MODFRAME(m)		(&mod_frames[(m)->frameslot])
#else
typedef struct model_s	modframe_t;		// the arrays are in model_t itself
#define MODFRAME(m)		(m)
#endif

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
	marksurf_t	*marksurfaces;		// QuakeOG, see marksurf_t

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

// QuakeOG, the per-frame state id kept inside the BSP structures, moved out
// so those structures can be read only (flash). Indexed like the arrays
// above. Inline submodels share their world's arrays, as they share its
// surfaces, nodes and leafs. Use the accessor macros below.
#ifdef QOG_MOD_FLASH
	int				frameslot;			// QuakeOG, see modframe_t
#else
	visstamp_t		*surf_visframe;		// [numsurfaces]
	dlstamp_t		*surf_dlightframe;	// [numsurfaces]
	int				*surf_dlightbits;	// [numsurfaces], NULL with QOG_DLIGHT_SPARSE
	visstamp_t		*node_visframe;		// [numnodes]
	visstamp_t		*leaf_visframe;		// [numleafs]
#ifdef QOG_LEAF16
	byte			*leaf_efrags;		// [numleafs] head numbers, see efragheads_t
	unsigned short	*leaf_key;			// [numleafs]
#else
	efrag_t			**leaf_efrags;		// [numleafs]
	int				*leaf_key;			// [numleafs]
#endif
#ifdef QOG_EDGECACHE16
	unsigned short	*edge_cache;		// [numedges] 16-bit form, see r_draw.c
#else
	unsigned int	*edge_cache;		// [numedges] id's cachededgeoffset
#endif
#endif	// QOG_MOD_FLASH

} model_t;

// QuakeOG, MODFRAME(m) is the model itself, or its arrays in mod_frames
#define SURF_VISFRAME_IS(m, s, c)	VS_IS (MODFRAME (m)->surf_visframe, (s) - (m)->surfaces, c)
// QuakeOG, surface number n, as a leaf's mark surfaces give it
#define SURF_VISFRAME_SETNUM(m, n, c)	VS_SET (MODFRAME (m)->surf_visframe, n, c)
#define SURF_DLIGHTFRAME_SET(m, s, c)	DS_SET (MODFRAME (m)->surf_dlightframe, (s) - (m)->surfaces, c)
#ifdef QOG_DLIGHT_SPARSE
// QuakeOG, no dlightbits array, and the world's marks are made when first
// asked for, see R_DlightLit and R_DlightBits (r_light.c)
qboolean	R_DlightLit (model_t *m, msurface_t *surf, int c);
int		R_DlightBits (model_t *m, msurface_t *surf);
void	R_QogDlightSettle (void);	// QuakeOG, the waiting pass now, before two cores build grids
#define SURF_DLIGHTFRAME_IS(m, s, c)	R_DlightLit (m, s, c)
#else
#define SURF_DLIGHTFRAME_IS(m, s, c)	DS_IS (MODFRAME (m)->surf_dlightframe, (s) - (m)->surfaces, c)
#define SURF_DLIGHTBITS(m, s)	(MODFRAME (m)->surf_dlightbits[(s) - (m)->surfaces])
#endif
#ifdef QOG_LEAF16
#define LEAF_EFRAGS(m, l)		(EFRAG_HDR (MODFRAME (m)->leaf_efrags)->heads \
	[MODFRAME (m)->leaf_efrags[(l) - (m)->leafs]])
#define LEAF_KEY(m, l)			(MODFRAME (m)->leaf_key[(l) - (m)->leafs])
// QuakeOG, a frame's keys stay far below 0x10000 (one per node, leaf and
// drawn surface), the check makes sure a key is never silently cut
#define LEAF_KEY_SET(m, l, k)	((void)((k) <= 0xFFFF || (Mod_LeafKeyOverflow (), 0)), \
	LEAF_KEY (m, l) = (unsigned short)(k))
void	Mod_LeafKeyOverflow (void);
#else
#define LEAF_EFRAGS(m, l)		(MODFRAME (m)->leaf_efrags[(l) - (m)->leafs])
#define LEAF_KEY(m, l)			(MODFRAME (m)->leaf_key[(l) - (m)->leafs])
#define LEAF_KEY_SET(m, l, k)	(LEAF_KEY (m, l) = (k))
#endif
#define EDGE_CACHE(m, e)		(MODFRAME (m)->edge_cache[(e) - (m)->edges])
// QuakeOG, first frame of the 16-bit edge cache's window, an int stored just
// before the array so inline submodels share it with their world
#define EDGE_CACHE_START(m)		(((int *)MODFRAME (m)->edge_cache)[-1])
// nodes and leafs share id's node pointer type. Contents < 0 means a leaf
#define NODE_VISFRAME_IS(m, n, c)	((n)->contents < 0 \
	? VS_IS (MODFRAME (m)->leaf_visframe, (mleaf_t *)(n) - (m)->leafs, c) \
	: VS_IS (MODFRAME (m)->node_visframe, (n) - (m)->nodes, c))
#define NODE_VISFRAME_SET(m, n, c)	((n)->contents < 0 \
	? VS_SET (MODFRAME (m)->leaf_visframe, (mleaf_t *)(n) - (m)->leafs, c) \
	: VS_SET (MODFRAME (m)->node_visframe, (n) - (m)->nodes, c))

// QuakeOG, baked models (QOG_BAKE). The PC command qog_bake runs the loaders
// above on every map, alias model and sprite and writes what they build
// into one image, id1/qogbake.bin, with every pointer resolved for the flash
// address the image is mapped at. For a map that is everything in model_t
// play never writes (planes, textures, texinfo, surfaces, leafs, nodes,
// clip hulls, submodels), for an alias model the block id keeps in the
// cache, for a sprite its frames. Pointers into the packs (light, vis,
// texture pixels, edges, vertexes, alias frames) are kept as they are, so
// an image only fits the packs it was baked against. A load then copies
// the baked model_t, and a map allocates only the per-frame arrays above.
// Layout in docs/game-data.md. Little-endian, 32-bit.
#define QOGB_MAGIC		0x42474F51	// "QOGB"
#define QOGB_VERSION	2

typedef struct
{
	unsigned	magic;
	unsigned	version;
	unsigned	layout;		// hash of this build's structure layout, see Mod_BakeLayout
	unsigned	base;		// flash address the image is built for
	unsigned	size;		// bytes, this header included
	unsigned	nummaps;	// directory entries (maps, alias models and sprites)
	unsigned	dirofs;		// directory, from base
	unsigned	pad[9];
} qogbake_header_t;			// 64 bytes

typedef struct
{
	char		name[40];	// model name, "maps/e1m1.bsp", "progs/player.mdl"
	unsigned	model;		// address of the baked model_t, the start of the model's block
	unsigned	size;		// bytes in the model's block
	unsigned	fileaddr;	// flash address and length of the file it was baked from
	unsigned	filelen;
	unsigned	filehash;	// FNV-1a of a map's header and plane lump, or a model's first 4 KB
	unsigned	pad;
} qogbake_entry_t;			// 64 bytes

// QuakeOG, QOG_BAKE_STUBS. qog_bake run with -bakestubs copies everything a
// map's block points at in the pack (light, vis, entities, vertexes, edges,
// surface edges, clip nodes, mark surfaces, texture pixels) into the image,
// each distinct texture's pixels once for all maps. The map file is then
// not read at all, and tools/mkboard.py replaces it in the board's pak with
// this stub. Its directory entry has fileaddr 0 (any address) and the
// stub's length and FNV-1a hash. The lumps stay as in id's header, since
// the leaf count is read from it (Mod_FinishBrushModel).
#define QOGB_STUB_IDENT	0x53474F51	// "QOGS", where a map has its version

typedef struct
{
	int			ident;		// QOGB_STUB_IDENT
	lump_t		lumps[HEADER_LUMPS];	// the map's header, unchanged
	unsigned	filelen;	// length and Mod_FileHash of the map it replaces
	unsigned	filehash;
} qogbake_stub_t;			// 132 bytes

// QuakeOG, the server's image for the main chip of the split game.
// qog_bake -bakestubs -bakeserver keeps what the server reads of a map
// (planes, leafs, nodes, clip nodes and hulls, submodels, entities,
// visibility) and leaves out textures, texinfo, surfaces, edges, vertexes,
// mark surfaces and light, which only the client draws. An alias model or
// sprite is its model_t alone (type, bounds, frame count, flags), with no
// cache data. Every model file in the server's pak is then a stub, a map
// the qogbake_stub_t above, an alias model or sprite this 16-byte one. The
// image's layout word is the build's layout XOR QOGB_SERVER_LAYOUT, so only
// a server half (QOG_SPLIT_SERVER) uses it. tools/mkboard.py --main.
#define QOGB_SERVER_LAYOUT	0x53525652	// "RVRS"

typedef struct
{
	int			ident;		// QOGB_STUB_IDENT
	int			fileident;	// IDPOLYHEADER or IDSPRITEHEADER of the file it replaces
	unsigned	filelen;	// length and Mod_FileHash of that file
	unsigned	filehash;
} qogbake_mstub_t;			// 16 bytes

//============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
model_t *Mod_ForName (char *name, qboolean crash);
void	*Mod_Extradata (model_t *mod);	// handles caching
void	Mod_TouchModel (char *name);
void	Mod_KnownStats (int *known, int *referenced, int *peak);	// QuakeOG, qog_mem

mleaf_t *Mod_PointInLeaf (float *p, model_t *model);
byte	*Mod_LeafPVS (mleaf_t *leaf, model_t *model);

#endif	// __MODEL__
