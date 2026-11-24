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
// r_local.h -- private refresh defs

#ifndef GLQUAKE
#include "r_shared.h"

// QuakeOG: QOG_EDGES_IN_ZBUFFER moves the edge table into the z-buffer and
// the spans into the surface table, which replaces QOG_SPANS_IN_EDGES
#ifdef QOG_EDGES_IN_ZBUFFER
#undef QOG_SPANS_IN_EDGES
#ifndef QOG_EDGE_T_PACKED
#error "QOG_EDGES_IN_ZBUFFER needs QOG_EDGE_T_PACKED"
#endif
#endif

// QuakeOG: QOG_EDGECACHE_ZBUFFER keeps the world's 16-bit edge cache in the
// z-buffer while R_RenderWorld runs (r_draw.c, R_NewMap), not on the hunk
#ifdef QOG_EDGECACHE_ZBUFFER
#ifndef QOG_EDGECACHE16
#error "QOG_EDGECACHE_ZBUFFER needs QOG_EDGECACHE16"
#endif
extern unsigned short	*r_qog_edgecache;
extern int				r_qog_edgecachebytes;
#endif

#define ALIAS_BASE_SIZE_RATIO		(1.0 / 11.0)
					// normalizing factor so player model works out to about
					//  1 pixel per triangle

#define BMODEL_FULLY_CLIPPED	0x10 // value returned by R_BmodelCheckBBox ()
									 //  if bbox is trivially rejected

//===========================================================================
// viewmodel lighting

typedef struct {
	int			ambientlight;
	int			shadelight;
	float		*plightvec;
} alight_t;

//===========================================================================
// clipped bmodel edges

typedef struct bedge_s
{
	mvertex_t		*v[2];
	struct bedge_s	*pnext;
} bedge_t;

// one brush entity polygon after clipping against the world (r_bsp.c)
#ifdef QOG_MAX_BMODEL_VERTS
#define MAX_BMODEL_VERTS	QOG_MAX_BMODEL_VERTS
#define MAX_BMODEL_EDGES	QOG_MAX_BMODEL_EDGES
#else
#define MAX_BMODEL_VERTS	500			// 6K
#define MAX_BMODEL_EDGES	1000		// 12K
#endif
#define BMODEL_CLIP_BYTES	(MAX_BMODEL_VERTS * sizeof(mvertex_t) + MAX_BMODEL_EDGES * sizeof(bedge_t))

typedef struct {
	float	fv[3];		// viewspace x, y
} auxvert_t;

#ifdef QOG_ALIAS_NO_AUXVERTS
// QuakeOG: R_AliasDrawModel keeps no per-vertex view-space array. The
// triangle clipper recomputes a vertex's view-space position from the
// frame's vertices with the same function, so the values are the same.
extern trivertx_t	*r_qog_apverts;		// first vertex of the frame being drawn
void R_AliasTransformAuxVert (auxvert_t *av, trivertx_t *pverts);
#endif

//===========================================================================

extern cvar_t	r_draworder;
extern cvar_t	r_speeds;
extern cvar_t	r_timegraph;
extern cvar_t	r_graphheight;
extern cvar_t	r_clearcolor;
extern cvar_t	r_waterwarp;
extern cvar_t	r_fullbright;
extern cvar_t	r_drawentities;
extern cvar_t	r_aliasstats;
extern cvar_t	r_dspeeds;
#ifdef QOG_RFAST
extern cvar_t	r_fast;		// QuakeOG (Agent AA): Fast render mode (r_main.c)
extern int		r_qogfast;	//   and its value for the current view
#endif
extern cvar_t	r_drawflat;
extern cvar_t	r_ambient;
extern cvar_t	r_reportsurfout;
extern cvar_t	r_maxsurfs;
extern cvar_t	r_numsurfs;
extern cvar_t	r_reportedgeout;
extern cvar_t	r_maxedges;
extern cvar_t	r_numedges;

#define XCENTERING	(1.0 / 2.0)
#define YCENTERING	(1.0 / 2.0)

#define CLIP_EPSILON		0.001

#define BACKFACE_EPSILON	0.01

//===========================================================================

#define	DIST_NOT_SET	98765

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct clipplane_s
{
	vec3_t		normal;
	float		dist;
	struct		clipplane_s	*next;
	byte		leftedge;
	byte		rightedge;
	byte		reserved[2];
} clipplane_t;

extern	clipplane_t	view_clipplanes[4];

//=============================================================================

void R_RenderWorld (void);

//=============================================================================

extern	mplane_t	screenedge[4];

extern	vec3_t	r_origin;

extern	vec3_t	r_entorigin;

extern	float	screenAspect;
extern	float	verticalFieldOfView;
extern	float	xOrigin, yOrigin;

extern	int		r_visframecount;

//=============================================================================

extern int	vstartscan;


void R_ClearPolyList (void);
void R_DrawPolyList (void);

//
// current entity info
//
extern	qboolean		insubmodel;
extern	vec3_t			r_worldmodelorg;


void R_DrawSprite (void);
void R_RenderFace (msurface_t *fa, int clipflags);
void R_RenderPoly (msurface_t *fa, int clipflags);
void R_RenderBmodelFace (bedge_t *pedges, msurface_t *psurf);
void R_TransformPlane (mplane_t *p, float *normal, float *dist);
void R_TransformFrustum (void);
void R_SetSkyFrame (void);
void R_DrawSurfaceBlock16 (void);
void R_DrawSurfaceBlock8 (void);
texture_t *R_TextureAnimation (texture_t *base);

#if	id386

void R_DrawSurfaceBlock8_mip0 (void);
void R_DrawSurfaceBlock8_mip1 (void);
void R_DrawSurfaceBlock8_mip2 (void);
void R_DrawSurfaceBlock8_mip3 (void);

#endif

void R_GenSkyTile (void *pdest);
void R_GenSkyTile16 (void *pdest);
void R_Surf8Patch (void);
void R_Surf16Patch (void);
void R_DrawSubmodelPolygons (model_t *pmodel, int clipflags);
void R_DrawSolidClippedSubmodelPolygons (model_t *pmodel);

void R_AddPolygonEdges (emitpoint_t *pverts, int numverts, int miplevel);
surf_t *R_GetSurf (void);
void R_AliasDrawModel (alight_t *plighting);
void R_BeginEdgeFrame (void);
void R_ScanEdges (void);
void D_DrawSurfaces (void);
void R_InsertNewEdges (edge_t *edgestoadd, edge_t *edgelist);
void R_StepActiveU (edge_t *pedge);
void R_RemoveEdges (edge_t *pedge);

extern void R_Surf8Start (void);
extern void R_Surf8End (void);
extern void R_Surf16Start (void);
extern void R_Surf16End (void);
extern void R_EdgeCodeStart (void);
extern void R_EdgeCodeEnd (void);

extern void R_RotateBmodel (void);

extern int	c_faceclip;
extern int	r_polycount;
extern int	r_wholepolycount;

extern	model_t		*cl_worldmodel;

extern int		*pfrustum_indexes[4];

// !!! if this is changed, it must be changed in asm_draw.h too !!!
#define	NEAR_CLIP	0.01

extern int			ubasestep, errorterm, erroradjustup, erroradjustdown;
extern int			vstartscan;

extern fixed16_t	sadjust, tadjust;
extern fixed16_t	bbextents, bbextentt;

#define MAXBVERTINDEXES	1000	// new clipped vertices when clipping bmodels
								//  to the world BSP
extern mvertex_t	*r_ptverts, *r_ptvertsmax;

extern vec3_t			sbaseaxis[3], tbaseaxis[3];
extern float			entity_rotation[3][3];

extern int		reinit_surfcache;

extern int		r_currentkey;
extern int		r_currentbkey;

typedef struct btofpoly_s {
	int			clipflags;
	msurface_t	*psurf;
} btofpoly_t;

// QuakeOG: only a driver that sets r_worldpolysbacktofront uses this list
// (a 40 KB array in R_RenderWorld's frame), and D_Init always clears it.
// QOG_MAX_BTOFPOLYS 0 removes the array, R_RenderWorld then stops with an
// error if the flag is ever set.
#ifdef QOG_MAX_BTOFPOLYS
#define MAX_BTOFPOLYS	QOG_MAX_BTOFPOLYS
#else
#define MAX_BTOFPOLYS	5000	// FIXME: tune this
#endif

extern int			numbtofpolys;
extern btofpoly_t	*pbtofpolys;

void	R_InitTurb (void);
void	R_ZDrawSubmodelPolys (model_t *clmodel);

//=========================================================
// Alias models
//=========================================================

// QuakeOG: R_AliasDrawModel keeps two work arrays of this many vertices on
// the stack (44 bytes each). The largest shareware model is progs/boss.mdl
// with 325 vertices. Mod_LoadAliasModel still refuses a bigger model with
// "model ... has too many vertices".
#ifdef QOG_MAXALIASVERTS
#define MAXALIASVERTS		QOG_MAXALIASVERTS
#else
#define MAXALIASVERTS		2000	// TODO: tune this
#endif
#define ALIAS_Z_CLIP_PLANE	5

extern int				numverts;
extern int				a_skinwidth;
extern mtriangle_t		*ptriangles;
extern int				numtriangles;
extern aliashdr_t		*paliashdr;
extern mdl_t			*pmdl;
extern float			leftclip, topclip, rightclip, bottomclip;
extern int				r_acliptype;
extern finalvert_t		*pfinalverts;
extern auxvert_t		*pauxverts;

qboolean R_AliasCheckBBox (void);

//=========================================================
// turbulence stuff

#define	AMP		8*0x10000
#define	AMP2	3
#define	SPEED	20

//=========================================================
// particle stuff

void R_DrawParticles (void);
void R_InitParticles (void);
void R_ClearParticles (void);
void R_ReadPointFile_f (void);
void R_SurfacePatch (void);

extern int		r_amodels_drawn;
extern edge_t	*auxedges;
extern int		r_numallocatededges;
extern edge_t	*r_edges, *edge_p, *edge_max;

extern	edgeref_t	newedges[MAXHEIGHT];	// QuakeOG: edgeref_t, see r_shared.h
extern	edgeref_t	removeedges[MAXHEIGHT];

extern	int	screenwidth;

#ifdef QOG_EDGE_T_PACKED
// QuakeOG: the sentinels are the first entries of the edge buffer, so they
// can be linked with 16-bit offsets (set up in R_EdgeDrawing)
#define EDGE_SENTINELS	4
extern	edge_t	*r_edgesentinels;
#define edge_head		(r_edgesentinels[0])
#define edge_tail		(r_edgesentinels[1])
#define edge_aftertail	(r_edgesentinels[2])
#define edge_sentinel	(r_edgesentinels[3])
#if defined(QOG_EDGES_IN_ZBUFFER)
// QuakeOG: the edge table (sentinels first) lives at the end of the
// z-buffer during R_EdgeDrawing, ZEDGE_BYTES of it, and nothing of it is on
// the stack. R_ScanEdges keeps its spans in the surface table after the last
// surface of the frame, up to r_spanpool_end. The table has room for
// QOG_SPAN_MIN spans past the surface limit, so the limit is unchanged. See
// R_EdgeDrawing (r_main.c) and R_QogScanRows (r_edge.c).
#if !defined(QOG_ESPAN_T_PACKED)
#error "QOG_EDGES_IN_ZBUFFER needs QOG_ESPAN_T_PACKED"
#endif
#ifndef QOG_SPAN_MIN
#define QOG_SPAN_MIN	512
#endif
#define SPAN_RESERVE_SURFS	((QOG_SPAN_MIN * sizeof(espan_t) + sizeof(surf_t) - 1) / sizeof(surf_t))
#define ZEDGE_BYTES(n)		((EDGE_SENTINELS + (n)) * (int)sizeof(edge_t))
extern	byte	*r_spanpool_end;
// measurements for the PC qog_rpeaks command (r_edge.c)
extern	int		r_qog_zstack;	// QuakeOG: the others are PC measurements (r_edge.c)
#elif defined(QOG_SPANS_IN_EDGES)
// QuakeOG: R_ScanEdges puts its spans in the edge table after the last edge
// of the frame, up to r_spanpool_end. The table has room for QOG_SPAN_MIN
// spans past the edge limit, so the edge limit is unchanged. Where the spans
// are flushed changes nothing in the picture, each pixel is drawn once.
#if !defined(QOG_ESPAN_T_PACKED)
#error "QOG_SPANS_IN_EDGES needs QOG_ESPAN_T_PACKED"
#endif
#ifndef QOG_SPAN_MIN
#define QOG_SPAN_MIN	512
#endif
// Before that, R_DrawSolidClippedSubmodelPolygons keeps its clip buffers
// there, so the room is the larger of the two.
#define SPAN_RESERVE_BYTES	(QOG_SPAN_MIN * sizeof(espan_t) > BMODEL_CLIP_BYTES ? \
							 QOG_SPAN_MIN * sizeof(espan_t) : BMODEL_CLIP_BYTES)
#define SPAN_RESERVE_EDGES	((SPAN_RESERVE_BYTES + sizeof(edge_t) - 1) / sizeof(edge_t))
extern	byte	*r_spanpool_end;
#endif
#else
// FIXME: make stack vars when debugging done
extern	edge_t	edge_head;
extern	edge_t	edge_tail;
extern	edge_t	edge_aftertail;
#endif
extern int		r_bmodelactive;
extern vrect_t	*pconupdate;

extern float		aliasxscale, aliasyscale, aliasxcenter, aliasycenter;
extern float		r_aliastransition, r_resfudge;

extern int		r_outofsurfaces;
extern int		r_outofedges;

extern mvertex_t	*r_pcurrentvertbase;
extern int			r_maxvalidedgeoffset;

void R_AliasClipTriangle (mtriangle_t *ptri);

extern float	r_time1;
extern float	dp_time1, dp_time2, db_time1, db_time2, rw_time1, rw_time2;
extern float	se_time1, se_time2, de_time1, de_time2, dv_time1, dv_time2;
extern int		r_frustum_indexes[4*6];
extern int		r_maxsurfsseen, r_maxedgesseen, r_cnumsurfs;
extern qboolean	r_surfsonstack;
extern cshift_t	cshift_water;
extern qboolean	r_dowarpold, r_viewchanged;

extern mleaf_t	*r_viewleaf, *r_oldviewleaf;

extern vec3_t	r_emins, r_emaxs;
extern mnode_t	*r_pefragtopnode;
extern int		r_clipflags;
extern int		r_dlightframecount;
extern qboolean	r_fov_greater_than_90;

void R_StoreEfrags (efrag_t **ppefrag);
void R_TimeRefresh_f (void);
void R_TimeGraph (void);
void R_PrintAliasStats (void);
void R_PrintTimes (void);
void R_PrintDSpeeds (void);
void R_AnimateLight (void);
int R_LightPoint (vec3_t p);
void R_SetupFrame (void);
void R_cshift_f (void);
void R_EmitEdge (mvertex_t *pv0, mvertex_t *pv1);
void R_ClipEdge (mvertex_t *pv0, mvertex_t *pv1, clipplane_t *clip);
void R_SplitEntityOnNode2 (mnode_t *node);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);

#endif