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

// refresh.h -- public interface to refresh functions

#define	MAXCLIPPLANES	11

#define	TOP_RANGE		16			// soldier uniform colors
#define	BOTTOM_RANGE	96

//=============================================================================

typedef struct efrag_s
{
	struct mleaf_s		*leaf;
	struct efrag_s		*leafnext;
	struct entity_s		*entity;
	struct efrag_s		*entnext;
} efrag_t;


#ifdef QOG_ENTITY_PACKED
/*
QuakeOG: packed client entity (184 bytes in id's layout, 104 here). Every
client entity_t is kept per level for each edict number, plus the beams and
the static entities. The fields the renderer reads stay as they are. The
client-only ones are kept as they arrive from the network:
- origins as the protocol's shorts (units of 1/8) and angles as its signed
  bytes (units of 360/256). ENT_COORD and ENT_ANGLE give back exactly the
  floats MSG_ReadCoord and MSG_ReadAngle return, so interpolation is the same.
- msgtime as a float. It is only ever set from cl.mtime[0], which only ever
  holds a float read from the message, so the value and every comparison are
  the same as id's double.
- the baseline's model, frame, colormap, skin and effects as the bytes the
  protocol sends.
- update_type, dlightframe and dlightbits are never used for entities.
*/
typedef short			entcoord_t;
typedef signed char		entangle_t;
#define ENT_COORD(c)		((float)(c) * 0.125f)
#define ENT_ANGLE(a)		((float)(a) * (float)(360.0/256))
#define ENT_READCOORD()		((entcoord_t)MSG_ReadShort ())
#define ENT_READANGLE()		((entangle_t)MSG_ReadChar ())

typedef struct
{
	entcoord_t		origin[3];
	entangle_t		angles[3];
	byte			modelindex, frame, colormap, skin, effects;
} entbaseline_t;

typedef struct entity_s
{
	float					msgtime;		// time of last update
	vec3_t					origin;
	vec3_t					angles;
	struct model_s			*model;			// NULL = no model
	struct efrag_s			*efrag;			// linked list of efrags
	int						frame;
	float					syncbase;		// for client-side animations
	byte					*colormap;
	int						effects;		// light, particals, etc
	int						skinnum;		// for Alias models
	int						visframe;		// last frame this entity was
											//  found in an active leaf
	int						trivial_accept;
	struct mnode_s			*topnode;		// for bmodels, first world node
											//  that splits bmodel, or NULL if
											//  not split
	entcoord_t				msg_origins[2][3];	// last two updates (0 is newest)
	entbaseline_t			baseline;		// to fill in defaults in updates
	entangle_t				msg_angles[2][3];	// last two updates (0 is newest)
	byte					forcelink;		// model changed
} entity_t;
#else
#define ENT_COORD(c)		(c)
#define ENT_ANGLE(a)		(a)
#define ENT_READCOORD()		MSG_ReadCoord ()
#define ENT_READANGLE()		MSG_ReadAngle ()

typedef struct entity_s
{
	qboolean				forcelink;		// model changed

	int						update_type;

	entity_state_t			baseline;		// to fill in defaults in updates

	double					msgtime;		// time of last update
	vec3_t					msg_origins[2];	// last two updates (0 is newest)	
	vec3_t					origin;
	vec3_t					msg_angles[2];	// last two updates (0 is newest)
	vec3_t					angles;	
	struct model_s			*model;			// NULL = no model
	struct efrag_s			*efrag;			// linked list of efrags
	int						frame;
	float					syncbase;		// for client-side animations
	byte					*colormap;
	int						effects;		// light, particals, etc
	int						skinnum;		// for Alias models
	int						visframe;		// last frame this entity was
											//  found in an active leaf
											
	int						dlightframe;	// dynamic lighting
	int						dlightbits;
	
// FIXME: could turn these into a union
	int						trivial_accept;
	struct mnode_s			*topnode;		// for bmodels, first world node
											//  that splits bmodel, or NULL if
											//  not split
} entity_t;
#endif
// QuakeOG: copy a network origin or angles into a float vector
#define ENT_COORDCOPY(a,b)	{b[0]=ENT_COORD(a[0]);b[1]=ENT_COORD(a[1]);b[2]=ENT_COORD(a[2]);}
#define ENT_ANGLECOPY(a,b)	{b[0]=ENT_ANGLE(a[0]);b[1]=ENT_ANGLE(a[1]);b[2]=ENT_ANGLE(a[2]);}

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct
{
	vrect_t		vrect;				// subwindow in video for refresh
									// FIXME: not need vrect next field here?
	vrect_t		aliasvrect;			// scaled Alias version
	int			vrectright, vrectbottom;	// right & bottom screen coords
	int			aliasvrectright, aliasvrectbottom;	// scaled Alias versions
	float		vrectrightedge;			// rightmost right edge we care about,
										//  for use in edge list
	float		fvrectx, fvrecty;		// for floating-point compares
	float		fvrectx_adj, fvrecty_adj; // left and top edges, for clamping
	int			vrect_x_adj_shift20;	// (vrect.x + 0.5 - epsilon) << 20
	int			vrectright_adj_shift20;	// (vrectright + 0.5 - epsilon) << 20
	float		fvrectright_adj, fvrectbottom_adj;
										// right and bottom edges, for clamping
	float		fvrectright;			// rightmost edge, for Alias clamping
	float		fvrectbottom;			// bottommost edge, for Alias clamping
	float		horizontalFieldOfView;	// at Z = 1.0, this many X is visible 
										// 2.0 = 90 degrees
	float		xOrigin;			// should probably allways be 0.5
	float		yOrigin;			// between be around 0.3 to 0.5

	vec3_t		vieworg;
	vec3_t		viewangles;
	
	float		fov_x, fov_y;

	int			ambientlight;
} refdef_t;


//
// refresh
//
extern	int		reinit_surfcache;


extern	refdef_t	r_refdef;
extern vec3_t	r_origin, vpn, vright, vup;

extern	struct texture_s	*r_notexture_mip;


void R_Init (void);
void R_InitTextures (void);
void R_InitEfrags (void);
void R_RenderView (void);		// must set r_refdef first
void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect);
								// called whenever r_refdef or vid change
void R_InitSky (struct texture_s *mt);	// called at level load

void R_AddEfrags (entity_t *ent);
void R_RemoveEfrags (entity_t *ent);

void R_NewMap (void);


void R_ParseParticleEffect (void);
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count);
void R_RocketTrail (vec3_t start, vec3_t end, int type);

#ifdef QUAKE2
void R_DarkFieldParticles (entity_t *ent);
#endif
void R_EntityParticles (entity_t *ent);
void R_BlobExplosion (vec3_t org);
void R_ParticleExplosion (vec3_t org);
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength);
void R_LavaSplash (vec3_t org);
void R_TeleportSplash (vec3_t org);

void R_PushDlights (void);


//
// surface cache related
//
extern	int		reinit_surfcache;	// if 1, surface cache is currently empty and
extern qboolean	r_cache_thrash;	// set if thrashing the surface cache

int	D_SurfaceCacheForRes (int width, int height);
void D_FlushCaches (void);
void D_DeleteSurfaceCache (void);
void D_InitCaches (void *buffer, int size);
void R_SetVrect (vrect_t *pvrect, vrect_t *pvrectin, int lineadj);

