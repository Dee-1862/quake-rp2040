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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "r_local.h"

model_t	*loadmodel;
char	loadname[32];	// for hunk tags

void Mod_LoadSpriteModel (model_t *mod, void *buffer);
void Mod_LoadBrushModel (model_t *mod, void *buffer);
static void Mod_FinishBrushModel (model_t *mod);	// QuakeOG
void Mod_LoadAliasModel (model_t *mod, void *buffer);
model_t *Mod_LoadModel (model_t *mod, qboolean crash);

#ifdef QOG_CONST_NOVIS
// QuakeOG: the all-visible row is never written, so it is a constant (flash)
#define NOVIS16		0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
#define NOVIS256	NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16, \
					NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16,NOVIS16
static const byte	mod_novis[MAX_MAP_LEAFS/8] = { NOVIS256, NOVIS256, NOVIS256, NOVIS256 };
#else
byte	mod_novis[MAX_MAP_LEAFS/8];
#endif

// QuakeOG: QOG_MAX_MOD_KNOWN shrinks the table. Mod_FindName reuses slots of
// models the current level no longer references once it is full, so it only
// has to hold one level's models (160 at most in the shareware episode).
#ifdef QOG_MAX_MOD_KNOWN
#define	MAX_MOD_KNOWN	QOG_MAX_MOD_KNOWN
#else
#define	MAX_MOD_KNOWN	256
#endif
#ifdef QOG_MOD_FLASH
// QuakeOG: QOG_MOD_FLASH keeps no model_t in static RAM. The table holds the
// current level's models only: a name, and a model_t that is either baked
// (used in place in the flash image, see qogbake_header_t in model.h) or
// loaded the normal way into a model_t allocated on the level's hunk.
// Mod_ClearAll empties it before the hunk is freed, so every level finds
// its models again (an alias model loaded the normal way is loaded again
// too, its cache block is freed with its model_t). The per-frame arrays of
// a map are in mod_frames, found through model_t.frameslot.
typedef struct
{
	const char	*name;		// the model's own name, or a constant "*n" while it has none
	model_t		*model;		// baked, on the hunk, or NULL (an inline model not loaded yet)
	int			needload;	// a baked map: NL_PRESENT once it has its per-frame arrays
} modslot_t;
static modslot_t	mod_known[MAX_MOD_KNOWN];
modframe_t	mod_frames[QOG_MOD_FRAMES];
static int	mod_peakknown;	// most models one level used, for qog_mem
static int	mod_nextframe;	// next frameslot for an item box loaded the normal way
#else
model_t	mod_known[MAX_MOD_KNOWN];
#endif
int		mod_numknown;

static qboolean	mod_inflash;	// QuakeOG: see below at mod_base
static int		mod_flashlen;	// QuakeOG: file length while mod_inflash

// QuakeOG: baked maps, see qogbake_header_t in model.h. id's surface cache
// keeps per-frame pointers inside msurface_t, so it cannot be baked.
#ifdef QOG_SURFCACHE
#undef QOG_BAKE
#endif
#ifndef QOG_BAKE
#undef QOG_BAKE_WRITER
#endif
#ifdef QOG_BAKE_WRITER
static void Mod_Bake_f (void);
#endif
#ifdef QOG_BAKE
static qogbake_header_t	*mod_bake;		// the mapped image, or NULL
static qboolean			mod_baketried;
static int				mod_aliassize;	// bytes of the last alias model put in the cache

// QuakeOG: a baked alias model's data is in the image, not in the cache
static qboolean Mod_InBake (void *p)
{
	return mod_bake && (byte *)p >= (byte *)mod_bake
		&& (byte *)p < (byte *)mod_bake + mod_bake->size;
}
static qboolean Mod_LoadBaked (model_t *mod, byte *f, int flen);
static model_t *Mod_BakeLookup (char *name, byte *f, int flen);
#define MOD_INBAKE(p)	Mod_InBake (p)
#ifdef QOG_BAKE_STUBS
// QuakeOG: a map baked with -bakestubs points at nothing in the pack, so its
// file (a qogbake_stub_t) may be anywhere
#define BAKE_ANYADDR(e)	((e)->fileaddr == 0)
#else
#define BAKE_ANYADDR(e)	false
#endif
#else
#define MOD_INBAKE(p)	false
#endif

// values for model_t's needload
#define NL_PRESENT		0
#define NL_NEEDS_LOADED	1
#define NL_UNREFERENCED	2

// QuakeOG: an item box (maps/b_*.bsp) is a map loaded next to the world,
// so with QOG_MOD_FLASH it needs per-frame arrays of its own
static qboolean Mod_IsItemBox (const char *name)
{
	return !Q_strncmp ((char *)name, "maps/b_", 7);
}

float RadiusFromBounds (vec3_t mins, vec3_t maxs);

// QuakeOG: the per-submodel fields id's Mod_LoadBrushModel sets for inline
// model i (0 is the world itself), moved here from its loop unchanged
static void Mod_SetSubmodel (model_t *mod, int i)
{
	dmodel_t	*bm;
	int			j;

	bm = &mod->submodels[i];

	mod->hulls[0].firstclipnode = bm->headnode[0];
	for (j=1 ; j<MAX_MAP_HULLS ; j++)
	{
		mod->hulls[j].firstclipnode = bm->headnode[j];
		mod->hulls[j].lastclipnode = mod->numclipnodes-1;
	}

	mod->firstmodelsurface = bm->firstface;
	mod->nummodelsurfaces = bm->numfaces;

	VectorCopy (bm->maxs, mod->maxs);
	VectorCopy (bm->mins, mod->mins);
	mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

	mod->numleafs = bm->visleafs;
}

#ifdef QOG_MOD_FLASH
static void Mod_FinishBaked (model_t *mod);

// QuakeOG: constant names for inline models with no model_t yet (a client
// touches "*n" before it loads the world)
#define MOD_IN10(p)	p "0", p "1", p "2", p "3", p "4", p "5", p "6", p "7", p "8", p "9"
static const char	mod_inlinenames[MAX_MODELS][5] = {
	"*0", "*1", "*2", "*3", "*4", "*5", "*6", "*7", "*8", "*9",
	MOD_IN10("*1"), MOD_IN10("*2"), MOD_IN10("*3"), MOD_IN10("*4"),
	MOD_IN10("*5"), MOD_IN10("*6"), MOD_IN10("*7"), MOD_IN10("*8"), MOD_IN10("*9"),
	MOD_IN10("*10"), MOD_IN10("*11"), MOD_IN10("*12"), MOD_IN10("*13"), MOD_IN10("*14"),
	MOD_IN10("*15"), MOD_IN10("*16"), MOD_IN10("*17"), MOD_IN10("*18"), MOD_IN10("*19"),
	MOD_IN10("*20"), MOD_IN10("*21"), MOD_IN10("*22"), MOD_IN10("*23"), MOD_IN10("*24"),
	"*250", "*251", "*252", "*253", "*254", "*255"
};

static model_t *Mod_NewModel (const char *name)
{
	model_t	*mod;

	mod = Hunk_AllocName (sizeof(model_t), "models");	// zeroed
	strcpy (mod->name, name);
	mod->needload = NL_NEEDS_LOADED;
	return mod;
}

/*
==================
Mod_FindSlot

QuakeOG: the table entry for name, added if the level does not know it
yet: the baked model when the image holds this file, a constant name for
an inline model, otherwise a new model_t on the hunk to load into.
==================
*/
static modslot_t *Mod_FindSlot (char *name)
{
	int			i;
	modslot_t	*s;

	if (!name[0])
		Sys_Error ("Mod_ForName: NULL name");
	for (i=0 , s=mod_known ; i<mod_numknown ; i++, s++)
		if (!strcmp (s->name, name))
			return s;
	if (mod_numknown == MAX_MOD_KNOWN)
		Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
	mod_numknown++;
	if (mod_numknown > mod_peakknown)
		mod_peakknown = mod_numknown;
	s->needload = NL_NEEDS_LOADED;
#ifdef QOG_BAKE
	if (name[0] != '*')
	{
		int		flen;
		byte	*f = COM_FlashFile (name, &flen);

		s->model = f ? Mod_BakeLookup (name, f, flen) : NULL;
		if (s->model)
		{
			s->name = s->model->name;
			return s;
		}
	}
#endif
	if (name[0] == '*')
	{
		i = atoi (name + 1);
		if (i > 0 && i < MAX_MODELS && !strcmp (mod_inlinenames[i], name))
		{
			s->model = NULL;
			s->name = mod_inlinenames[i];
			return s;
		}
	}
	s->model = Mod_NewModel (name);
	s->name = s->model->name;
	return s;
}
#endif	// QOG_MOD_FLASH

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
#ifndef QOG_CONST_NOVIS
	memset (mod_novis, 0xff, sizeof(mod_novis));
#endif
#ifdef QOG_BAKE_WRITER
	Cmd_AddCommand ("qog_bake", Mod_Bake_f);	// QuakeOG
#endif
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (model_t *mod)
{
	void	*r;

	if (MOD_INBAKE (mod->cache.data))	// QuakeOG: baked, read in place
		return mod->cache.data;
	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, true);
	
	if (!mod->cache.data)
		Sys_Error ("Mod_Extradata: caching failed");
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, model_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;
	
	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}
	
	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, model_t *model)
{
	static byte	decompressed[MAX_MAP_LEAFS/8];
	int		c;
	byte	*out;
	int		row;

	row = (model->numleafs+7)>>3;	
	out = decompressed;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return decompressed;		
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}
	
		c = in[1];
		in += 2;
		while (c)
		{
			*out++ = 0;
			c--;
		}
	} while (out - decompressed < row);
	
	return decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, model_t *model)
{
	if (leaf == model->leafs)
		return (byte *)mod_novis;	// QuakeOG: cast, see QOG_CONST_NOVIS
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	model_t	*mod;

#ifdef QOG_MOD_FLASH
	// QuakeOG: the hunk is freed next, with every model_t loaded the normal
	// way, so free their cache blocks (the cache points into the model_t)
	for (i=0 ; i<mod_numknown ; i++)
	{
		mod = mod_known[i].model;
		if (mod && mod->type == mod_alias && !MOD_INBAKE (mod->cache.data) && mod->cache.data)
			Cache_Free (&mod->cache);
	}
	mod_numknown = 0;
	mod_nextframe = QOG_MOD_FRAMES_BAKED;
	memset (mod_frames, 0, sizeof(mod_frames));
#else

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++) {
		mod->needload = NL_UNREFERENCED;
//FIX FOR CACHE_ALLOC ERRORS:
		if (mod->type == mod_sprite) mod->cache.data = NULL;
	}
#endif
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName (char *name)
{
	int		i;
	model_t	*mod;
	model_t	*avail = NULL;

#ifdef QOG_MOD_FLASH
	// QuakeOG: a model_t to load into (the brush loader's inline models)
	modslot_t	*s = Mod_FindSlot (name);

	if (!s->model || MOD_INBAKE (s->model))
	{
		s->model = Mod_NewModel (name);
		s->name = s->model->name;
	}
	return s->model;
#else
	if (!name[0])
		Sys_Error ("Mod_ForName: NULL name");
		
//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!strcmp (mod->name, name) )
			break;
		if (mod->needload == NL_UNREFERENCED)
			if (!avail || mod->type != mod_alias)
				avail = mod;
	}
			
	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{
			if (avail)
			{
				mod = avail;
				if (mod->type == mod_alias && !MOD_INBAKE (mod->cache.data))	// QuakeOG: MOD_INBAKE
					if (Cache_Check (&mod->cache))
						Cache_Free (&mod->cache);
#ifdef QOG_MAX_MOD_KNOWN
				// QuakeOG: a reused slot starts empty, like one never used.
				// The brush loader never sets synctype, so a map or inline
				// model inherited an alias model's ST_RAND and the client
				// drew a random syncbase (a rand() call) for it.
				memset (mod, 0, sizeof(*mod));
#endif
			}
			else
				Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		}
		else
			mod_numknown++;
		strcpy (mod->name, name);
		mod->needload = NL_NEEDS_LOADED;
	}

	return mod;
#endif
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (char *name)
{
	model_t	*mod;
	
#ifdef QOG_MOD_FLASH
	mod = Mod_FindSlot (name)->model;	// QuakeOG: NULL for an inline model not loaded yet
	if (!mod)
		return;
#else
	mod = Mod_FindName (name);
#endif
	
	if (mod->needload == NL_PRESENT)
	{
		if (mod->type == mod_alias && !MOD_INBAKE (mod->cache.data))	// QuakeOG: MOD_INBAKE
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel (model_t *mod, qboolean crash)
{
	unsigned *buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap

	if (mod->type == mod_alias)
	{
		if (MOD_INBAKE (mod->cache.data) || Cache_Check (&mod->cache))	// QuakeOG: MOD_INBAKE
		{
			mod->needload = NL_PRESENT;
			return mod;
		}
	}
	else
	{
		if (mod->needload == NL_PRESENT)
			return mod;
	}

//
// because the world is so huge, load it one piece at a time
//
	
//
// load the file
//
	// QuakeOG: brush models (maps) and alias models are used from the
	// read-only flash image when the port provides one (alias models keep
	// only their header and descriptors in the cache). Sprites still load
	// into the cache for now.
	buf = NULL;
	mod_inflash = false;
	{
		int		flen;
		byte	*f = COM_FlashFile (mod->name, &flen);
#if defined(QOG_BAKE) && !defined(QOG_MOD_FLASH)
		// QuakeOG: a baked map, alias model or sprite needs no loading
		// (with QOG_MOD_FLASH, Mod_FindSlot has already looked)
		if (f && Mod_LoadBaked (mod, f, flen))
			return mod;
#endif
		if (f && !(((int)f) & 3)
			&& LittleLong (*(unsigned *)f) != IDSPRITEHEADER
			&& (LittleLong (*(unsigned *)f) != IDPOLYHEADER || r_pixbytes == 1))
		{
			buf = (unsigned *)f;
			mod_inflash = true;
			mod_flashlen = flen;
		}
	}
	if (!buf)
		buf = (unsigned *)COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf));
	if (!buf)
	{
		if (crash)
			Sys_Error ("Mod_NumForName: %s not found", mod->name);
		return NULL;
	}
	
//
// allocate a new model
//
	COM_FileBase (mod->name, loadname);
	
	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = NL_PRESENT;

	switch (LittleLong(*(unsigned *)buf))
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;
		
	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;
	
	default:
		Mod_LoadBrushModel (mod, buf);
		break;
	}
	mod_inflash = false;

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName (char *name, qboolean crash)
{
	model_t	*mod;

#ifdef QOG_MOD_FLASH
	// QuakeOG: a baked model is used in place, a map gets its per-frame
	// arrays the first time the level asks for it
	modslot_t	*s = Mod_FindSlot (name);

	mod = s->model;
	if (!mod)
	{
		if (crash)
			Sys_Error ("Mod_NumForName: %s not found", name);
		return NULL;
	}
	if (MOD_INBAKE (mod))
	{
		if (s->needload != NL_PRESENT)
		{
			s->needload = NL_PRESENT;
			if (mod->type == mod_brush)
				Mod_FinishBaked (mod);
		}
		return mod;
	}
	return Mod_LoadModel (mod, crash);
#else
	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
#endif
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

byte	*mod_base;

// QuakeOG: mod_inflash is true while the model being loaded is a read-only
// image in flash. Read-only lumps whose file layout matches memory are then
// used in place, and the in-place byte swaps (no-ops on little-endian) are
// skipped.
#define MOD_IN_PLACE(p)	(mod_inflash && (((int)(p)) & 3) == 0)


/*
=================
Mod_LoadTextures
=================
*/
void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, max, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;

	if (!l->filelen)
	{
		loadmodel->textures = NULL;
		return;
	}
	m = (dmiptexlump_t *)(mod_base + l->fileofs);

	if (!mod_inflash)	// QuakeOG: no in-place swaps on read-only flash (no-ops on little-endian)
		m->nummiptex = LittleLong (m->nummiptex);

	loadmodel->numtextures = m->nummiptex;
	loadmodel->textures = Hunk_AllocName (m->nummiptex * sizeof(*loadmodel->textures) , loadname);

	for (i=0 ; i<m->nummiptex ; i++)
	{
		if (!mod_inflash)
			m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		if (!mod_inflash)
		{
			mt->width = LittleLong (mt->width);
			mt->height = LittleLong (mt->height);
			for (j=0 ; j<MIPLEVELS ; j++)
				mt->offsets[j] = LittleLong (mt->offsets[j]);
		}

		if ( (mt->width & 15) || (mt->height & 15) )
			Sys_Error ("Texture %s is not 16 aligned", mt->name);
		pixels = mt->width*mt->height/64*85;
		// QuakeOG: from flash only the small header lives in RAM; the pixels
		// are read in place. Otherwise they follow the struct, as before.
		tx = Hunk_AllocName (sizeof(texture_t) + (mod_inflash ? 0 : pixels), loadname );
		loadmodel->textures[i] = tx;

		memcpy (tx->name, mt->name, sizeof(tx->name));
		tx->width = mt->width;
		tx->height = mt->height;
		if (mod_inflash)
		{
			for (j=0 ; j<MIPLEVELS ; j++)
				tx->mips[j] = (byte *)mt + mt->offsets[j];
		}
		else
		{
			for (j=0 ; j<MIPLEVELS ; j++)
				tx->mips[j] = (byte *)(tx+1) + mt->offsets[j] - sizeof(miptex_t);
			memcpy ( tx+1, mt+1, pixels);
		}
		
		if (!Q_strncmp(mt->name,"sky",3))	
			R_InitSky (tx);
	}

//
// sequence the animations
//
	for (i=0 ; i<m->nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// allready sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		max = tx->name[1];
		altmax = 0;
		if (max >= 'a' && max <= 'z')
			max -= 'a' - 'A';
		if (max >= '0' && max <= '9')
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if (max >= 'A' && max <= 'J')
		{
			altmax = max - 'A';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<m->nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > max)
					max = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}
		
#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<max ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%max ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (max)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting
=================
*/
void Mod_LoadLighting (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}
	if (mod_inflash)
	{	// QuakeOG: read only bytes, read in place from flash
		loadmodel->lightdata = mod_base + l->fileofs;
		return;
	}
	loadmodel->lightdata = Hunk_AllocName ( l->filelen, loadname);
	memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVisibility
=================
*/
void Mod_LoadVisibility (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}
	if (mod_inflash)
	{	// QuakeOG: read only bytes, read in place from flash
		loadmodel->visdata = mod_base + l->fileofs;
		return;
	}
	loadmodel->visdata = Hunk_AllocName ( l->filelen, loadname);
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
void Mod_LoadEntities (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	if (mod_inflash && mod_base[l->fileofs + l->filelen - 1] == 0)
	{	// QuakeOG: parsed read only, so use it in place when it is NUL terminated
		loadmodel->entities = (char *)(mod_base + l->fileofs);
		return;
	}
	loadmodel->entities = Hunk_AllocName ( l->filelen, loadname);
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadVertexes
=================
*/
void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	loadmodel->numvertexes = count;
	if (MOD_IN_PLACE(in))
	{	// QuakeOG: mvertex_t and dvertex_t are both three floats
		loadmodel->vertexes = (mvertex_t *)in;
		return;
	}
	out = Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->vertexes = out;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);
		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
void Mod_LoadEdges (lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	loadmodel->numedges = count;
	if (MOD_IN_PLACE(in))
	{	// QuakeOG: with cachededgeoffset moved out, medge_t is dedge_t
		loadmodel->edges = (medge_t *)in;
		return;
	}
	out = Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);

	loadmodel->edges = out;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int 	i, j, count;
	int		miptex;
	float	len1, len2;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<8 ; j++)
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
		len1 = Length (out->vecs[0]);
		len2 = Length (out->vecs[1]);
		len1 = (len1 + len2)/2;
		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;
#if 0
		if (len1 + len2 < 0.001)
			out->mipadjust = 1;		// don't crash
		else
			out->mipadjust = 1 / floor( (len1+len2)/2 + 0.1 );
#endif

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);
	
		if (!loadmodel->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else
		{
			if (miptex >= loadmodel->numtextures)
				Sys_Error ("miptex >= loadmodel->numtextures");
			out->texture = loadmodel->textures[miptex];
			if (!out->texture)
			{
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = SURF_TEXINFO (loadmodel, s);	// QuakeOG
	
	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		
		for (j=0 ; j<2 ; j++)
		{
			val = v->position[0] * tex->vecs[j][0] + 
				v->position[1] * tex->vecs[j][1] +
				v->position[2] * tex->vecs[j][2] +
				tex->vecs[j][3];
			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{	
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 256)
			Sys_Error ("Bad surface extents");
	}
}


/*
=================
Mod_LoadFaces
=================
*/
void Mod_LoadFaces (lump_t *l)
{
	dface_t		*in;
	msurface_t 	*out;
	int			i, count, surfnum;
	int			planenum, side;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for ( surfnum=0 ; surfnum<count ; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);		
		out->flags = 0;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);
		if (side)
			out->flags |= SURF_PLANEBACK;
#ifdef QOG_PLANES_EXACT
		if (planenum < 0 || planenum >= loadmodel->numplanes)	// QuakeOG
			Sys_Error ("Mod_LoadFaces: bad plane number %i in %s", planenum, loadmodel->name);
#endif

#ifdef QOG_SURF_INDEX
		// QuakeOG: 16-bit numbers as in the file, read as unsigned and checked
		planenum = (unsigned short)LittleShort(in->planenum);
		if (planenum >= loadmodel->numplanes)
			Sys_Error ("Mod_LoadFaces: bad plane number %i in %s", planenum, loadmodel->name);
		out->planenum = planenum;
		i = (unsigned short)LittleShort (in->texinfo);
		if (i >= loadmodel->numtexinfo)
			Sys_Error ("Mod_LoadFaces: bad texinfo number %i in %s", i, loadmodel->name);
		out->texinfonum = i;
#else
		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + LittleShort (in->texinfo);
#endif

		CalcSurfaceExtents (out);
				
	// lighting info

		for (i=0 ; i<MAXLIGHTMAPS ; i++)
			out->styles[i] = in->styles[i];
		i = LittleLong(in->lightofs);
		if (i == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + i;
		
	// set the drawing flags flag
		
		if (!Q_strncmp(SURF_TEXINFO (loadmodel, out)->texture->name,"sky",3))	// sky (QuakeOG: SURF_TEXINFO)
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			continue;
		}
		
		if (!Q_strncmp(SURF_TEXINFO (loadmodel, out)->texture->name,"*",1))		// turbulent (QuakeOG: SURF_TEXINFO)
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
			for (i=0 ; i<2 ; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
			continue;
		}
	}
}


/*
=================
Mod_SetParent
=================
*/
void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
void Mod_LoadNodes (lump_t *l)
{
	int			i, j, count, p;
	dnode_t		*in;
	mnode_t 	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}
	
		p = LittleLong(in->planenum);
#ifdef QOG_PLANES_EXACT
		if (p < 0 || p >= loadmodel->numplanes)	// QuakeOG
			Sys_Error ("Mod_LoadNodes: bad plane number %i in %s", p, loadmodel->name);
#endif
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort (in->firstface);
		out->numsurfaces = LittleShort (in->numfaces);
		
		for (j=0 ; j<2 ; j++)
		{
			p = LittleShort (in->children[j]);
			if (p >= 0)
				out->children[j] = loadmodel->nodes + p;
			else
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
		}
	}
	
	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
void Mod_LoadLeafs (lump_t *l)
{
	dleaf_t 	*in;
	mleaf_t 	*out;
	int			i, j, count, p;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

#ifdef QOG_LEAF_PACKED
		// QuakeOG: kept as the file's 16-bit index, see LEAF_MARKSURFACES
		out->firstmarksurface = (unsigned short)LittleShort(in->firstmarksurface);
		out->nummarksurfaces = (unsigned short)LittleShort(in->nummarksurfaces);
		if (out->firstmarksurface + out->nummarksurfaces > loadmodel->nummarksurfaces)
			Sys_Error ("Mod_LoadLeafs: bad mark surfaces in %s", loadmodel->name);
#else
		out->firstmarksurface = loadmodel->marksurfaces +
			LittleShort(in->firstmarksurface);
		out->nummarksurfaces = LittleShort(in->nummarksurfaces);
#endif
		
		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		
		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];
	}	
}

/*
=================
Mod_LoadClipnodes
=================
*/
void Mod_LoadClipnodes (lump_t *l)
{
	dclipnode_t *in, *out;
	int			i, count;
	hull_t		*hull;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	// QuakeOG: same layout as the file and never written, so read in place from flash
	out = MOD_IN_PLACE(in) ? in : Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

#ifdef QOG_PLANES_EXACT
	for (i=0 ; i<count ; i++)	// QuakeOG
	{
		int		p = LittleLong (in[i].planenum);
		if (p < 0 || p >= loadmodel->numplanes)
			Sys_Error ("Mod_LoadClipnodes: bad plane number %i in %s", p, loadmodel->name);
	}
#endif
	if (out == in)
		return;
	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = LittleLong(in->planenum);
		out->children[0] = LittleShort(in->children[0]);
		out->children[1] = LittleShort(in->children[1]);
	}
}

/*
=================
Mod_MakeHull0

Deplicate the drawing hull structure as a clipping hull
=================
*/
void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	dclipnode_t *out;
	int			i, j, count;
	hull_t		*hull;
	
	hull = &loadmodel->hulls[0];	
	
	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = Hunk_AllocName ( count*sizeof(*out), loadname);	

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
void Mod_LoadMarksurfaces (lump_t *l)
{	
	int		i, j, count;
	short		*in;
	marksurf_t	*out;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
#ifdef QOG_MARKSURF16
	// QuakeOG: the file's 16-bit surface numbers, read in place from flash
	// (checked like id's, but as the unsigned numbers they are)
	if (mod_inflash && !(((int)in) & 1))
	{
		for (i=0 ; i<count ; i++)
			if ((unsigned short)in[i] >= loadmodel->numsurfaces)
				Sys_Error ("Mod_ParseMarksurfaces: bad surface number");
		loadmodel->marksurfaces = (marksurf_t *)in;
		loadmodel->nummarksurfaces = count;
		return;
	}
#endif
	out = Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
#ifdef QOG_MARKSURF16
		j = (unsigned short)LittleShort(in[i]);
		if (j >= loadmodel->numsurfaces)
			Sys_Error ("Mod_ParseMarksurfaces: bad surface number");
		out[i] = j;
#else
		j = LittleShort(in[i]);
		if (j >= loadmodel->numsurfaces)
			Sys_Error ("Mod_ParseMarksurfaces: bad surface number");
		out[i] = loadmodel->surfaces + j;
#endif
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void Mod_LoadSurfedges (lump_t *l)
{	
	int		i, count;
	int		*in, *out;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	loadmodel->numsurfedges = count;
	if (MOD_IN_PLACE(in))
	{	// QuakeOG: same layout as the file, read in place from flash
		loadmodel->surfedges = in;
		return;
	}
	out = Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->surfedges = out;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}

/*
=================
Mod_LoadPlanes
=================
*/
void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;
	
	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
#ifdef QOG_PLANES_EXACT
	// QuakeOG: id allocates twice the planes and never touches the second
	// half. Allocate only what the file holds, and check every plane number
	// against it at load (faces, nodes, clipnodes) so nothing can read past.
	out = Hunk_AllocName ( count*sizeof(*out), loadname);
#else
	out = Hunk_AllocName ( count*2*sizeof(*out), loadname);
#endif

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return Length (corner);
}

#if !defined(QOG_FRAMEBITS) || !defined(QOG_DLIGHT_SPARSE)	// QuakeOG: else unused
/*
=================
Mod_AllocFrameStamps

QuakeOG: a zeroed array of per-frame stamps (see framestamp_t in model.h).
With QOG_FRAME16 the array's length and base counter (zero) come first.
=================
*/
static framestamp_t *Mod_AllocFrameStamps (int count)
{
#ifdef QOG_FRAME16
	int		*head;

	head = Hunk_AllocName (2 * sizeof(int) + count * sizeof(framestamp_t), loadname);
	head[0] = count;	// FS_COUNT
	head[1] = 0;		// FS_BASE
	return (framestamp_t *)(head + 2);
#else
	return Hunk_AllocName (count * sizeof(framestamp_t), loadname);
#endif
}
#endif

#if defined(QOG_FRAMEBITS) || defined(QOG_DLIGHT_SPARSE)
/*
=================
Mod_AllocFrameBits

QuakeOG: a stamp array as bits (see framebits_t in model.h): the byte
count, tag 0, then every bit set, which is id's zeroed array of stamps.
=================
*/
static framebits_t *Mod_AllocFrameBits (int count)
{
	int		*head, bytes;

	bytes = (count + 7) >> 3;
	head = Hunk_AllocName (2 * sizeof(int) + bytes, loadname);
	head[0] = bytes;	// FB_BYTES
	head[1] = 0;		// FB_TAG
	memset (head + 2, 0xFF, bytes);
	return (framebits_t *)(head + 2);
}

/*
=================
FB_Clear

QuakeOG: the bits now belong to counter, and no entry holds it yet.
=================
*/
void FB_Clear (framebits_t *bits, int counter)
{
	memset (bits, 0, FB_BYTES (bits));
	FB_TAG (bits) = counter;
}
#endif

#ifdef QOG_LEAF16
/*
=================
Mod_LeafKeyOverflow

QuakeOG: LEAF_KEY_SET found a key that 16 bits cannot hold.
=================
*/
void Mod_LeafKeyOverflow (void)
{
	Sys_Error ("LEAF_KEY_SET: a key above 0xFFFF, QOG_LEAF16 cannot hold it");
}
#endif

#ifdef QOG_FRAME16
/*
=================
FS_Refill

QuakeOG: start a new window at counter. Every old stamp becomes 0xFFFF,
which matches no counter, as id's older stamps never match again.
=================
*/
void FS_Refill (framestamp_t *stamps, int counter)
{
	memset (stamps, 0xFF, FS_COUNT (stamps) * sizeof(framestamp_t));
	FS_BASE (stamps) = counter;
}
#endif

/*
=================
Mod_LoadBrushLumps

QuakeOG: the first half of id's Mod_LoadBrushModel, which builds the
structures play only reads. The baked image holds its result.
=================
*/
static void Mod_LoadBrushLumps (model_t *mod, void *buffer)
{
	int			i;
	dheader_t	*header;

	loadmodel->type = mod_brush;
	
	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
#ifdef QOG_BAKE_STUBS
	if (i == QOGB_STUB_IDENT)	// QuakeOG: the map itself is in qogbake.bin
		Sys_Error ("Mod_LoadBrushModel: %s is a stub, its map is in qogbake.bin, which is missing or does not match", mod->name);
#endif
	if (i != BSPVERSION)
		Sys_Error ("Mod_LoadBrushModel: %s has wrong version number (%i should be %i)", mod->name, i, BSPVERSION);

// swap all the lumps
	mod_base = (byte *)header;

	if (!mod_inflash)	// QuakeOG: flash is read only, and the swap is a no-op on little-endian
		for (i=0 ; i<sizeof(dheader_t)/4 ; i++)
			((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap
	
	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES]);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes (&header->lumps[LUMP_NODES]);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES]);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 ();
}

#ifdef QOG_BAKE
/*
===============================================================================

QuakeOG: BAKED MODELS (see qogbake_header_t in model.h)

===============================================================================
*/

static unsigned Mod_Fnv (unsigned h, const void *data, int n)
{
	const byte	*p = data;
	int			i;

	for (i=0 ; i<n ; i++)
		h = (h ^ p[i]) * 0x01000193u;
	return h;
}

#define MOD_OFS(t, f)	((unsigned)(size_t)&((t *)0)->f)

/*
=================
Mod_BakeLayout

A hash of every size, offset and flag the image depends on, so an image
from a build with other structures is refused instead of misread.
=================
*/
static unsigned Mod_BakeLayout (void)
{
	unsigned	key[] = {
		QOGB_VERSION, sizeof(void *),
		sizeof(model_t), sizeof(mnode_t), sizeof(mleaf_t), sizeof(msurface_t),
		sizeof(mtexinfo_t), sizeof(texture_t), sizeof(mplane_t), sizeof(dmodel_t),
		sizeof(dclipnode_t), sizeof(hull_t), sizeof(marksurf_t), sizeof(medge_t),
		sizeof(mvertex_t), sizeof(framestamp_t), sizeof(aliashdr_t), sizeof(msprite_t),
		sizeof(mspriteframe_t), sizeof(mspritegroup_t),
		MOD_OFS(model_t, submodels), MOD_OFS(model_t, hulls), MOD_OFS(model_t, textures),
#ifdef QOG_MOD_FLASH
		MOD_OFS(model_t, cache), MOD_OFS(model_t, frameslot), QOG_MOD_FRAMES_BAKED,
#else
		MOD_OFS(model_t, cache), MOD_OFS(model_t, surf_visframe),
#endif
		MOD_OFS(mnode_t, children), MOD_OFS(mnode_t, plane), MOD_OFS(mnode_t, parent),
		MOD_OFS(mleaf_t, compressed_vis), MOD_OFS(mleaf_t, ambient_sound_level),
		MOD_OFS(msurface_t, samples), MOD_OFS(msurface_t, styles),
		MOD_OFS(texture_t, mips), MOD_OFS(texture_t, anim_next), MOD_OFS(mtexinfo_t, texture),
		0
#ifdef QOG_SURF_INDEX
		| 1
#endif
#ifdef QOG_MARKSURF16
		| 2
#endif
#ifdef QOG_LEAF_PACKED
		| 4
#endif
#ifdef QOG_SURF_PACKED
		| 8
#endif
#ifdef QOG_PLANES_EXACT
		| 16
#endif
#ifdef QOG_MOD_FLASH
		| 32	// QuakeOG: a map's block holds its finished inline models
#endif
	};

	return Mod_Fnv (0x811C9DC5u, key, sizeof(key));
}

/*
=================
Mod_FileHash

Identifies the file a model was baked from: a map's header and plane
lump, or the first 4 KB of an alias model or sprite.
=================
*/
static unsigned Mod_FileHash (byte *f, int len)
{
	dheader_t	*h = (dheader_t *)f;
	unsigned	v;

	if (len < (int)sizeof(*h) || LittleLong (h->version) != BSPVERSION)
		return Mod_Fnv (0x811C9DC5u, f, len < 4096 ? len : 4096);
	v = Mod_Fnv (0x811C9DC5u, h, sizeof(*h));
	return Mod_Fnv (v, f + LittleLong (h->lumps[LUMP_PLANES].fileofs),
		LittleLong (h->lumps[LUMP_PLANES].filelen));
}

/*
=================
Mod_BakeOpen

Maps <gamedir>/qogbake.bin once, at the first model load, and checks it
was built by a build with this layout for the address it got. -nobake
skips it (qog_bake runs that way).
=================
*/
static void Mod_BakePath (char *path)
{
	int		i;

	// QuakeOG: -bakefile <path> reads (and qog_bake writes) another image
	i = COM_CheckParm ("-bakefile");
	if (i && i < com_argc - 1)
		Q_strncpy (path, com_argv[i + 1], MAX_OSPATH - 1);
	else
		sprintf (path, "%s/qogbake.bin", com_gamedir);
}

static void Mod_BakeOpen (void)
{
	char				path[MAX_OSPATH];
	int					len, h;
	qogbake_header_t	*img;

	if (COM_CheckParm ("-nobake"))
		return;
	Mod_BakePath (path);
	len = Sys_FileOpenRead (path, &h);
	if (len < 0)
		return;
	Sys_FileClose (h);
	if (len < (int)sizeof(qogbake_header_t))
		return;
	img = Sys_FlashMap (path, len);
	if (!img)
		return;
	if (img->magic != QOGB_MAGIC || img->version != QOGB_VERSION
		|| img->layout != Mod_BakeLayout () || img->size != (unsigned)len)
	{
		Con_Printf ("qogbake.bin was baked by another build, not used (run qog_bake again)\n");
		return;
	}
	if (img->base != (unsigned)(size_t)img)
	{
		Con_Printf ("qogbake.bin is for address %08x but mapped at %p, not used\n", img->base, img);
		return;
	}
	mod_bake = img;
	Con_Printf ("Baked models: %i in qogbake.bin, %i KB\n", img->nummaps, img->size / 1024);
}

/*
=================
Mod_LoadBaked

Fills mod from the baked image when it holds this file (f, flen in a
mapped pack), and returns true. A map then gets only its per-frame
arrays. An alias model or sprite points at its data in the image.
=================
*/
static model_t *Mod_BakeLookup (char *name, byte *f, int flen)
{
	qogbake_entry_t	*e;
	int				i;

	if (!mod_baketried)
	{
		mod_baketried = true;
		Mod_BakeOpen ();
	}
	if (!mod_bake || r_pixbytes != 1)	// the image holds 8-bit pixels
		return NULL;
	e = (qogbake_entry_t *)((byte *)mod_bake + mod_bake->dirofs);
	for (i=0 ; i<mod_bake->nummaps ; i++, e++)
		if (!strcmp (e->name, name))
			break;
	if (i == mod_bake->nummaps)
		return NULL;
	if ((!BAKE_ANYADDR (e) && e->fileaddr != (unsigned)(size_t)f) || e->filelen != (unsigned)flen
		|| e->filehash != Mod_FileHash (f, flen))
	{
		Con_Printf ("%s changed since it was baked, loading it the normal way\n", name);
		return NULL;
	}
	return (model_t *)e->model;
}

// QuakeOG: the one side effect of id's texture loader
static void Mod_BakedSky (model_t *mod)
{
	texture_t	*tx;
	int			i;

	for (i=0 ; i<mod->numtextures ; i++)
	{
		tx = mod->textures[i];
		if (tx && !Q_strncmp (tx->name, "sky", 3))
			R_InitSky (tx);
	}
}

static qboolean Mod_LoadBaked (model_t *mod, byte *f, int flen)
{
	model_t			*b;
	char			name[MAX_QPATH];

	b = Mod_BakeLookup (mod->name, f, flen);
	if (!b)
		return false;

	COM_FileBase (mod->name, loadname);
	loadmodel = mod;
	memcpy (name, mod->name, sizeof(name));
	*mod = *b;
	memcpy (mod->name, name, sizeof(name));
	mod->needload = NL_PRESENT;
	if (mod->type != mod_brush)
		return true;

	Mod_BakedSky (mod);
	Mod_FinishBrushModel (mod);
	return true;
}

#ifdef QOG_MOD_FLASH
/*
=================
Mod_FinishBaked

QuakeOG: a baked map used in place: its per-frame arrays and its inline
models' table entries, once per level.
=================
*/
static void Mod_FinishBaked (model_t *mod)
{
	COM_FileBase (mod->name, loadname);
	loadmodel = mod;
	Mod_BakedSky (mod);
	Mod_FinishBrushModel (mod);
}
#endif

#ifdef QOG_BAKE_WRITER
/*
===============================================================================

QuakeOG: the baker (PC only). The image is built in memory: each structure
the loader allocated on the hunk is copied in, and every pointer is then
translated through the table of copied ranges. Pointers into a mapped
pack are kept. Anything else (a pointer the image could not reach) makes
the model unbakeable, and it is skipped.

===============================================================================
*/

typedef struct
{
	byte		*lo, *hi;	// the hunk copy
	unsigned	to;			// its address in the image
} bakerange_t;

#define	MAX_BAKE_RANGES	2048

static byte			*bk_buf;
static int			bk_len, bk_max;
static unsigned		bk_base;
static bakerange_t	bk_ranges[MAX_BAKE_RANGES];
static int			bk_numranges;
static qboolean		bk_failed;
static char			bk_names[512][40];
static int			bk_numnames;
static int			bk_nextframe;	// QuakeOG: frameslot of the next item box map
static unsigned		bk_limit;		// QuakeOG: pack pointers kept must lie below this

#ifdef QOG_BAKE_STUBS
// QuakeOG: -bakestubs (see qogbake_stub_t in model.h). bk_detach is set
// while a map is baked that way: its pack data is copied, not pointed at.
// Texture pixels are copied once per distinct content for the whole image.
typedef struct
{
	unsigned	hash;
	int			size;
	unsigned	to;			// its address in the image
} bakeshared_t;

#define	MAX_BAKE_SHARED	4096

static qboolean		bk_stubs, bk_detach;
static bakeshared_t	bk_shared[MAX_BAKE_SHARED];
static int			bk_numshared;
static int			bk_sharedbytes, bk_savedbytes;	// texture pixels stored, and not stored twice
#else
#define bk_detach	false
#endif

static int Bake_Reserve (int size)
{
	int		ofs;

	ofs = (bk_len + 15) & ~15;
	if (ofs + size > bk_max)
	{
		bk_max = (ofs + size) * 2 + 0x10000;
		bk_buf = realloc (bk_buf, bk_max);
		if (!bk_buf)
			Sys_Error ("qog_bake: out of memory");
	}
	memset (bk_buf + bk_len, 0, ofs + size - bk_len);
	bk_len = ofs + size;
	return ofs;
}

#define BAKE_AT(t, addr)	((t *)(bk_buf + ((addr) - bk_base)))

// copies size bytes from the hunk into the image; data already in flash stays
static unsigned Bake_Copy (void *p, int size)
{
	int		ofs;

	if (!p || (COM_InFlash (p) && !bk_detach))	// QuakeOG: bk_detach copies pack data too
		return (unsigned)(size_t)p;
	if (bk_numranges == MAX_BAKE_RANGES)
		Sys_Error ("qog_bake: MAX_BAKE_RANGES");
	ofs = Bake_Reserve (size);
	memcpy (bk_buf + ofs, p, size);
	bk_ranges[bk_numranges].lo = p;
	bk_ranges[bk_numranges].hi = (byte *)p + size;
	bk_ranges[bk_numranges].to = bk_base + ofs;
	bk_numranges++;
	return bk_base + ofs;
}

static unsigned Bake_Ptr (void *p)
{
	int		i;

	if (!p)
		return 0;
	for (i=0 ; i<bk_numranges ; i++)
		if ((byte *)p >= bk_ranges[i].lo && (byte *)p < bk_ranges[i].hi)
			return bk_ranges[i].to + ((byte *)p - bk_ranges[i].lo);
	// QuakeOG: a pack pointer stays, unless the map is detached or the
	// image goes below that part of the pack (-bakestubs with -bakebase)
	if (COM_InFlash (p) && !bk_detach && (unsigned)(size_t)p < bk_limit)
		return (unsigned)(size_t)p;
	if (!bk_failed)
		Con_Printf ("qog_bake: %s has a pointer outside the map and the packs\n", loadmodel->name);
	bk_failed = true;
	return 0;
}

// rewrites a pointer field of a structure in the image
#define BAKE_FIX(f)		(*(unsigned *)&(f) = Bake_Ptr (f))

#ifdef QOG_BAKE_STUBS
/*
=================
Bake_CopyShared

QuakeOG: like Bake_Copy, but bytes the image already holds from another
map (the same texture's pixels) are pointed at instead of copied again.
=================
*/
static void Bake_CopyShared (byte *p, int size)
{
	unsigned	h;
	int			i;

	h = Mod_Fnv (0x811C9DC5u, p, size);
	for (i=0 ; i<bk_numshared ; i++)
		if (bk_shared[i].hash == h && bk_shared[i].size == size
			&& !memcmp (BAKE_AT(byte, bk_shared[i].to), p, size))
		{
			if (bk_numranges == MAX_BAKE_RANGES)
				Sys_Error ("qog_bake: MAX_BAKE_RANGES");
			bk_ranges[bk_numranges].lo = p;
			bk_ranges[bk_numranges].hi = p + size;
			bk_ranges[bk_numranges].to = bk_shared[i].to;
			bk_numranges++;
			bk_savedbytes += size;
			return;
		}
	if (bk_numshared == MAX_BAKE_SHARED)
		Sys_Error ("qog_bake: MAX_BAKE_SHARED");
	bk_shared[bk_numshared].hash = h;
	bk_shared[bk_numshared].size = size;
	bk_shared[bk_numshared].to = Bake_Copy (p, size);
	bk_numshared++;
	bk_sharedbytes += size;
}

// QuakeOG: the bytes of a texture's four mip levels, which follow each other
static void Bake_TexPixels (texture_t *tx, byte **lo, int *size)
{
	byte	*a, *b, *e;
	int		j;

	a = b = tx->mips[0];
	for (j=0 ; j<MIPLEVELS ; j++)
	{
		e = tx->mips[j] + (tx->width >> j) * (tx->height >> j);
		if (tx->mips[j] < a)
			a = tx->mips[j];
		if (e > b)
			b = e;
	}
	*lo = a;
	*size = b - a;
}

// QuakeOG: the stub that stands for map f in the board's pak (tools/mkboard.py
// writes the same bytes)
static void Bake_MakeStub (byte *f, int flen, qogbake_stub_t *st)
{
	memset (st, 0, sizeof(*st));
	st->ident = QOGB_STUB_IDENT;
	memcpy (st->lumps, ((dheader_t *)f)->lumps, sizeof(st->lumps));
	st->filelen = flen;
	st->filehash = Mod_FileHash (f, flen);
}
#endif

// a map's model_t in the image: its pointers translated, the cache empty
static void Bake_FixModel (model_t *bm)
{
	int		i;

	memset (&bm->cache, 0, sizeof(bm->cache));
	BAKE_FIX(bm->submodels);
	BAKE_FIX(bm->planes);
	BAKE_FIX(bm->leafs);
	BAKE_FIX(bm->vertexes);
	BAKE_FIX(bm->edges);
	BAKE_FIX(bm->nodes);
	BAKE_FIX(bm->texinfo);
	BAKE_FIX(bm->surfaces);
	BAKE_FIX(bm->surfedges);
	BAKE_FIX(bm->clipnodes);
	BAKE_FIX(bm->marksurfaces);
	for (i=0 ; i<MAX_MAP_HULLS ; i++)
	{
		BAKE_FIX(bm->hulls[i].clipnodes);
		BAKE_FIX(bm->hulls[i].planes);
	}
	BAKE_FIX(bm->textures);
	BAKE_FIX(bm->visdata);
	BAKE_FIX(bm->lightdata);
	BAKE_FIX(bm->entities);
}

/*
=================
Bake_Map

Runs the loader on one map and appends its block. Returns the address of
its model_t, or 0 if the map could not be baked.
=================
*/
static unsigned Bake_Map (char *name, byte *f, int flen)
{
	model_t		m, *bm;
	int			mark, i, j;
	unsigned	at, notex;
	dheader_t	*h;
	texture_t	*tx;
	mtexinfo_t	*ti;
	msurface_t	*s;
	mleaf_t		*l;
	mnode_t		*n;
	int			nsub = 1, frameslot = 0;

	h = (dheader_t *)f;

	// id's loader, into a scratch model on the hunk
	mark = Hunk_LowMark ();
	memset (&m, 0, sizeof(m));
	strcpy (m.name, name);
	loadmodel = &m;
	COM_FileBase (name, loadname);
	mod_inflash = true;
	mod_flashlen = flen;
	Mod_LoadBrushLumps (&m, f);
	mod_inflash = false;

	// the model itself first, then every hunk structure it points at
#ifdef QOG_MOD_FLASH
	// QuakeOG: with QOG_MOD_FLASH the map used in place, finished the way
	// Mod_FinishBrushModel leaves it, then its inline models *1, *2, ...
	if (m.numsubmodels > 1)
		nsub = m.numsubmodels;
	if (Mod_IsItemBox (name))
	{
		if (bk_nextframe == QOG_MOD_FRAMES_BAKED)
		{
			Con_Printf ("qog_bake: more than %i item box maps\n", QOG_MOD_FRAMES_BAKED - 1);
			Hunk_FreeToLowMark (mark);
			loadmodel = NULL;
			return 0;
		}
		frameslot = bk_nextframe++;
	}
#endif
	at = bk_base + Bake_Reserve (nsub * sizeof(model_t));
#ifdef QOG_BAKE_STUBS
	bk_detach = bk_stubs;	// QuakeOG: the lumps below are copied from the pack too
#endif
	Bake_Copy (m.planes, m.numplanes * sizeof(mplane_t));
	if (m.textures)
	{
		Bake_Copy (m.textures, m.numtextures * sizeof(texture_t *));
		for (i=0 ; i<m.numtextures ; i++)
			if (m.textures[i])
				Bake_Copy (m.textures[i], sizeof(texture_t));
#ifdef QOG_BAKE_STUBS
		// QuakeOG: each distinct texture's pixels once in the image
		for (i=0 ; i<m.numtextures && bk_detach ; i++)
		{
			byte	*px;
			int		size;

			if (!m.textures[i])
				continue;
			Bake_TexPixels (m.textures[i], &px, &size);
			Bake_CopyShared (px, size);
		}
#endif
	}
	notex = 0;
	for (i=0 ; i<m.numtexinfo ; i++)
		if (m.texinfo[i].texture == r_notexture_mip && !notex)	// the checkerboard, with its pixels
			notex = Bake_Copy (r_notexture_mip, sizeof(texture_t) + 16*16+8*8+4*4+2*2);
	Bake_Copy (m.texinfo, m.numtexinfo * sizeof(mtexinfo_t));
	Bake_Copy (m.surfaces, m.numsurfaces * sizeof(msurface_t));
	Bake_Copy (m.leafs, m.numleafs * sizeof(mleaf_t));
	Bake_Copy (m.nodes, m.numnodes * sizeof(mnode_t));
	Bake_Copy (m.submodels, m.numsubmodels * sizeof(dmodel_t));
	Bake_Copy (m.hulls[0].clipnodes, m.numnodes * sizeof(dclipnode_t));
	// read in place from the pack when aligned, otherwise copied too
	Bake_Copy (m.vertexes, m.numvertexes * sizeof(mvertex_t));
	Bake_Copy (m.edges, (m.numedges + 1) * sizeof(medge_t));
	Bake_Copy (m.surfedges, m.numsurfedges * sizeof(int));
	Bake_Copy (m.clipnodes, m.numclipnodes * sizeof(dclipnode_t));
	Bake_Copy (m.marksurfaces, m.nummarksurfaces * sizeof(marksurf_t));
	Bake_Copy (m.lightdata, LittleLong (h->lumps[LUMP_LIGHTING].filelen));
	Bake_Copy (m.visdata, LittleLong (h->lumps[LUMP_VISIBILITY].filelen));
	Bake_Copy (m.entities, LittleLong (h->lumps[LUMP_ENTITIES].filelen));

	// translate the pointers inside the copies
	if (m.textures)
	{
		texture_t	**list = BAKE_AT(texture_t *, Bake_Ptr (m.textures));

		for (i=0 ; i<m.numtextures ; i++)
		{
			if (!m.textures[i])
				continue;
			tx = BAKE_AT(texture_t, Bake_Ptr (m.textures[i]));
			BAKE_FIX(list[i]);
			BAKE_FIX(tx->anim_next);
			BAKE_FIX(tx->alternate_anims);
			for (j=0 ; j<MIPLEVELS ; j++)
				BAKE_FIX(tx->mips[j]);
		}
	}
	if (notex)
	{
		tx = BAKE_AT(texture_t, notex);
		for (j=0 ; j<MIPLEVELS ; j++)
			BAKE_FIX(tx->mips[j]);
	}
	if (m.numtexinfo)
	{
		ti = BAKE_AT(mtexinfo_t, Bake_Ptr (m.texinfo));
		for (i=0 ; i<m.numtexinfo ; i++, ti++)
			BAKE_FIX(ti->texture);
	}
	if (m.numsurfaces)
	{
		s = BAKE_AT(msurface_t, Bake_Ptr (m.surfaces));
		for (i=0 ; i<m.numsurfaces ; i++, s++)
		{
			BAKE_FIX(s->samples);
#ifndef QOG_SURF_INDEX
			BAKE_FIX(s->plane);
			BAKE_FIX(s->texinfo);
#endif
		}
	}
	if (m.numleafs)
	{
		l = BAKE_AT(mleaf_t, Bake_Ptr (m.leafs));
		for (i=0 ; i<m.numleafs ; i++, l++)
		{
			BAKE_FIX(l->parent);
			BAKE_FIX(l->compressed_vis);
#ifndef QOG_LEAF_PACKED
			BAKE_FIX(l->firstmarksurface);
#endif
		}
	}
	if (m.numnodes)
	{
		n = BAKE_AT(mnode_t, Bake_Ptr (m.nodes));
		for (i=0 ; i<m.numnodes ; i++, n++)
		{
			BAKE_FIX(n->parent);
			BAKE_FIX(n->plane);
			BAKE_FIX(n->children[0]);
			BAKE_FIX(n->children[1]);
		}
	}
#ifndef QOG_MARKSURF16
	if (m.nummarksurfaces && !COM_InFlash (m.marksurfaces))
	{
		marksurf_t	*mk = BAKE_AT(marksurf_t, Bake_Ptr (m.marksurfaces));

		for (i=0 ; i<m.nummarksurfaces ; i++)
			BAKE_FIX(mk[i]);
	}
#endif

	// the model, with the per-frame arrays and the cache left empty
	bm = BAKE_AT(model_t, at);
#ifdef QOG_MOD_FLASH
	for (i=0 ; i<nsub ; i++)
	{
		model_t	t = m;

		t.numframes = 2;		// as Mod_FinishBrushModel sets them
		t.flags = 0;
		if (i < m.numsubmodels)
			Mod_SetSubmodel (&t, i);
		if (i)
		{
			memset (t.name, 0, sizeof(t.name));
			sprintf (t.name, "*%i", i);
		}
		t.frameslot = frameslot;
		bm[i] = t;
		Bake_FixModel (&bm[i]);
	}
#else
	*bm = m;
	Bake_FixModel (bm);
#endif
#ifdef QOG_BAKE_STUBS
	bk_detach = false;
#endif

	Hunk_FreeToLowMark (mark);
	loadmodel = NULL;
	return at;
}

/*
=================
Bake_Alias

An alias model: the relocatable block id's loader puts in the cache,
copied whole. Its offsets are relative to the header, and flashbase
points into the pack.
=================
*/
static unsigned Bake_Alias (char *name, byte *f, int flen)
{
	model_t		m;
	unsigned	at, data;

	memset (&m, 0, sizeof(m));
	strcpy (m.name, name);
	loadmodel = &m;
	COM_FileBase (name, loadname);
	mod_inflash = true;
	mod_flashlen = flen;
	Mod_LoadAliasModel (&m, f);
	mod_inflash = false;
	loadmodel = NULL;
	if (!m.cache.data)
		return 0;

	at = bk_base + Bake_Reserve (sizeof(model_t));
	data = bk_base + Bake_Reserve (mod_aliassize);
	memcpy (BAKE_AT(byte, data), m.cache.data, mod_aliassize);
	Cache_Free (&m.cache);
	m.cache.data = (void *)(size_t)data;
	*BAKE_AT(model_t, at) = m;
	return at;
}

/*
=================
Bake_Sprite

A sprite: everything its loader puts on the hunk, copied as one range,
then the frame and interval pointers translated.
=================
*/
static unsigned Bake_Sprite (char *name, byte *f, int flen)
{
	model_t			m;
	int				mark, i, j;
	unsigned		at;
	byte			*end;
	msprite_t		*ps;
	mspritegroup_t	*pg;

	mark = Hunk_LowMark ();
	memset (&m, 0, sizeof(m));
	strcpy (m.name, name);
	loadmodel = &m;
	COM_FileBase (name, loadname);
	Mod_LoadSpriteModel (&m, f);
	loadmodel = NULL;
	end = Hunk_AllocName (16, "bakeend");	// the hunk's end, past the last frame

	at = bk_base + Bake_Reserve (sizeof(model_t));
	Bake_Copy (m.cache.data, end - (byte *)m.cache.data);
	ps = BAKE_AT(msprite_t, Bake_Ptr (m.cache.data));
	for (i=0 ; i<ps->numframes ; i++)
	{
		if (ps->frames[i].type != SPR_SINGLE)
		{
			pg = BAKE_AT(mspritegroup_t, Bake_Ptr (ps->frames[i].frameptr));
			BAKE_FIX(pg->intervals);
			for (j=0 ; j<pg->numframes ; j++)
				BAKE_FIX(pg->frames[j]);
		}
		BAKE_FIX(ps->frames[i].frameptr);
	}
	BAKE_FIX(m.cache.data);
	*BAKE_AT(model_t, at) = m;
	Hunk_FreeToLowMark (mark);
	return at;
}

/*
=================
Bake_Model

Bakes one map, alias model or sprite from a mapped pack and appends its
block and directory entry. False if it could not be baked (nothing is
then left in the image).
=================
*/
static qboolean Bake_Model (char *name, qogbake_entry_t *e)
{
	byte		*f;
	int			flen, start, id;
	unsigned	at;
#ifdef QOG_BAKE_STUBS
	int			shared, sharedbytes, savedbytes;
#endif

	f = COM_FlashFile (name, &flen);
	if (!f || (((int)f) & 3) || flen < 4 || r_pixbytes != 1)
	{
		Con_Printf ("qog_bake: %s is not in a mapped pack, skipped\n", name);
		return false;
	}
	start = bk_len;
	bk_numranges = 0;
	bk_failed = false;
#ifdef QOG_BAKE_STUBS
	shared = bk_numshared;
	sharedbytes = bk_sharedbytes;
	savedbytes = bk_savedbytes;
#endif
	id = LittleLong (*(int *)f);
	if (id == BSPVERSION)
		at = Bake_Map (name, f, flen);
	else if (id == IDPOLYHEADER)
		at = Bake_Alias (name, f, flen);
	else if (id == IDSPRITEHEADER)
		at = Bake_Sprite (name, f, flen);
	else
		at = 0;
	// QuakeOG: an alias model's frames and skins stay in the pack, which
	// must then lie below the image (-bakestubs with -bakebase)
	if (at && id == IDPOLYHEADER && (unsigned)(size_t)(f + flen) > bk_limit)
	{
		Con_Printf ("qog_bake: %s lies past the image's address\n", name);
		at = 0;
	}
	if (!at || bk_failed)
	{
		Con_Printf ("qog_bake: %s could not be baked, skipped\n", name);
		bk_len = start;
#ifdef QOG_BAKE_STUBS
		bk_numshared = shared;	// the shared pixels it added are gone too
		bk_sharedbytes = sharedbytes;
		bk_savedbytes = savedbytes;
#endif
		return false;
	}
	memset (e, 0, sizeof(*e));
	strcpy (e->name, name);
	e->model = at;
	e->size = bk_len - start;
	e->fileaddr = (unsigned)(size_t)f;
	e->filelen = flen;
	e->filehash = Mod_FileHash (f, flen);
#ifdef QOG_BAKE_STUBS
	if (bk_stubs && id == BSPVERSION)
	{	// QuakeOG: the map is used through its stub, wherever the pak puts it
		qogbake_stub_t	st;

		Bake_MakeStub (f, flen, &st);
		e->fileaddr = 0;
		e->filelen = sizeof(st);
		e->filehash = Mod_FileHash ((byte *)&st, sizeof(st));
	}
#endif
	return true;
}

/*
=================
Bake_Check

qog_bake check: loads every baked map again with id's loader and compares
it with the baked copy, field by field, pointers as indices into their
arrays (or as the same flash address). Prints each difference found.
=================
*/
static int			bc_errors;
static const char	*bc_map;

static void Bake_Diff (const char *what, int i)
{
	if (bc_errors++ < 20)
		Con_Printf ("qog_bake check: %s differs in %s, item %i\n", what, bc_map, i);
}

// the same element of two arrays, or the same address outside them
static qboolean Bake_SamePtr (void *a, void *abase, void *b, void *bbase, int size, int count)
{
	int		ia, ib;

	if (!a || !b)
		return a == b;
	ia = (byte *)a - (byte *)abase;
	ib = (byte *)b - (byte *)bbase;
	if (abase && ia >= 0 && ia < size * count)
		return ia == ib;
	return a == b;
}

#define BC_SAME(fa, fb, abase, bbase, type, count) \
	Bake_SamePtr ((void *)(fa), (void *)(abase), (void *)(fb), (void *)(bbase), sizeof(type), (count))

static int Bake_TexIndex (model_t *m, texture_t *t)
{
	int		i;

	for (i=0 ; i<m->numtextures ; i++)
		if (m->textures[i] == t)
			return i;
	return t ? -2 : -1;
}

static qboolean Bake_SameSpriteFrame (mspriteframe_t *a, mspriteframe_t *b)
{
	return a->width == b->width && a->height == b->height && a->up == b->up && a->down == b->down
		&& a->left == b->left && a->right == b->right
		&& !memcmp (a->pixels, b->pixels, a->width * a->height);
}

// an alias model or sprite against its baked copy b
static void Bake_CheckModel (model_t *b, byte *f, int flen)
{
	model_t			m;
	int				mark, i, k;
	msprite_t		*pa, *pb;
	mspritegroup_t	*ga, *gb;

	mark = Hunk_LowMark ();
	memset (&m, 0, sizeof(m));
	strcpy (m.name, b->name);
	loadmodel = &m;
	COM_FileBase (b->name, loadname);
	mod_inflash = (b->type == mod_alias);
	mod_flashlen = flen;
	if (b->type == mod_alias)
		Mod_LoadAliasModel (&m, f);
	else
		Mod_LoadSpriteModel (&m, f);
	mod_inflash = false;
	loadmodel = NULL;

	if (m.type != b->type || m.numframes != b->numframes || m.synctype != b->synctype
		|| m.flags != b->flags || memcmp (m.mins, b->mins, sizeof(m.mins))
		|| memcmp (m.maxs, b->maxs, sizeof(m.maxs)) || m.radius != b->radius || !m.cache.data)
		Bake_Diff ("model fields", 0);
	else if (b->type == mod_alias)
	{
		// id's loader copies the hunk span from the header on, which runs one
		// hunk header (16 bytes) past its last block: those bytes are unused
		for (i=0 ; i<mod_aliassize - 16 ; i++)
			if (((byte *)m.cache.data)[i] != ((byte *)b->cache.data)[i])
			{
				Bake_Diff ("alias model data", i);
				break;
			}
	}
	else
	{
		pa = m.cache.data;
		pb = b->cache.data;
		if (pa->type != pb->type || pa->maxwidth != pb->maxwidth || pa->maxheight != pb->maxheight
			|| pa->numframes != pb->numframes || pa->beamlength != pb->beamlength)
			Bake_Diff ("sprite header", 0);
		else
			for (i=0 ; i<pa->numframes ; i++)
			{
				if (pa->frames[i].type != pb->frames[i].type)
					Bake_Diff ("sprite frame type", i);
				else if (pa->frames[i].type == SPR_SINGLE)
				{
					if (!Bake_SameSpriteFrame (pa->frames[i].frameptr, pb->frames[i].frameptr))
						Bake_Diff ("sprite frame", i);
				}
				else
				{
					ga = (mspritegroup_t *)pa->frames[i].frameptr;
					gb = (mspritegroup_t *)pb->frames[i].frameptr;
					if (ga->numframes != gb->numframes
						|| memcmp (ga->intervals, gb->intervals, ga->numframes * sizeof(float)))
						Bake_Diff ("sprite group", i);
					else
						for (k=0 ; k<ga->numframes ; k++)
							if (!Bake_SameSpriteFrame (ga->frames[k], gb->frames[k]))
								Bake_Diff ("sprite group frame", i);
				}
			}
	}
	if (b->type == mod_alias && m.cache.data)
		Cache_Free (&m.cache);
	Hunk_FreeToLowMark (mark);
}

#ifdef QOG_BAKE_STUBS
/*
=================
Bake_SrcFile

QuakeOG: a map whose pak entry is a stub is checked against the full map
read from the pak given with -bakesrc (the pak it was baked from). The
caller frees it.
=================
*/
static byte *Bake_SrcFile (char *name, int *len)
{
	int		i, h, n, hd[3], ent[16];
	byte	*buf;

	i = COM_CheckParm ("-bakesrc");
	if (!i || i >= com_argc - 1 || Sys_FileOpenRead (com_argv[i + 1], &h) < 0)
		return NULL;
	buf = NULL;
	if (Sys_FileRead (h, hd, sizeof(hd)) == sizeof(hd) && !memcmp (hd, "PACK", 4))
	{
		n = LittleLong (hd[2]) / (int)sizeof(ent);
		for (i=0 ; i<n ; i++)
		{
			Sys_FileSeek (h, LittleLong (hd[1]) + i * sizeof(ent));
			if (Sys_FileRead (h, ent, sizeof(ent)) != sizeof(ent))
				break;
			if (strncmp ((char *)ent, name, 56))
				continue;
			*len = LittleLong (ent[15]);
			buf = malloc (*len + 16);
			Sys_FileSeek (h, LittleLong (ent[14]));
			if (!buf || Sys_FileRead (h, buf, *len) != *len)
			{
				free (buf);
				buf = NULL;
			}
			break;
		}
	}
	Sys_FileClose (h);
	return buf;
}

// QuakeOG: the same bytes at the same offset into their lumps
static qboolean Bake_SameIn (byte *a, byte *abase, byte *b, byte *bbase)
{
	if (!a || !b)
		return a == b;
	return a - abase == b - bbase;
}
#define BC_SAMEIN(a, ab, b, bb)	Bake_SameIn ((byte *)(a), (byte *)(ab), (byte *)(b), (byte *)(bb))
#else
#define BC_SAMEIN(a, ab, b, bb)	true
#endif

static void Bake_Check (void)
{
	qogbake_entry_t	*e;
	model_t			m, *b;
	int				i, j, mark, flen, total;
	byte			*f;
	byte			*src = NULL;	// QuakeOG: a stubbed map's full file (-bakesrc)
	dheader_t		*hdr;

	if (!mod_baketried)
	{
		mod_baketried = true;
		Mod_BakeOpen ();
	}
	if (!mod_bake || sv.active || cls.state == ca_connected)
	{
		Con_Printf ("qog_bake check: needs the image mapped (no -nobake) and no map loaded\n");
		return;
	}
	total = 0;
	e = (qogbake_entry_t *)((byte *)mod_bake + mod_bake->dirofs);
	for (j=0 ; j<mod_bake->nummaps ; j++, e++)
	{
		bc_errors = 0;
		bc_map = e->name;
		b = (model_t *)e->model;
		f = COM_FlashFile (e->name, &flen);
		if (!f || ((unsigned)(size_t)f != e->fileaddr && !BAKE_ANYADDR (e)) || (unsigned)flen != e->filelen
			|| Mod_FileHash (f, flen) != e->filehash)
		{
			Bake_Diff ("source file", 0);
			total += bc_errors;
			continue;
		}
#ifdef QOG_BAKE_STUBS
		if (BAKE_ANYADDR (e))
		{	// QuakeOG: a stub, compare with the map it stands for
			qogbake_stub_t	*st = (qogbake_stub_t *)f;

			src = (flen == sizeof(*st) && st->ident == QOGB_STUB_IDENT) ? Bake_SrcFile (e->name, &flen) : NULL;
			if (!src || (unsigned)flen != st->filelen || Mod_FileHash (src, flen) != st->filehash)
			{
				Con_Printf ("qog_bake check: %s needs -bakesrc <the pak it was baked from>\n", e->name);
				Bake_Diff ("stub source file", 0);
				total += bc_errors;
				free (src);
				src = NULL;
				continue;
			}
			f = src;
		}
#endif
		hdr = (dheader_t *)f;
		if (b->type != mod_brush)
		{
			Bake_CheckModel (b, f, flen);
			if (bc_errors)
				Con_Printf ("qog_bake check: %s has %i differences\n", e->name, bc_errors);
			total += bc_errors;
			continue;
		}
		mark = Hunk_LowMark ();
		memset (&m, 0, sizeof(m));
		strcpy (m.name, e->name);
		loadmodel = &m;
		COM_FileBase (e->name, loadname);
		mod_inflash = true;
		mod_flashlen = flen;
		Mod_LoadBrushLumps (&m, f);
		mod_inflash = false;
#ifdef QOG_MOD_FLASH
		// QuakeOG: the baked map is finished (Bake_Map) and its inline
		// models follow it, each the world with its own fields
		{
			static model_t	fin, bu;

			fin = m;
			fin.numframes = 2;
			fin.flags = 0;
			if (m.numsubmodels)
				Mod_SetSubmodel (&fin, 0);
			if (fin.numframes != b->numframes || fin.flags != b->flags
				|| fin.firstmodelsurface != b->firstmodelsurface
				|| fin.nummodelsurfaces != b->nummodelsurfaces || fin.numleafs != b->numleafs
				|| memcmp (fin.mins, b->mins, sizeof(fin.mins)) || memcmp (fin.maxs, b->maxs, sizeof(fin.maxs))
				|| fin.radius != b->radius)
				Bake_Diff ("finished model", 0);
			for (i=0 ; i<MAX_MAP_HULLS ; i++)
				if (fin.hulls[i].firstclipnode != b->hulls[i].firstclipnode
					|| fin.hulls[i].lastclipnode != b->hulls[i].lastclipnode)
					Bake_Diff ("finished hull", i);
			if (b->frameslot != 0 && !(Mod_IsItemBox (e->name)
				&& b->frameslot > 0 && b->frameslot < QOG_MOD_FRAMES_BAKED))
				Bake_Diff ("frameslot", 0);
			for (i=1 ; i<m.numsubmodels ; i++)
			{
				fin = *b;
				Mod_SetSubmodel (&fin, i);
				memset (fin.name, 0, sizeof(fin.name));
				sprintf (fin.name, "*%i", i);
				if (memcmp (&fin, &b[i], sizeof(model_t)))
					Bake_Diff ("inline model", i);
			}
			// the rest compares what the loader built: undo the finishing
			bu = *b;
			bu.numframes = m.numframes;
			bu.flags = m.flags;
			bu.firstmodelsurface = m.firstmodelsurface;
			bu.nummodelsurfaces = m.nummodelsurfaces;
			bu.numleafs = m.numleafs;
			VectorCopy (m.mins, bu.mins);
			VectorCopy (m.maxs, bu.maxs);
			bu.radius = m.radius;
			for (i=0 ; i<MAX_MAP_HULLS ; i++)
			{
				bu.hulls[i].firstclipnode = m.hulls[i].firstclipnode;
				bu.hulls[i].lastclipnode = m.hulls[i].lastclipnode;
			}
			b = &bu;
		}
#endif

		// counts, bounds and lumps read in place
		if (m.type != b->type || m.numsubmodels != b->numsubmodels || m.numplanes != b->numplanes
			|| m.numleafs != b->numleafs || m.numvertexes != b->numvertexes || m.numedges != b->numedges
			|| m.numnodes != b->numnodes || m.numtexinfo != b->numtexinfo || m.numsurfaces != b->numsurfaces
			|| m.numsurfedges != b->numsurfedges || m.numclipnodes != b->numclipnodes
			|| m.nummarksurfaces != b->nummarksurfaces || m.numtextures != b->numtextures
			|| m.firstmodelsurface != b->firstmodelsurface || m.nummodelsurfaces != b->nummodelsurfaces
			|| m.numframes != b->numframes || m.flags != b->flags || m.synctype != b->synctype
			|| memcmp (m.mins, b->mins, sizeof(m.mins)) || memcmp (m.maxs, b->maxs, sizeof(m.maxs))
			|| m.radius != b->radius)
			Bake_Diff ("model counts", 0);
		if ((COM_InFlash (m.vertexes) && m.vertexes != b->vertexes)
			|| memcmp (m.vertexes, b->vertexes, m.numvertexes * sizeof(mvertex_t)))
			Bake_Diff ("vertexes", 0);
		if (memcmp (m.edges, b->edges, m.numedges * sizeof(medge_t)))
			Bake_Diff ("edges", 0);
		if (memcmp (m.surfedges, b->surfedges, m.numsurfedges * sizeof(int)))
			Bake_Diff ("surfedges", 0);
		if (memcmp (m.clipnodes, b->clipnodes, m.numclipnodes * sizeof(dclipnode_t)))
			Bake_Diff ("clipnodes", 0);
#ifdef QOG_MARKSURF16
		if (memcmp (m.marksurfaces, b->marksurfaces, m.nummarksurfaces * sizeof(marksurf_t)))
			Bake_Diff ("marksurfaces", 0);
#endif
#ifdef QOG_BAKE_STUBS
		if (src)
		{	// QuakeOG: copied into the image, nothing left pointing at the pack
#define BC_COPY(a, b, lump)	((!(a)) != (!(b)) || ((a) && memcmp ((a), (b), LittleLong (hdr->lumps[lump].filelen))))
			if (BC_COPY (m.lightdata, b->lightdata, LUMP_LIGHTING) || BC_COPY (m.visdata, b->visdata, LUMP_VISIBILITY)
				|| BC_COPY (m.entities, b->entities, LUMP_ENTITIES))
				Bake_Diff ("light, vis or entity lump", 0);
			if ((b->lightdata && !MOD_INBAKE (b->lightdata)) || (b->visdata && !MOD_INBAKE (b->visdata))
				|| (b->entities && !MOD_INBAKE (b->entities)) || (b->numvertexes && !MOD_INBAKE (b->vertexes))
				|| (b->numedges && !MOD_INBAKE (b->edges)) || (b->numsurfedges && !MOD_INBAKE (b->surfedges))
				|| (b->numclipnodes && !MOD_INBAKE (b->clipnodes))
				|| (b->nummarksurfaces && !MOD_INBAKE (b->marksurfaces)))
				Bake_Diff ("lump left in the pack", 0);
		}
		else
#endif
		if (m.lightdata != b->lightdata || m.visdata != b->visdata || m.entities != b->entities)
			Bake_Diff ("light, vis or entity lump", 0);
		if (memcmp (m.planes, b->planes, m.numplanes * sizeof(mplane_t)))
			Bake_Diff ("planes", 0);
		if (memcmp (m.submodels, b->submodels, m.numsubmodels * sizeof(dmodel_t)))
			Bake_Diff ("submodels", 0);

		for (i=0 ; i<MAX_MAP_HULLS ; i++)
		{
			hull_t	*ha = &m.hulls[i], *hb = &b->hulls[i];

			if (ha->firstclipnode != hb->firstclipnode || ha->lastclipnode != hb->lastclipnode
				|| memcmp (ha->clip_mins, hb->clip_mins, sizeof(vec3_t))
				|| memcmp (ha->clip_maxs, hb->clip_maxs, sizeof(vec3_t))
				|| !BC_SAME (ha->planes, hb->planes, m.planes, b->planes, mplane_t, m.numplanes))
				Bake_Diff ("hull", i);
			if ((ha->clipnodes == NULL) != (hb->clipnodes == NULL)
				|| (ha->clipnodes && ha->lastclipnode >= 0 && memcmp (ha->clipnodes, hb->clipnodes,
					(ha->lastclipnode + 1) * sizeof(dclipnode_t))))
				Bake_Diff ("hull clipnodes", i);
		}

		for (i=0 ; i<m.numtextures ; i++)
		{
			texture_t	*ta = m.textures[i], *tb = b->textures[i];

			if (!ta || !tb)
			{
				if (ta != tb)
					Bake_Diff ("texture", i);
				continue;
			}
			if (memcmp (ta->name, tb->name, sizeof(ta->name)) || ta->width != tb->width
				|| ta->height != tb->height || ta->anim_total != tb->anim_total
				|| ta->anim_min != tb->anim_min || ta->anim_max != tb->anim_max
				|| Bake_TexIndex (&m, ta->anim_next) != Bake_TexIndex (b, tb->anim_next)
				|| Bake_TexIndex (&m, ta->alternate_anims) != Bake_TexIndex (b, tb->alternate_anims)
				|| (!src && memcmp (ta->mips, tb->mips, sizeof(ta->mips))))
				Bake_Diff ("texture", i);
#ifdef QOG_BAKE_STUBS
			else if (src)	// QuakeOG: the pixels themselves, shared in the image
			{
				int		mip;

				for (mip=0 ; mip<MIPLEVELS ; mip++)
					if (!MOD_INBAKE (tb->mips[mip])
						|| memcmp (ta->mips[mip], tb->mips[mip], (ta->width >> mip) * (ta->height >> mip)))
						break;
				if (mip < MIPLEVELS)
					Bake_Diff ("texture pixels", i);
			}
#endif
		}

		for (i=0 ; i<m.numtexinfo ; i++)
		{
			mtexinfo_t	*ta = &m.texinfo[i], *tb = &b->texinfo[i];
			texture_t	*xa = ta->texture, *xb = tb->texture;

			if (memcmp (ta->vecs, tb->vecs, sizeof(ta->vecs)) || ta->mipadjust != tb->mipadjust
				|| ta->flags != tb->flags || Bake_TexIndex (&m, xa) != Bake_TexIndex (b, xb))
				Bake_Diff ("texinfo", i);
			else if (Bake_TexIndex (&m, xa) == -2)	// the checkerboard
			{
				if (xa != r_notexture_mip || memcmp (xb->name, xa->name, sizeof(xa->name))
					|| xb->width != xa->width || memcmp (xb->mips[0], xa->mips[0], 16*16+8*8+4*4+2*2))
					Bake_Diff ("texinfo checkerboard", i);
			}
		}

		for (i=0 ; i<m.numsurfaces ; i++)
		{
			msurface_t	*sa = &m.surfaces[i], *sb = &b->surfaces[i];

			if (sa->flags != sb->flags || sa->firstedge != sb->firstedge || sa->numedges != sb->numedges
				|| memcmp (sa->texturemins, sb->texturemins, sizeof(sa->texturemins))
				|| memcmp (sa->extents, sb->extents, sizeof(sa->extents))
				|| memcmp (sa->styles, sb->styles, sizeof(sa->styles))
				|| (src ? !BC_SAMEIN (sa->samples, m.lightdata, sb->samples, b->lightdata)
					: sa->samples != sb->samples)	// QuakeOG: src, a stubbed map
#ifdef QOG_SURF_INDEX
				|| sa->planenum != sb->planenum || sa->texinfonum != sb->texinfonum
#else
				|| !BC_SAME (sa->plane, sb->plane, m.planes, b->planes, mplane_t, m.numplanes)
				|| !BC_SAME (sa->texinfo, sb->texinfo, m.texinfo, b->texinfo, mtexinfo_t, m.numtexinfo)
#endif
				)
				Bake_Diff ("surface", i);
		}

		for (i=0 ; i<m.numleafs ; i++)
		{
			mleaf_t		*la = &m.leafs[i], *lb = &b->leafs[i];

			if (la->contents != lb->contents || memcmp (la->minmaxs, lb->minmaxs, sizeof(la->minmaxs))
				|| !BC_SAME (la->parent, lb->parent, m.nodes, b->nodes, mnode_t, m.numnodes)
				|| (src ? !BC_SAMEIN (la->compressed_vis, m.visdata, lb->compressed_vis, b->visdata)
					: la->compressed_vis != lb->compressed_vis)	// QuakeOG: src, a stubbed map
				|| la->nummarksurfaces != lb->nummarksurfaces
#ifdef QOG_LEAF_PACKED
				|| la->firstmarksurface != lb->firstmarksurface
#else
				|| !BC_SAME (la->firstmarksurface, lb->firstmarksurface, m.marksurfaces, b->marksurfaces, marksurf_t, m.nummarksurfaces)
#endif
				|| memcmp (la->ambient_sound_level, lb->ambient_sound_level, sizeof(la->ambient_sound_level)))
				Bake_Diff ("leaf", i);
		}

		for (i=0 ; i<m.numnodes ; i++)
		{
			mnode_t		*na = &m.nodes[i], *nb = &b->nodes[i];
			int			c;

			if (na->contents != nb->contents || memcmp (na->minmaxs, nb->minmaxs, sizeof(na->minmaxs))
				|| !BC_SAME (na->parent, nb->parent, m.nodes, b->nodes, mnode_t, m.numnodes)
				|| !BC_SAME (na->plane, nb->plane, m.planes, b->planes, mplane_t, m.numplanes)
				|| na->firstsurface != nb->firstsurface || na->numsurfaces != nb->numsurfaces)
				Bake_Diff ("node", i);
			for (c=0 ; c<2 ; c++)
			{
				if (na->children[c]->contents < 0
					? !BC_SAME (na->children[c], nb->children[c], m.leafs, b->leafs, mleaf_t, m.numleafs)
					: !BC_SAME (na->children[c], nb->children[c], m.nodes, b->nodes, mnode_t, m.numnodes))
					Bake_Diff ("node child", i);
			}
		}

		Hunk_FreeToLowMark (mark);
		loadmodel = NULL;
		free (src);		// QuakeOG
		src = NULL;
		if (bc_errors)
			Con_Printf ("qog_bake check: %s has %i differences\n", e->name, bc_errors);
		total += bc_errors;
	}
	Con_Printf ("qog_bake check: %i models compared, %i differences\n", mod_bake->nummaps, total);
}

static void Bake_AddName (char *name)
{
	int		i, n = strlen (name);

	if (n < 9 || n >= sizeof(bk_names[0]))
		return;
	if (!(!Q_strncmp (name, "maps/", 5) && !Q_strcmp (name + n - 4, ".bsp"))
		&& !(!Q_strncmp (name, "progs/", 6) && !Q_strcmp (name + n - 4, ".mdl"))
		&& !(!Q_strncmp (name, "progs/", 6) && !Q_strcmp (name + n - 4, ".spr")))
		return;
	for (i=0 ; i<bk_numnames ; i++)
		if (!Q_strcmp (bk_names[i], name))
			return;
	if (bk_numnames < 512)
		strcpy (bk_names[bk_numnames++], name);
}

/*
=================
Mod_Bake_f

qog_bake [name ...]: bakes the named maps ("e1m1") and models
("progs/player.mdl"), or with no argument every maps/*.bsp, progs/*.mdl
and progs/*.spr in the packs, into <gamedir>/qogbake.bin for the address
the image will be mapped at in a run with the same packs. Run it with
-nobake and before any map is loaded. qog_bake check compares the image
with a fresh load (see Bake_Check).
=================
*/
static void Mod_Bake_f (void)
{
	qogbake_entry_t		dir[512];
	qogbake_header_t	*hd;
	char				path[MAX_OSPATH];
	int					i, n, hnd, dirofs;

	if (Cmd_Argc () == 2 && !Q_strcmp (Cmd_Argv (1), "check"))
	{
		Bake_Check ();
		return;
	}
	if (mod_bake || sv.active || cls.state == ca_connected)
	{
		Con_Printf ("qog_bake: run it with -nobake, before any map is loaded\n");
		return;
	}
	bk_numnames = 0;
	if (Cmd_Argc () > 1)
	{
		for (i=1 ; i<Cmd_Argc () ; i++)
		{
			if (strchr (Cmd_Argv (i), '.'))
				Q_strncpy (path, Cmd_Argv (i), sizeof(path) - 1);	// "progs/player.mdl"
			else
				sprintf (path, "maps/%s.bsp", Cmd_Argv (i));	// "e1m1"
			Bake_AddName (path);
		}
	}
	else
		COM_ForEachPackFile (Bake_AddName);

	bk_base = (unsigned)(size_t)Sys_FlashNext ();
	bk_limit = 0xFFFFFFFFu;
#ifdef QOG_BAKE_STUBS
	bk_stubs = COM_CheckParm ("-bakestubs") != 0;
	bk_numshared = bk_sharedbytes = bk_savedbytes = 0;
#endif
	// QuakeOG: -bakebase <address> builds the image for another flash address
	// (at or after the packs, 64 KB aligned), for example where a board data
	// image puts it. Pointers into the packs stay as they are mapped here.
	// With -bakestubs it may also lie inside the packs, where the board's
	// pak with stubs ends: the maps then point at nothing in the pack, and
	// any other pack pointer must lie below it (bk_limit).
	i = COM_CheckParm ("-bakebase");
	if (i && i < com_argc - 1)
	{
		unsigned	want = (unsigned)strtoul (com_argv[i + 1], NULL, 0);
		qboolean	below = false;

#ifdef QOG_BAKE_STUBS
		below = bk_stubs;
#endif
		if ((want < bk_base && !below) || (want & 0xFFFF))
		{
			Con_Printf ("qog_bake: -bakebase %08x must be 64 KB aligned and at or after %08x\n", want, bk_base);
			return;
		}
		if (want < bk_base)
			bk_limit = want;
		bk_base = want;
	}
	bk_nextframe = 1;
	bk_len = 0;
	Bake_Reserve (sizeof(qogbake_header_t));
	n = 0;
	for (i=0 ; i<bk_numnames ; i++)
	{
		if (Bake_Model (bk_names[i], &dir[n]))
		{
			Con_Printf ("qog_bake: %-20s %7i bytes\n", dir[n].name, dir[n].size);
			n++;
		}
	}
	dirofs = Bake_Reserve (n * sizeof(qogbake_entry_t));
	memcpy (bk_buf + dirofs, dir, n * sizeof(qogbake_entry_t));
	Bake_Reserve (0);	// round the size to 16

	hd = (qogbake_header_t *)bk_buf;
	hd->magic = QOGB_MAGIC;
	hd->version = QOGB_VERSION;
	hd->layout = Mod_BakeLayout ();
	hd->base = bk_base;
	hd->size = bk_len;
	hd->nummaps = n;
	hd->dirofs = dirofs;

	Mod_BakePath (path);
	hnd = Sys_FileOpenWrite (path);
	if (hnd == -1)
	{
		Con_Printf ("qog_bake: cannot write %s\n", path);
		return;
	}
	Sys_FileWrite (hnd, bk_buf, bk_len);
	Sys_FileClose (hnd);
#ifdef QOG_BAKE_STUBS
	if (bk_stubs)
		Con_Printf ("qog_bake: maps detached, texture pixels %i bytes, %i bytes shared between maps\n",
			bk_sharedbytes, bk_savedbytes);
#endif
	Con_Printf ("qog_bake: %i models, %i bytes, for address %08x, in %s\n", n, bk_len, bk_base, path);
	free (bk_buf);
	bk_buf = NULL;
	bk_max = bk_len = 0;
}
#endif	// QOG_BAKE_WRITER
#endif	// QOG_BAKE

/*
=================
Mod_LoadBrushModel
=================
*/
void Mod_LoadBrushModel (model_t *mod, void *buffer)
{
	Mod_LoadBrushLumps (mod, buffer);
	Mod_FinishBrushModel (mod);
}

/*
=================
Mod_FinishBrushModel

QuakeOG: the second half of id's Mod_LoadBrushModel, run for loaded and
baked maps alike: the per-frame arrays and the inline submodels.
=================
*/
static void Mod_FinishBrushModel (model_t *mod)
{
	int			i, numleafs;
	modframe_t	*fr;

	numleafs = loadmodel->numleafs;
#ifdef QOG_MOD_FLASH
	// QuakeOG: a baked map is finished, its numleafs is already the world
	// submodel's visleafs: the leaf arrays take the file's count, as in id's
	if (MOD_INBAKE (loadmodel))
	{
		dheader_t	*h = (dheader_t *)COM_FlashFile (loadmodel->name, NULL);

		if (!h)
			Sys_Error ("Mod_FinishBrushModel: %s is not in flash", loadmodel->name);
		numleafs = LittleLong (h->lumps[LUMP_LEAFS].filelen) / sizeof(dleaf_t);
	}
	// QuakeOG: a map loaded the normal way gets its frameslot here, a baked
	// one has it in the image (numbered by qog_bake)
	if (!MOD_INBAKE (loadmodel))
	{
		loadmodel->frameslot = 0;
		if (Mod_IsItemBox (loadmodel->name))
		{
			if (mod_nextframe == QOG_MOD_FRAMES)
				Sys_Error ("Mod_FinishBrushModel: more than %i item box maps", QOG_MOD_FRAMES - QOG_MOD_FRAMES_BAKED);
			loadmodel->frameslot = mod_nextframe++;
		}
	}
#endif
#ifdef QOG_PVS_LEAFS
	// QuakeOG: the server's PVS rows (fatpvs, checkpvs) hold QOG_PVS_LEAFS
	if (numleafs > QOG_PVS_LEAFS)
		Sys_Error ("Mod_FinishBrushModel: %s has %i leafs, QOG_PVS_LEAFS is %i", loadmodel->name, numleafs, QOG_PVS_LEAFS);
#endif
	fr = MODFRAME (loadmodel);

	// QuakeOG: the per-frame state id kept inside the structures, in RAM
	// (zeroed by the hunk, as the fields were). Copied into the submodels
	// below with the rest of the model, so they share it like the arrays.
#ifdef QOG_FRAMEBITS
	// QuakeOG: one bit per stamp, see framebits_t in model.h
	fr->surf_visframe = Mod_AllocFrameBits (loadmodel->numsurfaces);
#else
	fr->surf_visframe = Mod_AllocFrameStamps (loadmodel->numsurfaces);
#endif
#ifdef QOG_DLIGHT_SPARSE
	// QuakeOG: a bit per surface, the dlight bits of the lit surfaces are
	// kept for the frame in r_light.c (R_DlightMark)
	if (loadmodel->numsurfaces > 0xFFFF)
		Sys_Error ("Mod_FinishBrushModel: %s has %i surfaces, QOG_DLIGHT_SPARSE takes 65535", loadmodel->name, loadmodel->numsurfaces);
	fr->surf_dlightframe = Mod_AllocFrameBits (loadmodel->numsurfaces);
	fr->surf_dlightbits = NULL;
#else
	fr->surf_dlightframe = Mod_AllocFrameStamps (loadmodel->numsurfaces);
	fr->surf_dlightbits = Hunk_AllocName (loadmodel->numsurfaces * sizeof(int), loadname);
#endif
#ifdef QOG_FRAMEBITS
	fr->node_visframe = Mod_AllocFrameBits (loadmodel->numnodes);
	fr->leaf_visframe = Mod_AllocFrameBits (numleafs);
#else
	fr->node_visframe = Mod_AllocFrameStamps (loadmodel->numnodes);
	fr->leaf_visframe = Mod_AllocFrameStamps (numleafs);
#endif
#ifdef QOG_LEAF16
	// QuakeOG: the efrag heads, then a head number per leaf (all head 0,
	// which stays NULL), and 16-bit keys
	fr->leaf_efrags = (byte *)((efragheads_t *)Hunk_AllocName (sizeof(efragheads_t)
		+ numleafs, loadname) + 1);
	EFRAG_HDR (fr->leaf_efrags)->used = 1;
	fr->leaf_key = Hunk_AllocName (numleafs * sizeof(unsigned short), loadname);
#else
	fr->leaf_efrags = Hunk_AllocName (numleafs * sizeof(efrag_t *), loadname);
	fr->leaf_key = Hunk_AllocName (numleafs * sizeof(int), loadname);
#endif
#if defined(QOG_EDGECACHE_ZBUFFER)
	// QuakeOG: the renderer keeps the world's edge cache in the z-buffer
	// (R_NewMap, r_draw.c), other models' edge caches are never read
	fr->edge_cache = NULL;
#elif defined(QOG_EDGECACHE16)
	// QuakeOG: window start (an int, zero) in front of the 16-bit entries
	fr->edge_cache = (unsigned short *)((byte *)Hunk_AllocName (sizeof(int)
		+ (loadmodel->numedges + 1) * sizeof(unsigned short), loadname) + sizeof(int));
#else
	fr->edge_cache = Hunk_AllocName ((loadmodel->numedges + 1) * sizeof(unsigned int), loadname);
#endif

#ifdef QOG_MOD_FLASH
	// QuakeOG: a baked map is already finished in the image, its inline
	// models follow it there (see Bake_Map)
	if (MOD_INBAKE (mod))
	{
		modslot_t	*s;

		for (i=1 ; i<mod->numsubmodels ; i++)
		{
			s = Mod_FindSlot (mod[i].name);
			s->model = &mod[i];
			s->name = mod[i].name;
			s->needload = NL_PRESENT;
		}
		return;
	}
#endif

	mod->numframes = 2;		// regular and alternate animation
	mod->flags = 0;

//
// set up the submodels (FIXME: this is confusing)
//
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		Mod_SetSubmodel (mod, i);	// QuakeOG: id's code, moved

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[10];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasFrame
=================
*/
void * Mod_LoadAliasFrame (void * pin, int *pframeindex, int numv,
	trivertx_t *pbboxmin, trivertx_t *pbboxmax, aliashdr_t *pheader, char *name)
{
	trivertx_t		*pframe, *pinframe;
	int				i, j;
	daliasframe_t	*pdaliasframe;

	pdaliasframe = (daliasframe_t *)pin;

	strcpy (name, pdaliasframe->name);

	for (i=0 ; i<3 ; i++)
	{
	// these are byte values, so we don't have to worry about
	// endianness
		pbboxmin->v[i] = pdaliasframe->bboxmin.v[i];
		pbboxmax->v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);
	if (pheader->flashbase)
	{	// QuakeOG: vertices are bytes in the same layout, read them in place
		*pframeindex = (byte *)pinframe - pheader->flashbase;
		return (void *)(pinframe + numv);
	}
	pframe = Hunk_AllocName (numv * sizeof(*pframe), loadname);

	*pframeindex = (byte *)pframe - (byte *)pheader;

	for (j=0 ; j<numv ; j++)
	{
		int		k;

	// these are all byte values, so no need to deal with endianness
		pframe[j].lightnormalindex = pinframe[j].lightnormalindex;

		for (k=0 ; k<3 ; k++)
		{
			pframe[j].v[k] = pinframe[j].v[k];
		}
	}

	pinframe += numv;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
void * Mod_LoadAliasGroup (void * pin, int *pframeindex, int numv,
	trivertx_t *pbboxmin, trivertx_t *pbboxmax, aliashdr_t *pheader, char *name)
{
	daliasgroup_t		*pingroup;
	maliasgroup_t		*paliasgroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;
	
	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	paliasgroup = Hunk_AllocName (sizeof (maliasgroup_t) +
			(numframes - 1) * sizeof (paliasgroup->frames[0]), loadname);

	paliasgroup->numframes = numframes;

	for (i=0 ; i<3 ; i++)
	{
	// these are byte values, so we don't have to worry about endianness
		pbboxmin->v[i] = pingroup->bboxmin.v[i];
		pbboxmax->v[i] = pingroup->bboxmax.v[i];
	}

	*pframeindex = (byte *)paliasgroup - (byte *)pheader;

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	poutintervals = Hunk_AllocName (numframes * sizeof (float), loadname);

	paliasgroup->intervals = (byte *)poutintervals - (byte *)pheader;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadAliasGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadAliasFrame (ptemp,
									&paliasgroup->frames[i].frame,
									numv,
									&paliasgroup->frames[i].bboxmin,
									&paliasgroup->frames[i].bboxmax,
									pheader, name);
	}

	return ptemp;
}


/*
=================
Mod_LoadAliasSkin
=================
*/
void * Mod_LoadAliasSkin (void * pin, int *pskinindex, int skinsize,
	aliashdr_t *pheader)
{
	int		i;
	byte	*pskin, *pinskin;
	unsigned short	*pusskin;

	pinskin = (byte *)pin;
	if (pheader->flashbase)
	{	// QuakeOG: 8-bit pixels, read them in place (flash is only used when r_pixbytes is 1)
		*pskinindex = pinskin - pheader->flashbase;
		return (void *)(pinskin + skinsize);
	}
	pskin = Hunk_AllocName (skinsize * r_pixbytes, loadname);
	*pskinindex = (byte *)pskin - (byte *)pheader;

	if (r_pixbytes == 1)
	{
		Q_memcpy (pskin, pinskin, skinsize);
	}
	else if (r_pixbytes == 2)
	{
		pusskin = (unsigned short *)pskin;

		for (i=0 ; i<skinsize ; i++)
			pusskin[i] = d_8to16table[pinskin[i]];
	}
	else
	{
		Sys_Error ("Mod_LoadAliasSkin: driver set invalid r_pixbytes: %d\n",
				 r_pixbytes);
	}

	pinskin += skinsize;

	return ((void *)pinskin);
}


/*
=================
Mod_LoadAliasSkinGroup
=================
*/
void * Mod_LoadAliasSkinGroup (void * pin, int *pskinindex, int skinsize,
	aliashdr_t *pheader)
{
	daliasskingroup_t		*pinskingroup;
	maliasskingroup_t		*paliasskingroup;
	int						i, numskins;
	daliasskininterval_t	*pinskinintervals;
	float					*poutskinintervals;
	void					*ptemp;

	pinskingroup = (daliasskingroup_t *)pin;

	numskins = LittleLong (pinskingroup->numskins);

	paliasskingroup = Hunk_AllocName (sizeof (maliasskingroup_t) +
			(numskins - 1) * sizeof (paliasskingroup->skindescs[0]),
			loadname);

	paliasskingroup->numskins = numskins;

	*pskinindex = (byte *)paliasskingroup - (byte *)pheader;

	pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

	poutskinintervals = Hunk_AllocName (numskins * sizeof (float),loadname);

	paliasskingroup->intervals = (byte *)poutskinintervals - (byte *)pheader;

	for (i=0 ; i<numskins ; i++)
	{
		*poutskinintervals = LittleFloat (pinskinintervals->interval);
		if (*poutskinintervals <= 0)
			Sys_Error ("Mod_LoadAliasSkinGroup: interval<=0");

		poutskinintervals++;
		pinskinintervals++;
	}

	ptemp = (void *)pinskinintervals;

	for (i=0 ; i<numskins ; i++)
	{
		ptemp = Mod_LoadAliasSkin (ptemp,
				&paliasskingroup->skindescs[i].skin, skinsize, pheader);
	}

	return ptemp;
}


/*
=================
Mod_LoadAliasModel
=================
*/
void Mod_LoadAliasModel (model_t *mod, void *buffer)
{
	int					i;
	mdl_t				*pmodel, *pinmodel;
	stvert_t			*pstverts, *pinstverts;
	aliashdr_t			*pheader;
	mtriangle_t			*ptri;
	dtriangle_t			*pintriangles;
	int					version, numframes, numskins;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	maliasskindesc_t	*pskindesc;
	int					skinsize;
	int					start, end, total;
	
	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t) + (LittleLong (pinmodel->numframes) - 1) *
			 sizeof (pheader->frames[0]) +
			sizeof (mdl_t) +
			LittleLong (pinmodel->numverts) * sizeof (stvert_t);
	if (!mod_inflash)	// QuakeOG: from flash the triangles are read in place
		size += LittleLong (pinmodel->numtris) * sizeof (mtriangle_t);

	pheader = Hunk_AllocName (size, loadname);
	pheader->flashbase = mod_inflash ? (byte *)buffer : NULL;
	pmodel = (mdl_t *) ((byte *)&pheader[1] +
			(LittleLong (pinmodel->numframes) - 1) *
			 sizeof (pheader->frames[0]));
	
//	mod->cache.data = pheader;
	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pmodel->boundingradius = LittleFloat (pinmodel->boundingradius);
	pmodel->numskins = LittleLong (pinmodel->numskins);
	pmodel->skinwidth = LittleLong (pinmodel->skinwidth);
	pmodel->skinheight = LittleLong (pinmodel->skinheight);

	if (pmodel->skinheight > MAX_LBM_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	pmodel->numverts = LittleLong (pinmodel->numverts);

	if (pmodel->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pmodel->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pmodel->numtris = LittleLong (pinmodel->numtris);

	if (pmodel->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pmodel->numframes = LittleLong (pinmodel->numframes);
	pmodel->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = LittleLong (pinmodel->synctype);
	mod->numframes = pmodel->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pmodel->scale[i] = LittleFloat (pinmodel->scale[i]);
		pmodel->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pmodel->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

	numskins = pmodel->numskins;
	numframes = pmodel->numframes;

	if (pmodel->skinwidth & 0x03)
		Sys_Error ("Mod_LoadAliasModel: skinwidth not multiple of 4");

	pheader->model = (byte *)pmodel - (byte *)pheader;

//
// load the skins
//
	skinsize = pmodel->skinheight * pmodel->skinwidth;

	if (numskins < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d\n", numskins);

	pskintype = (daliasskintype_t *)&pinmodel[1];

	pskindesc = Hunk_AllocName (numskins * sizeof (maliasskindesc_t),
								loadname);

	pheader->skindesc = (byte *)pskindesc - (byte *)pheader;

	for (i=0 ; i<numskins ; i++)
	{
		aliasskintype_t	skintype;

		skintype = LittleLong (pskintype->type);
		pskindesc[i].type = skintype;

		if (skintype == ALIAS_SKIN_SINGLE)
		{
			pskintype = (daliasskintype_t *)
					Mod_LoadAliasSkin (pskintype + 1,
									   &pskindesc[i].skin,
									   skinsize, pheader);
		}
		else
		{
			pskintype = (daliasskintype_t *)
					Mod_LoadAliasSkinGroup (pskintype + 1,
											&pskindesc[i].skin,
											skinsize, pheader);
		}
	}

//
// set base s and t vertices
//
	pstverts = (stvert_t *)&pmodel[1];
	pinstverts = (stvert_t *)pskintype;

	pheader->stverts = (byte *)pstverts - (byte *)pheader;

	for (i=0 ; i<pmodel->numverts ; i++)
	{
		pstverts[i].onseam = LittleLong (pinstverts[i].onseam);
	// put s and t in 16.16 format
		pstverts[i].s = LittleLong (pinstverts[i].s) << 16;
		pstverts[i].t = LittleLong (pinstverts[i].t) << 16;
	}

//
// set up the triangles
//
	ptri = (mtriangle_t *)&pstverts[pmodel->numverts];
	pintriangles = (dtriangle_t *)&pinstverts[pmodel->numverts];

	if (pheader->flashbase)
	{	// QuakeOG: dtriangle_t and mtriangle_t are the same four ints
		if (((int)pintriangles) & 3)
			Sys_Error ("%s: triangles not 4-byte aligned in flash", mod->name);
		pheader->triangles = (byte *)pintriangles - pheader->flashbase;
	}
	else
	{
	pheader->triangles = (byte *)ptri - (byte *)pheader;
	}

	if (!pheader->flashbase)
	for (i=0 ; i<pmodel->numtris ; i++)
	{
		int		j;

		ptri[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			ptri[i].vertindex[j] =
					LittleLong (pintriangles[i].vertindex[j]);
		}
	}

//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d\n", numframes);

	pframetype = (daliasframetype_t *)&pintriangles[pmodel->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;

		frametype = LittleLong (pframetype->type);
		pheader->frames[i].type = frametype;

		if (frametype == ALIAS_SINGLE)
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasFrame (pframetype + 1,
										&pheader->frames[i].frame,
										pmodel->numverts,
										&pheader->frames[i].bboxmin,
										&pheader->frames[i].bboxmax,
										pheader, pheader->frames[i].name);
		}
		else
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasGroup (pframetype + 1,
										&pheader->frames[i].frame,
										pmodel->numverts,
										&pheader->frames[i].bboxmin,
										&pheader->frames[i].bboxmax,
										pheader, pheader->frames[i].name);
		}
	}

	mod->type = mod_alias;

// FIXME: do this right
	mod->mins[0] = mod->mins[1] = mod->mins[2] = -16;
	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16;

//
// move the complete, relocatable alias model to the cache
//	
	end = Hunk_LowMark ();
#ifdef QOG_BAKE
	mod_aliassize = end - start;	// QuakeOG: what the baker copies
#endif
	total = end - start;
	
	Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
void * Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					i, width, height, size, origin[2];
	unsigned short		*ppixout;
	byte				*ppixin;

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = Hunk_AllocName (sizeof (mspriteframe_t) + size*r_pixbytes,
								   loadname);

	Q_memset (pspriteframe, 0, sizeof (mspriteframe_t) + size);
	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	if (r_pixbytes == 1)
	{
		Q_memcpy (&pspriteframe->pixels[0], (byte *)(pinframe + 1), size);
	}
	else if (r_pixbytes == 2)
	{
		ppixin = (byte *)(pinframe + 1);
		ppixout = (unsigned short *)&pspriteframe->pixels[0];

		for (i=0 ; i<size ; i++)
			ppixout[i] = d_8to16table[ppixin[i]];
	}
	else
	{
		Sys_Error ("Mod_LoadSpriteFrame: driver set invalid r_pixbytes: %d\n",
				 r_pixbytes);
	}

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
void * Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	pspritegroup = Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i]);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
void Mod_LoadSpriteModel (model_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;
	
	pin = (dsprite_t *)buffer;

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) +	(numframes - 1) * sizeof (psprite->frames);

	psprite = Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;
	
//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d\n", numframes);

	mod->numframes = numframes;
	mod->flags = 0;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1,
										 &psprite->frames[i].frameptr);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1,
										 &psprite->frames[i].frameptr);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		i;
	model_t	*mod;

	Con_Printf ("Cached models:\n");
#ifdef QOG_MOD_FLASH
	// QuakeOG: the level's models, baked or loaded
	for (i=0 ; i < mod_numknown ; i++)
	{
		mod = mod_known[i].model;
		Con_Printf ("%8p : %s%s\n", mod ? mod->cache.data : NULL, mod_known[i].name,
			!mod ? " (!P)" : MOD_INBAKE (mod) ? " (baked)" : "");
	}
#else
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		Con_Printf ("%8p : %s",mod->cache.data, mod->name);
		if (mod->needload & NL_UNREFERENCED)
			Con_Printf (" (!R)");
		if (mod->needload & NL_NEEDS_LOADED)
			Con_Printf (" (!P)");
		Con_Printf ("\n");
	}
#endif
}


/*
================
Mod_KnownStats

QuakeOG: model table use, for qog_mem
================
*/
void Mod_KnownStats (int *known, int *referenced, int *peak)
{
	int		i;

	*known = mod_numknown;
#ifdef QOG_MOD_FLASH
	*referenced = mod_numknown;		// the table holds the current level only
	*peak = mod_peakknown;
#else
	*referenced = 0;
	for (i=0 ; i<mod_numknown ; i++)
		if (mod_known[i].needload != NL_UNREFERENCED)
			(*referenced)++;
	*peak = mod_numknown;
#endif
}

