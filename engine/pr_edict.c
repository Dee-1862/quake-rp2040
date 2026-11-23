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
// sv_edict.c -- entity dictionary

#include "quakedef.h"

dprograms_t		*progs;
dfunction_t		*pr_functions;
char			*pr_strings;
ddef_t			*pr_fielddefs;
ddef_t			*pr_globaldefs;
dstatement_t	*pr_statements;
globalvars_t	*pr_global_struct;
float			*pr_globals;			// same as pr_global_struct
int				pr_edict_size;	// in bytes

unsigned short		pr_crc;

int		type_size[8] = {1,sizeof(string_t)/4,1,3,1,1,sizeof(func_t)/4,sizeof(void *)/4};

ddef_t *ED_FieldAtOfs (int ofs);
qboolean	ED_ParseEpair (void *base, ddef_t *key, char *s);

cvar_t	nomonsters = {"nomonsters", "0"};
cvar_t	gamecfg = {"gamecfg", "0"};
cvar_t	scratch1 = {"scratch1", "0"};
cvar_t	scratch2 = {"scratch2", "0"};
cvar_t	scratch3 = {"scratch3", "0"};
cvar_t	scratch4 = {"scratch4", "0"};
cvar_t	savedgamecfg = {"savedgamecfg", "0", true};
cvar_t	saved1 = {"saved1", "0", true};
cvar_t	saved2 = {"saved2", "0", true};
cvar_t	saved3 = {"saved3", "0", true};
cvar_t	saved4 = {"saved4", "0", true};

#define	MAX_FIELD_LEN	64
#define GEFV_CACHESIZE	2

typedef struct {
	ddef_t	*pcache;
	char	field[MAX_FIELD_LEN];
} gefv_cache;

static gefv_cache	gefvCache[GEFV_CACHESIZE] = {{NULL, ""}, {NULL, ""}};

/*
=================
ED_ClearEdict

Sets everything to NULL
=================
*/
void ED_ClearEdict (edict_t *e)
{
	ED_CLEARFIELDS(e);	// QuakeOG: was memset (&e->v, 0, progs->entityfields * 4)
	e->free = false;
}

#ifdef QOG_EDICT_SPARSE
/*
==============================================================================

QuakeOG: sparse edict fields (progs.h). Core fields sit in edict_t.v. Every
other field an edict has written since it was cleared is a pair in its list
in ed_cold: a header word (owner edict number, capacity << 16), the values,
then the field numbers as bytes. A list that grows moves to the top of the
arena. When the top reaches the end, live lists slide down over dead ones.
Nothing outside this file keeps a pointer into the arena across a write.
Freed edicts keep their lists until they are reused, so QuakeC that still
reads a removed entity's fields sees what id's code would.

==============================================================================
*/
signed char	*pr_fieldmap;		// QuakeC field number -> core word, or -1
static int	*ed_cold;			// the arena
static int	ed_coldsize;		// words
static int	ed_coldtop;			// first free word, word 0 is never used
int			ed_coldpeak, ed_coldcompactions;	// for qog_mem
int			ed_coldlive, ed_coldlivepeak;		// words in live lists

#define	COLD_DEAD		0xffff
#define	COLD_WORDS(cap)	(1 + (cap) + ((cap) + 3) / 4)
#ifndef QOG_EDICT_COLD_STEP
#define	QOG_EDICT_COLD_STEP	4	// pairs added each time a list grows
#endif

// QuakeC field numbers of the core fields (id's layout) and their place in entcore_t
#define	CORE(f,n)	{ EV_OFS(f), (int)((size_t)&((entcore_t *)0)->f / 4), n }
static const struct { short qc, core, n; } ed_coremap[] = {
	CORE(modelindex,1), CORE(absmin,3), CORE(absmax,3), CORE(movetype,1),
	CORE(solid,1), CORE(origin,3), CORE(velocity,3), CORE(angles,3),
	CORE(classname,1), CORE(model,1), CORE(frame,1),
	CORE(mins,3), CORE(maxs,3), CORE(size,3), CORE(touch,1), CORE(think,1),
	CORE(nextthink,1), CORE(groundentity,1), CORE(flags,1),
	CORE(owner,1), CORE(waterlevel,1), CORE(watertype,1)
};

static void ED_BuildFieldMap (void)
{
	int		i, j, words = 0;

	if (progs->entityfields > 256)
		Sys_Error ("QOG_EDICT_SPARSE: %i entity fields, at most 256", progs->entityfields);
	pr_fieldmap = Hunk_AllocName (progs->entityfields, "fieldmap");
	memset (pr_fieldmap, -1, progs->entityfields);
	for (i=0 ; i<sizeof(ed_coremap)/sizeof(ed_coremap[0]) ; i++)
		for (j=0 ; j<ed_coremap[i].n ; j++)
		{
			pr_fieldmap[ed_coremap[i].qc + j] = ed_coremap[i].core + j;
			words++;
		}
	if (words * 4 != sizeof(entcore_t))
		Sys_Error ("QOG_EDICT_SPARSE: core map covers %i of %i bytes", words * 4, (int)sizeof(entcore_t));
}

/*
Called once the edicts are allocated: the arena comes from the hunk too.
*/
void ED_ColdInit (void)
{
#ifndef QOG_EDICT_COLD_WORDS
#define	QOG_EDICT_COLD_WORDS	12	// words of arena per edict slot, measured peak 9.2
#endif
	ed_coldsize = sv.max_edicts * QOG_EDICT_COLD_WORDS;
	if (ed_coldsize > 65535)
		ed_coldsize = 65535;	// edict_t.coldofs is 16 bits
	ed_cold = Hunk_AllocName (ed_coldsize * 4, "edcold");
	ed_coldtop = 1;
	ed_coldpeak = 1;
	ed_coldcompactions = 0;
	ed_coldlive = ed_coldlivepeak = 0;
}

static void ED_ColdCompact (void)
{
	int		r, dst, hdr, words;

	ed_coldcompactions++;
	for (r = dst = 1 ; r < ed_coldtop ; r += words)
	{
		hdr = ed_cold[r];
		words = COLD_WORDS((unsigned)hdr >> 16);
		if ((hdr & 0xffff) == COLD_DEAD)
			continue;
		if (dst != r)
			memmove (ed_cold + dst, ed_cold + r, words * 4);
		EDICT_NUM(hdr & 0xffff)->coldofs = dst;
		dst += words;
	}
	ed_coldtop = dst;
}

static int ED_ColdAlloc (int words)
{
	int		r;

	if (ed_coldtop + words > ed_coldsize)
	{
		ED_ColdCompact ();
		if (ed_coldtop + words > ed_coldsize)
			Sys_Error ("ED_ColdAlloc: edict field arena full (%i words)", ed_coldsize);
	}
	r = ed_coldtop;
	ed_coldtop += words;
	return r;
}

static void ED_ColdFree (edict_t *e)
{
	if (!e->coldofs)
		return;
	ed_cold[e->coldofs] |= COLD_DEAD;
	ed_coldlive -= COLD_WORDS(e->coldcap);
	if (e->coldofs + COLD_WORDS(e->coldcap) == ed_coldtop)
		ed_coldtop = e->coldofs;	// the last list: give the space straight back
	e->coldofs = 0;
	e->coldn = e->coldcap = 0;
}

static int *ED_ColdFind (edict_t *e, int ofs)
{
	int				*vals, i, n = e->coldn;
	unsigned char	*keys;

	if (!n)
		return NULL;
	vals = ed_cold + e->coldofs + 1;
	keys = (unsigned char *)(vals + e->coldcap);
	for (i=0 ; i<n ; i++)
		if (keys[i] == ofs)
			return vals + i;
	return NULL;
}

// Adds a pair for a field that has none. May move every list in the arena.
static int *ED_ColdAdd (edict_t *e, int ofs)
{
	int				*vals, n = e->coldn;
	unsigned char	*keys;

	if (n == e->coldcap)
	{
		int		cap = e->coldcap + QOG_EDICT_COLD_STEP;
		int		r, *oldv;

		if (cap > 255)
			cap = 255;
		r = ED_ColdAlloc (COLD_WORDS(cap));	// may compact, which moves e's list
		// not NUM_FOR_EDICT: a savegame load fills edicts past sv.num_edicts
		ed_cold[r] = (int)(((byte *)e - (byte *)sv.edicts) / pr_edict_size) | (cap << 16);
		if (e->coldofs)
		{
			oldv = ed_cold + e->coldofs + 1;
			memcpy (ed_cold + r + 1, oldv, n * 4);
			memcpy ((unsigned char *)(ed_cold + r + 1 + cap), oldv + e->coldcap, n);
			ed_cold[e->coldofs] |= COLD_DEAD;
			ed_coldlive -= COLD_WORDS(e->coldcap);
		}
		e->coldofs = r;
		e->coldcap = cap;
		ed_coldlive += COLD_WORDS(cap);
		if (ed_coldlive > ed_coldlivepeak)
			ed_coldlivepeak = ed_coldlive;
		if (ed_coldtop > ed_coldpeak)
			ed_coldpeak = ed_coldtop;
	}
	vals = ed_cold + e->coldofs + 1;
	keys = (unsigned char *)(vals + e->coldcap);
	keys[n] = ofs;
	vals[n] = 0;
	e->coldn = n + 1;
	return vals + n;
}

int ED_ColdGet (edict_t *e, int ofs)
{
	int		*p = ED_ColdFind (e, ofs);
	return p ? *p : 0;
}

void ED_ColdSet (edict_t *e, int ofs, int val)
{
	int		*p = ED_ColdFind (e, ofs);

	if (p)
		*p = val;
	else if (val)	// a missing field already reads as zero
		*ED_ColdAdd (e, ofs) = val;
}

float ED_ColdFloat (edict_t *e, int ofs)
{
	eval_t	v;
	v._int = ED_ColdGet (e, ofs);
	return v._float;
}

void ED_ColdSetFloat (edict_t *e, int ofs, float val)
{
	eval_t	v;
	v._float = val;
	ED_ColdSet (e, ofs, v._int);
}

float *ED_ColdVector (edict_t *e, int ofs)
{
	static eval_t	ring[8];
	static int		next;
	eval_t			*v = &ring[next++ & 7];
	int				i;

	for (i=0 ; i<3 ; i++)
		((int *)v->vector)[i] = ED_ColdGet (e, ofs + i);
	return v->vector;
}

void ED_ColdSetVector (edict_t *e, int ofs, const float *val)
{
	int		i;
	for (i=0 ; i<3 ; i++)
		ED_ColdSet (e, ofs + i, ((const int *)val)[i]);
}

int ED_FieldGet (edict_t *e, int ofs)
{
	int		m = pr_fieldmap[ofs];
	if (m >= 0)
		return ((int *)&e->v)[m];
	return ED_ColdGet (e, ofs);
}

void ED_FieldSet (edict_t *e, int ofs, int val)
{
	int		m = pr_fieldmap[ofs];
	if (m >= 0)
		((int *)&e->v)[m] = val;
	else
		ED_ColdSet (e, ofs, val);
}

// A pointer to a field that stays valid until the next field write
static eval_t *ED_FieldPtr (edict_t *e, int ofs)
{
	int		m = pr_fieldmap[ofs], *p;
	if (m >= 0)
		return (eval_t *)((int *)&e->v + m);
	p = ED_ColdFind (e, ofs);
	if (!p)
		p = ED_ColdAdd (e, ofs);
	return (eval_t *)p;
}

void ED_ClearFields (edict_t *e)
{
	memset (&e->v, 0, sizeof(e->v));
	ED_ColdFree (e);
}

// Fills v with up to three words of a field, for printing and saving
static void ED_FieldCopy (edict_t *e, int ofs, int n, int *v)
{
	int		i;
	for (i=0 ; i<n ; i++)
		v[i] = ED_FieldGet (e, ofs + i);
}

static qboolean ED_ParseEpairEdict (edict_t *ent, ddef_t *key, char *s);
#endif

/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *ED_Alloc (void)
{
	int			i;
	edict_t		*e;

	for ( i=svs.maxclients+1 ; i<sv.num_edicts ; i++)
	{
		e = EDICT_NUM(i);
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (e->free && ( e->freetime < 2 || sv.time - e->freetime > 0.5 ) )
		{
			ED_ClearEdict (e);
			return e;
		}
	}
	
	if (i == sv.max_edicts)	// QuakeOG: was MAX_EDICTS, wrong once the array is sized per map
		Sys_Error ("ED_Alloc: no free edicts");
		
	sv.num_edicts++;
	e = EDICT_NUM(i);
	ED_ClearEdict (e);

	return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free (edict_t *ed)
{
	SV_UnlinkEdict (ed);		// unlink from world bsp

	ed->free = true;
	ed->v.model = 0;
	EV_SETFLOAT(ed, takedamage, 0);	// QuakeOG: was ed->v.takedamage = 0
	ed->v.modelindex = 0;
	EV_SETFLOAT(ed, colormap, 0);
	EV_SETFLOAT(ed, skin, 0);
	ed->v.frame = 0;
	VectorCopy (vec3_origin, ed->v.origin);
	VectorCopy (vec3_origin, ed->v.angles);
	ed->v.nextthink = -1;
	ed->v.solid = 0;
	
	ed->freetime = sv.time;
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
ddef_t *ED_GlobalAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;
	
	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FieldAtOfs
============
*/
ddef_t *ED_FieldAtOfs (int ofs)
{
	ddef_t		*def;
	int			i;
	
	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		def = &pr_fielddefs[i];
		if (def->ofs == ofs)
			return def;
	}
	return NULL;
}

/*
============
ED_FindField
============
*/
ddef_t *ED_FindField (char *name)
{
	ddef_t		*def;
	int			i;
	
	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		def = &pr_fielddefs[i];
		if (!strcmp(pr_strings + def->s_name,name) )
			return def;
	}
	return NULL;
}


/*
============
ED_FindGlobal
============
*/
ddef_t *ED_FindGlobal (char *name)
{
	ddef_t		*def;
	int			i;
	
	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		if (!strcmp(pr_strings + def->s_name,name) )
			return def;
	}
	return NULL;
}


/*
============
ED_FindFunction
============
*/
dfunction_t *ED_FindFunction (char *name)
{
	dfunction_t		*func;
	int				i;
	
	for (i=0 ; i<progs->numfunctions ; i++)
	{
		func = &pr_functions[i];
		if (!strcmp(pr_strings + func->s_name,name) )
			return func;
	}
	return NULL;
}


eval_t *GetEdictFieldValue(edict_t *ed, char *field)
{
	ddef_t			*def = NULL;
	int				i;
	static int		rep = 0;

	for (i=0 ; i<GEFV_CACHESIZE ; i++)
	{
		if (!strcmp(field, gefvCache[i].field))
		{
			def = gefvCache[i].pcache;
			goto Done;
		}
	}

	def = ED_FindField (field);

	if (strlen(field) < MAX_FIELD_LEN)
	{
		gefvCache[rep].pcache = def;
		strcpy (gefvCache[rep].field, field);
		rep ^= 1;
	}

Done:
	if (!def)
		return NULL;

#ifdef QOG_EDICT_SPARSE
	return ED_FieldPtr (ed, def->ofs);	// QuakeOG: valid until the next field write
#else
	return (eval_t *)((char *)&ed->v + def->ofs*4);
#endif
}


/*
============
PR_ValueString

Returns a string describing *data in a type specific manner
=============
*/
char *PR_ValueString (etype_t type, eval_t *val)
{
	static char	line[256];
	ddef_t		*def;
	dfunction_t	*f;
	
	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		sprintf (line, "%s", pr_strings + val->string);
		break;
	case ev_entity:	
		sprintf (line, "entity %i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)) );
		break;
	case ev_function:
		f = pr_functions + val->function;
		sprintf (line, "%s()", pr_strings + f->s_name);
		break;
	case ev_field:
		def = ED_FieldAtOfs ( val->_int );
		sprintf (line, ".%s", pr_strings + def->s_name);
		break;
	case ev_void:
		sprintf (line, "void");
		break;
	case ev_float:
		sprintf (line, "%5.1f", val->_float);
		break;
	case ev_vector:
		sprintf (line, "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1], val->vector[2]);
		break;
	case ev_pointer:
		sprintf (line, "pointer");
		break;
	default:
		sprintf (line, "bad type %i", type);
		break;
	}
	
	return line;
}

/*
============
PR_UglyValueString

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
char *PR_UglyValueString (etype_t type, eval_t *val)
{
	static char	line[256];
	ddef_t		*def;
	dfunction_t	*f;
	
	type &= ~DEF_SAVEGLOBAL;

	switch (type)
	{
	case ev_string:
		sprintf (line, "%s", pr_strings + val->string);
		break;
	case ev_entity:	
		sprintf (line, "%i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
		break;
	case ev_function:
		f = pr_functions + val->function;
		sprintf (line, "%s", pr_strings + f->s_name);
		break;
	case ev_field:
		def = ED_FieldAtOfs ( val->_int );
		sprintf (line, "%s", pr_strings + def->s_name);
		break;
	case ev_void:
		sprintf (line, "void");
		break;
	case ev_float:
		sprintf (line, "%f", val->_float);
		break;
	case ev_vector:
		sprintf (line, "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);
		break;
	default:
		sprintf (line, "bad type %i", type);
		break;
	}
	
	return line;
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
char *PR_GlobalString (int ofs)
{
	char	*s;
	int		i;
	ddef_t	*def;
	void	*val;
	static char	line[128];
	
	val = (void *)PR_GlobalPtr (ofs);	// QuakeOG: was &pr_globals[ofs]
	def = ED_GlobalAtOfs(ofs);
	if (!def)
		sprintf (line,"%i(???)", ofs);
	else
	{
		s = PR_ValueString (def->type, val);
		sprintf (line,"%i(%s)%s", ofs, pr_strings + def->s_name, s);
	}
	
	i = strlen(line);
	for ( ; i<20 ; i++)
		strcat (line," ");
	strcat (line," ");
		
	return line;
}

char *PR_GlobalStringNoContents (int ofs)
{
	int		i;
	ddef_t	*def;
	static char	line[128];
	
	def = ED_GlobalAtOfs(ofs);
	if (!def)
		sprintf (line,"%i(???)", ofs);
	else
		sprintf (line,"%i(%s)", ofs, pr_strings + def->s_name);
	
	i = strlen(line);
	for ( ; i<20 ; i++)
		strcat (line," ");
	strcat (line," ");
		
	return line;
}


/*
=============
ED_Print

For debugging
=============
*/
void ED_Print (edict_t *ed)
{
	int		l;
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	if (ed->free)
	{
		Con_Printf ("FREE\n");
		return;
	}

	Con_Printf("\nEDICT %i:\n", NUM_FOR_EDICT(ed));
	for (i=1 ; i<progs->numfielddefs ; i++)
	{
#ifdef QOG_EDICT_SPARSE
		int		copy[3];
#endif
		d = &pr_fielddefs[i];
		name = pr_strings + d->s_name;
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars

	// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;
#ifdef QOG_EDICT_SPARSE
		ED_FieldCopy (ed, d->ofs, type_size[type], copy);	// QuakeOG
		v = copy;
#else
		v = (int *)((char *)&ed->v + d->ofs*4);
#endif

		for (j=0 ; j<type_size[type] ; j++)
			if (v[j])
				break;
		if (j == type_size[type])
			continue;
	
		Con_Printf ("%s",name);
		l = strlen (name);
		while (l++ < 15)
			Con_Printf (" ");

		Con_Printf ("%s\n", PR_ValueString(d->type, (eval_t *)v));		
	}
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write (FILE *f, edict_t *ed)
{
	ddef_t	*d;
	int		*v;
	int		i, j;
	char	*name;
	int		type;

	fprintf (f, "{\n");

	if (ed->free)
	{
		fprintf (f, "}\n");
		return;
	}
	
	for (i=1 ; i<progs->numfielddefs ; i++)
	{
#ifdef QOG_EDICT_SPARSE
		int		copy[3];
#endif
		d = &pr_fielddefs[i];
		name = pr_strings + d->s_name;
		if (name[strlen(name)-2] == '_')
			continue;	// skip _x, _y, _z vars

	// if the value is still all 0, skip the field
		type = d->type & ~DEF_SAVEGLOBAL;
#ifdef QOG_EDICT_SPARSE
		ED_FieldCopy (ed, d->ofs, type_size[type], copy);	// QuakeOG
		v = copy;
#else
		v = (int *)((char *)&ed->v + d->ofs*4);
#endif
		for (j=0 ; j<type_size[type] ; j++)
			if (v[j])
				break;
		if (j == type_size[type])
			continue;
	
		fprintf (f,"\"%s\" ",name);
		fprintf (f,"\"%s\"\n", PR_UglyValueString(d->type, (eval_t *)v));		
	}

	fprintf (f, "}\n");
}

void ED_PrintNum (int ent)
{
	ED_Print (EDICT_NUM(ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts (void)
{
	int		i;
	
	Con_Printf ("%i entities\n", sv.num_edicts);
	for (i=0 ; i<sv.num_edicts ; i++)
		ED_PrintNum (i);
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edicy
=============
*/
void ED_PrintEdict_f (void)
{
	int		i;
	
	i = Q_atoi (Cmd_Argv(1));
	if (i >= sv.num_edicts)
	{
		Con_Printf("Bad edict number\n");
		return;
	}
	ED_PrintNum (i);
}

/*
=============
ED_Count

For debugging
=============
*/
void ED_Count (void)
{
	int		i;
	edict_t	*ent;
	int		active, models, solid, step;

	active = models = solid = step = 0;
	for (i=0 ; i<sv.num_edicts ; i++)
	{
		ent = EDICT_NUM(i);
		if (ent->free)
			continue;
		active++;
		if (ent->v.solid)
			solid++;
		if (ent->v.model)
			models++;
		if (ent->v.movetype == MOVETYPE_STEP)
			step++;
	}

	Con_Printf ("num_edicts:%3i\n", sv.num_edicts);
	Con_Printf ("active    :%3i\n", active);
	Con_Printf ("view      :%3i\n", models);
	Con_Printf ("touch     :%3i\n", solid);
	Con_Printf ("step      :%3i\n", step);

}

/*
==============================================================================

					ARCHIVING GLOBALS

FIXME: need to tag constants, doesn't really work
==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals (FILE *f)
{
	ddef_t		*def;
	int			i;
	char		*name;
	int			type;

	fprintf (f,"{\n");
	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		def = &pr_globaldefs[i];
		type = def->type;
		if ( !(def->type & DEF_SAVEGLOBAL) )
			continue;
		type &= ~DEF_SAVEGLOBAL;

		if (type != ev_string
		&& type != ev_float
		&& type != ev_entity)
			continue;

		name = pr_strings + def->s_name;		
		fprintf (f,"\"%s\" ", name);
		fprintf (f,"\"%s\"\n", PR_UglyValueString(type, (eval_t *)PR_GlobalPtr (def->ofs)));	// QuakeOG: was &pr_globals[def->ofs]
	}
	fprintf (f,"}\n");
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals (char *data)
{
	char	keyname[64];
	ddef_t	*key;

	while (1)
	{	
	// parse key
		data = COM_Parse (data);
		if (com_token[0] == '}')
			break;
		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");

		strcpy (keyname, com_token);

	// parse value	
		data = COM_Parse (data);
		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Sys_Error ("ED_ParseEntity: closing brace without data");

		key = ED_FindGlobal (keyname);
		if (!key)
		{
			Con_Printf ("'%s' is not a global\n", keyname);
			continue;
		}

#ifdef QOG_AOT_GLOBALS
		if (pr_aot_globals)
		{	// QuakeOG: parse a copy, then store it where the global lives
			int		val[3], n, i, type = key->type & ~DEF_SAVEGLOBAL;
			ddef_t	at0 = *key;

			n = type == ev_vector ? 3 : 1;
			for (i=0 ; i<n ; i++)
				if (pr_aot_globalmap[key->ofs + i] < 0)
					break;
			if (i < n)
			{
				Con_Printf ("'%s' is a constant here\n", keyname);
				continue;
			}
			for (i=0 ; i<n ; i++)
				val[i] = *PR_GlobalPtr (key->ofs + i);
			at0.ofs = 0;
			if (!ED_ParseEpair ((void *)val, &at0, com_token))
				Host_Error ("ED_ParseGlobals: parse error");
			for (i=0 ; i<n ; i++)
				*PR_GlobalPtr (key->ofs + i) = val[i];
			continue;
		}
#endif
		if (!ED_ParseEpair ((void *)pr_globals, key, com_token))
			Host_Error ("ED_ParseGlobals: parse error");
	}
}

//============================================================================


/*
=============
ED_NewString
=============
*/
char *ED_NewString (char *string)
{
	char	*new, *new_p;
	int		i,l;
	
	l = strlen(string) + 1;
	new = Hunk_Alloc (l);
	new_p = new;

	for (i=0 ; i< l ; i++)
	{
		if (string[i] == '\\' && i < l-1)
		{
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}
	
	return new;
}


/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
qboolean	ED_ParseEpair (void *base, ddef_t *key, char *s)
{
	int		i;
	char	string[128];
	ddef_t	*def;
	char	*v, *w;
	void	*d;
	dfunction_t	*func;
	
	d = (void *)((int *)base + key->ofs);
	
	switch (key->type & ~DEF_SAVEGLOBAL)
	{
	case ev_string:
		*(string_t *)d = ED_NewString (s) - pr_strings;
		break;
		
	case ev_float:
		*(float *)d = atof (s);
		break;
		
	case ev_vector:
		strcpy (string, s);
		v = string;
		w = string;
		for (i=0 ; i<3 ; i++)
		{
			while (*v && *v != ' ')
				v++;
			*v = 0;
			((float *)d)[i] = atof (w);
			w = v = v+1;
		}
		break;
		
	case ev_entity:
		*(int *)d = EDICT_TO_PROG(EDICT_NUM(atoi (s)));
		break;
		
	case ev_field:
		def = ED_FindField (s);
		if (!def)
		{
			Con_Printf ("Can't find field %s\n", s);
			return false;
		}
		*(int *)d = *PR_GlobalPtr (def->ofs);	// QuakeOG: was G_INT(def->ofs)
		break;
	
	case ev_function:
		func = ED_FindFunction (s);
		if (!func)
		{
			Con_Printf ("Can't find function %s\n", s);
			return false;
		}
		*(func_t *)d = func - pr_functions;
		break;
		
	default:
		break;
	}
	return true;
}

#ifdef QOG_EDICT_SPARSE
/*
QuakeOG: ED_ParseEpair for an edict field: parse into a scratch copy, then
store the words the parse wrote.
*/
static qboolean ED_ParseEpairEdict (edict_t *ent, ddef_t *key, char *s)
{
	int		val[3], n, i, type = key->type & ~DEF_SAVEGLOBAL;
	ddef_t	at0;

	if (type != ev_string && type != ev_float && type != ev_vector
		&& type != ev_entity && type != ev_field && type != ev_function)
		return true;	// id's parse writes nothing for these
	at0 = *key;
	at0.ofs = 0;
	if (!ED_ParseEpair ((void *)val, &at0, s))
		return false;
	n = type == ev_vector ? 3 : 1;
	for (i=0 ; i<n ; i++)
		ED_FieldSet (ent, key->ofs + i, val[i]);
	return true;
}
#endif

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
char *ED_ParseEdict (char *data, edict_t *ent)
{
	ddef_t		*key;
	qboolean	anglehack;
	qboolean	init;
	char		keyname[256];
	int			n;

	init = false;

// clear it
	if (ent != sv.edicts)	// hack
		ED_CLEARFIELDS(ent);	// QuakeOG: was memset (&ent->v, 0, progs->entityfields * 4)

// go through all the dictionary pairs
	while (1)
	{	
	// parse key
		data = COM_Parse (data);
		if (com_token[0] == '}')
			break;
		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");
		
// anglehack is to allow QuakeEd to write single scalar angles
// and allow them to be turned into vectors. (FIXME...)
if (!strcmp(com_token, "angle"))
{
	strcpy (com_token, "angles");
	anglehack = true;
}
else
	anglehack = false;

// FIXME: change light to _light to get rid of this hack
if (!strcmp(com_token, "light"))
	strcpy (com_token, "light_lev");	// hack for single light def

		strcpy (keyname, com_token);

		// another hack to fix heynames with trailing spaces
		n = strlen(keyname);
		while (n && keyname[n-1] == ' ')
		{
			keyname[n-1] = 0;
			n--;
		}

	// parse value	
		data = COM_Parse (data);
		if (!data)
			Sys_Error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			Sys_Error ("ED_ParseEntity: closing brace without data");

		init = true;	

// keynames with a leading underscore are used for utility comments,
// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;
		
		key = ED_FindField (keyname);
		if (!key)
		{
			Con_Printf ("'%s' is not a field\n", keyname);
			continue;
		}

if (anglehack)
{
char	temp[32];
strcpy (temp, com_token);
sprintf (com_token, "0 %s 0", temp);
}

#ifdef QOG_EDICT_SPARSE
		if (!ED_ParseEpairEdict (ent, key, com_token))	// QuakeOG
#else
		if (!ED_ParseEpair ((void *)&ent->v, key, com_token))
#endif
			Host_Error ("ED_ParseEdict: parse error");
	}

	if (!init)
		ent->free = true;

	return data;
}


/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile (char *data)
{	
	edict_t		*ent;
	int			inhibit;
	dfunction_t	*func;
	
	ent = NULL;
	inhibit = 0;
	pr_global_struct->time = sv.time;
	
// parse ents
	while (1)
	{
// parse the opening brace	
		data = COM_Parse (data);
		if (!data)
			break;
		if (com_token[0] != '{')
			Sys_Error ("ED_LoadFromFile: found %s when expecting {",com_token);

		if (!ent)
			ent = EDICT_NUM(0);
		else
			ent = ED_Alloc ();
		data = ED_ParseEdict (data, ent);

// remove things from different skill levels or deathmatch
		if (deathmatch.value)
		{
			if (((int)EV_FLOAT(ent, spawnflags) & SPAWNFLAG_NOT_DEATHMATCH))	// QuakeOG: EV_
			{
				ED_Free (ent);	
				inhibit++;
				continue;
			}
		}
		else if ((current_skill == 0 && ((int)EV_FLOAT(ent, spawnflags) & SPAWNFLAG_NOT_EASY))	// QuakeOG: EV_
				|| (current_skill == 1 && ((int)EV_FLOAT(ent, spawnflags) & SPAWNFLAG_NOT_MEDIUM))
				|| (current_skill >= 2 && ((int)EV_FLOAT(ent, spawnflags) & SPAWNFLAG_NOT_HARD)) )
		{
			ED_Free (ent);	
			inhibit++;
			continue;
		}

//
// immediately call spawn function
//
		if (!ent->v.classname)
		{
			Con_Printf ("No classname for:\n");
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

	// look for the spawn function
		func = ED_FindFunction ( pr_strings + ent->v.classname );

		if (!func)
		{
			Con_Printf ("No spawn function for:\n");
			ED_Print (ent);
			ED_Free (ent);
			continue;
		}

		pr_global_struct->self = EDICT_TO_PROG(ent);
		PR_ExecuteProgram (func - pr_functions);
	}	

	Con_DPrintf ("%i entities inhibited\n", inhibit);
}


/*
===============
PR_LoadProgs
===============
*/
void PR_LoadProgs (void)
{
	int		i;

// flush the non-C variable lookup cache
	for (i=0 ; i<GEFV_CACHESIZE ; i++)
		gefvCache[i].field[0] = 0;

	CRC_Init (&pr_crc);

	// QuakeOG: progs.dat is used in place from flash and only its globals
	// are copied to RAM, whether QuakeC runs compiled or interpreted.
	progs = NULL;
	pr_inflash = false;
	{
		int		flen;
		byte	*f = COM_FlashFile ("progs.dat", &flen);
		// The interpreter can run from flash too: it no longer counts
		// profile hits there (pr_exec.c), the only thing it wrote.
		if (f && !(((int)f) & 3))
		{
			progs = (dprograms_t *)f;
			com_filesize = flen;
			pr_inflash = true;
		}
	}
	if (!progs)
		progs = (dprograms_t *)COM_LoadHunkFile ("progs.dat");
	if (!progs)
		Sys_Error ("PR_LoadProgs: couldn't load progs.dat");
	Con_DPrintf ("Programs occupy %iK%s.\n", com_filesize/1024, pr_inflash ? " of flash" : "");

	for (i=0 ; i<com_filesize ; i++)
		CRC_ProcessByte (&pr_crc, ((byte *)progs)[i]);

// byte swap the header
	if (!pr_inflash)	// no-op on little-endian, and flash is read only
	for (i=0 ; i<sizeof(*progs)/4 ; i++)
		((int *)progs)[i] = LittleLong ( ((int *)progs)[i] );

	if (progs->version != PROG_VERSION)
		Sys_Error ("progs.dat has wrong version number (%i should be %i)", progs->version, PROG_VERSION);
	if (progs->crc != PROGHEADER_CRC)
		Sys_Error ("progs.dat system vars have been modified, progdefs.h is out of date");

	pr_functions = (dfunction_t *)((byte *)progs + progs->ofs_functions);
	pr_strings = (char *)progs + progs->ofs_strings;
	pr_globaldefs = (ddef_t *)((byte *)progs + progs->ofs_globaldefs);
	pr_fielddefs = (ddef_t *)((byte *)progs + progs->ofs_fielddefs);
	pr_statements = (dstatement_t *)((byte *)progs + progs->ofs_statements);

	pr_global_struct = (globalvars_t *)((byte *)progs + progs->ofs_globals);
#ifdef QOG_EDICT_SPARSE
	pr_edict_size = sizeof (edict_t);	// QuakeOG: core fields only
	ED_BuildFieldMap ();
#else
	pr_edict_size = progs->entityfields * 4 + sizeof (edict_t) - sizeof(entvars_t);
#endif
	pr_aot_globals = false;
	if (pr_inflash)
	{	// the globals change at run time: the only part that needs RAM
#ifdef QOG_AOT_GLOBALS
		// QuakeOG: compiled QuakeC keeps constants in its code and most
		// temporaries in C locals, so it needs only the globals listed in
		// pr_aot_globalmap, packed. Decide which code runs first.
		PR_AOT_Check ();
		if (pr_aot_active && pr_aot_numram)
		{
			int		*g = Hunk_AllocName (pr_aot_numram * 4, "progglobals");
			int		*src = (int *)pr_global_struct;

			for (i=0 ; i<progs->numglobals ; i++)
				if (pr_aot_globalmap[i] >= 0)
					g[pr_aot_globalmap[i]] = src[i];
			pr_global_struct = (globalvars_t *)g;
			pr_aot_globals = true;
		}
		else
#endif
		{
		globalvars_t *g = Hunk_AllocName (progs->numglobals * 4, "progglobals");
		memcpy (g, pr_global_struct, progs->numglobals * 4);
		pr_global_struct = g;
		}
	}
	pr_globals = (float *)pr_global_struct;

	if (pr_inflash)
	{
		PR_SizeLocalStack ();	// QuakeOG: locals stack sized for these progs
#ifndef QOG_AOT_GLOBALS
		PR_AOT_Check ();
#endif
		return;	// the swaps below are no-ops on little-endian, and flash is read only
	}

// byte swap the lumps
	for (i=0 ; i<progs->numstatements ; i++)
	{
		pr_statements[i].op = LittleShort(pr_statements[i].op);
		pr_statements[i].a = LittleShort(pr_statements[i].a);
		pr_statements[i].b = LittleShort(pr_statements[i].b);
		pr_statements[i].c = LittleShort(pr_statements[i].c);
	}

	for (i=0 ; i<progs->numfunctions; i++)
	{
	pr_functions[i].first_statement = LittleLong (pr_functions[i].first_statement);
	pr_functions[i].parm_start = LittleLong (pr_functions[i].parm_start);
	pr_functions[i].s_name = LittleLong (pr_functions[i].s_name);
	pr_functions[i].s_file = LittleLong (pr_functions[i].s_file);
	pr_functions[i].numparms = LittleLong (pr_functions[i].numparms);
	pr_functions[i].locals = LittleLong (pr_functions[i].locals);
	}	

	for (i=0 ; i<progs->numglobaldefs ; i++)
	{
		pr_globaldefs[i].type = LittleShort (pr_globaldefs[i].type);
		pr_globaldefs[i].ofs = LittleShort (pr_globaldefs[i].ofs);
		pr_globaldefs[i].s_name = LittleLong (pr_globaldefs[i].s_name);
	}

	for (i=0 ; i<progs->numfielddefs ; i++)
	{
		pr_fielddefs[i].type = LittleShort (pr_fielddefs[i].type);
		if (pr_fielddefs[i].type & DEF_SAVEGLOBAL)
			Sys_Error ("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
		pr_fielddefs[i].ofs = LittleShort (pr_fielddefs[i].ofs);
		pr_fielddefs[i].s_name = LittleLong (pr_fielddefs[i].s_name);
	}

	for (i=0 ; i<progs->numglobals ; i++)
		((int *)pr_globals)[i] = LittleLong (((int *)pr_globals)[i]);

	PR_SizeLocalStack ();	// QuakeOG: locals stack sized for these progs
	PR_AOT_Check ();
}

/*
===============
PR_AOT_Check

QuakeOG: switch to the compiled QuakeC only if it was generated from this
exact progs.dat (same counts, same statement fingerprint). -noaot forces
the interpreter, for comparison.
===============
*/
qboolean	pr_inflash;
qboolean	pr_aot_globals;	// QuakeOG: pr_globals holds only the compiled code's RAM globals

/*
QuakeOG: a global by its number in progs.dat. With QOG_AOT_GLOBALS active a
constant is read from progs.dat in flash, so only RAM globals may be written
through the pointer.
*/
int *PR_GlobalPtr (int ofs)
{
#ifdef QOG_AOT_GLOBALS
	if (pr_aot_globals)
	{
		int		m = pr_aot_globalmap[ofs];
		if (m >= 0)
			return (int *)pr_globals + m;
		return (int *)((byte *)progs + progs->ofs_globals) + ofs;
	}
#endif
	return (int *)pr_globals + ofs;
}

qboolean PR_AOT_Matches (dprograms_t *p)
{
	unsigned	h;
	byte		*s;
	int			i, n;

	if (!pr_aot_numfunctions
		|| p->numfunctions != pr_aot_numfunctions
		|| p->numstatements != pr_aot_numstatements)
		return false;
	h = 0x811C9DC5;	// FNV-1a over the statements, as tools/qc2c.py computes it
	s = (byte *)p + p->ofs_statements;
	n = p->numstatements * sizeof(dstatement_t);
	for (i=0 ; i<n ; i++)
		h = (h ^ s[i]) * 0x01000193;
	return h == pr_aot_fingerprint;
}

void PR_AOT_Check (void)
{
	pr_aot_active = false;
	if (!pr_aot_numfunctions)
	{
		Con_Printf ("QuakeC: interpreter (no compiled progs in this build)\n");
		return;
	}
	if (!PR_AOT_Matches (progs))
	{
		Con_Printf ("QuakeC: interpreter (compiled progs do not match this progs.dat)\n");
		return;
	}
#ifdef QOG_EDICT_SPARSE
	if (pr_aot_numfields != progs->entityfields
		|| memcmp (pr_aot_coremap, pr_fieldmap, progs->entityfields))
	{	// QuakeOG: the generated code inlines the core field words
		Con_Printf ("QuakeC: interpreter (compiled progs assume another edict layout)\n");
		return;
	}
#endif
#ifdef QOG_AOT_GLOBALS
	if (!pr_inflash || !pr_aot_numram)
	{	// QuakeOG: the compiled code reads constants from progs.dat in place
		Con_Printf ("QuakeC: interpreter (compiled progs need progs.dat in flash)\n");
		return;
	}
#endif
	if (COM_CheckParm ("-noaot"))
	{
		Con_Printf ("QuakeC: interpreter (-noaot)\n");
		return;
	}
	pr_aot_active = true;
	Con_Printf ("QuakeC: compiled, %i functions%s\n", pr_aot_numfunctions,
		pr_inflash ? ", progs.dat read in place from flash" : "");
}


/*
===============
PR_Init
===============
*/
void PR_Init (void)
{
	Cmd_AddCommand ("edict", ED_PrintEdict_f);
	Cmd_AddCommand ("edicts", ED_PrintEdicts);
	Cmd_AddCommand ("edictcount", ED_Count);
	Cmd_AddCommand ("profile", PR_Profile_f);
	Cvar_RegisterVariable (&nomonsters);
	Cvar_RegisterVariable (&gamecfg);
	Cvar_RegisterVariable (&scratch1);
	Cvar_RegisterVariable (&scratch2);
	Cvar_RegisterVariable (&scratch3);
	Cvar_RegisterVariable (&scratch4);
	Cvar_RegisterVariable (&savedgamecfg);
	Cvar_RegisterVariable (&saved1);
	Cvar_RegisterVariable (&saved2);
	Cvar_RegisterVariable (&saved3);
	Cvar_RegisterVariable (&saved4);
}



edict_t *EDICT_NUM(int n)
{
	if (n < 0 || n >= sv.max_edicts)
		Sys_Error ("EDICT_NUM: bad number %i", n);
	return (edict_t *)((byte *)sv.edicts+ (n)*pr_edict_size);
}

int NUM_FOR_EDICT(edict_t *e)
{
	int		b;
	
	b = (byte *)e - (byte *)sv.edicts;
	b = b / pr_edict_size;
	
	if (b < 0 || b >= sv.num_edicts)
		Sys_Error ("NUM_FOR_EDICT: bad pointer");
	return b;
}
