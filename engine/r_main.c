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
// r_main.c

#include "quakedef.h"
#include "r_local.h"
#include "qog_phase.h"	// QuakeOG: render phase times (QOG_PHASE_TIME)
#include "qog_aqstats.h"	// QuakeOG (Agent AQ): front end work counts (QOG_AQ_STATS)
#ifdef QOG_AQ_STATS
unsigned	qog_aq[AQ_N];
#endif

//define	PASSAGES

void		*colormap;
vec3_t		viewlightvec;
alight_t	r_viewlighting = {128, 192, viewlightvec};
float		r_time1;
int			r_numallocatededges;
#ifdef QOG_ZTEMP
byte		*r_ztemp_base;	// QuakeOG: the z-buffer memory, set by VID_Init (r_shared.h)
int			r_ztemp_bytes;
#endif
#ifdef QOG_EDGECACHE_ZBUFFER
unsigned short	*r_qog_edgecache;	// QuakeOG: the world's edge cache (r_draw.c), see R_NewMap
int				r_qog_edgecachebytes;
#endif
#ifdef QOG_HUD_IN_ZBUFFER
int			r_zalias_row = 0x7fff;	// QuakeOG: first screen row stored in the z-buffer (set by VID_Init)

// QuakeOG: what Sbar_Draw reads from cl in single player, as it was the
// last time the status bar was drawn (see R_RenderView)
typedef struct
{
	int		stats[MAX_CL_STATS];
	int		items;
	float	item_gettime[32];
	float	faceanimtime;
	double	time;
	char	levelname[40];
	int		viewentity, maxclients, gametype;
} r_hudstate_t;

static r_hudstate_t	r_hudshot;
static qboolean		r_hudshot_valid;
static int			r_hud_sbbefore = -1;	// sb_updates when this frame's view was done

static void R_HudStateGet (r_hudstate_t *s)
{
	memcpy (s->stats, cl.stats, sizeof(s->stats));
	s->items = cl.items;
	memcpy (s->item_gettime, cl.item_gettime, sizeof(s->item_gettime));
	s->faceanimtime = cl.faceanimtime;
	s->time = cl.time;
	memcpy (s->levelname, cl.levelname, sizeof(s->levelname));
	s->viewentity = cl.viewentity;
	s->maxclients = cl.maxclients;
	s->gametype = cl.gametype;
}

static void R_HudStateSet (const r_hudstate_t *s)
{
	memcpy (cl.stats, s->stats, sizeof(s->stats));
	cl.items = s->items;
	memcpy (cl.item_gettime, s->item_gettime, sizeof(s->item_gettime));
	cl.faceanimtime = s->faceanimtime;
	cl.time = s->time;
	memcpy (cl.levelname, s->levelname, sizeof(s->levelname));
	cl.viewentity = s->viewentity;
	cl.maxclients = s->maxclients;
	cl.gametype = s->gametype;
}

/*
===============
R_HudFrameEnd

QuakeOG: called by VID_Update. If Sbar_Draw drew the status bar in this
frame (sb_updates went up from below vid.numpages), keep the state it drew
from, so R_RenderView can draw the same picture again in frames where id's
code would have left it on screen.
===============
*/
#ifdef QOG_HUD_SKIP
/*
QuakeOG (Agent AK): the status bar rows are sent to the panel only when they
change, as id's own VID_Update leaves the status bar out when nothing drew
it. The panel keeps what it shows. A frame whose rows would be the repaint
below (the tile and the status bar drawn again from r_hudshot) with nothing
else drawn over them (SCR_QogHudQuiet), while the panel already shows that
same repaint of the same r_hudshot, leaves the rows as the view left them
and VID_Update sends only the rows above them. r_hudshot_gen counts the
saved states and r_hudpanel_gen is the one the panel shows, -1 for rows
that are not such a repaint. A palette change makes VID_Update send every
row again, after the repaint is done late (R_HudFrameEndRows).
*/
static int			r_hudshot_gen;
static int			r_hudpanel_gen = -1;
static byte			r_hud_viewed;	// R_RenderView's status bar block ran this frame
static byte			r_hud_quiet;	// and nothing after it draws into those rows
static byte			r_hud_fromshot;	// it drew the status bar from r_hudshot
static byte			r_hud_skipnow;	// it left the rows alone
#endif

// QuakeOG: the rows the view's z-buffer overwrote, as a separate frame
// buffer would still hold them (see R_RenderView)
static void R_HudRepaint (void)
{
	extern int	sb_updates;

	Draw_TileClear (0, r_zalias_row, vid.width, vid.height - r_zalias_row);
	r_hud_sbbefore = sb_updates;
	if (sb_updates >= vid.numpages && !cl.intermission)
	{
		if (r_hudshot_valid)
		{
			r_hudstate_t	now;
			int				updates = sb_updates;

			R_HudStateGet (&now);
			R_HudStateSet (&r_hudshot);
			sb_updates = 0;
			Sbar_Draw ();
			sb_updates = updates;
			R_HudStateSet (&now);
#ifdef QOG_HUD_SKIP
			r_hud_fromshot = true;
#endif
		}
		else
			Sbar_Changed ();	// nothing drawn yet, draw it now
	}
}

void R_HudFrameEnd (void)
{
	extern int	sb_updates;
#ifdef QOG_HUD_SKIP
	qboolean	saved = false;
#endif

	if (r_hud_sbbefore >= 0 && r_hud_sbbefore < vid.numpages && sb_updates >= vid.numpages)
	{
		R_HudStateGet (&r_hudshot);
		r_hudshot_valid = true;
#ifdef QOG_HUD_SKIP
		r_hudshot_gen++;
		saved = true;
#endif
	}
#ifdef QOG_HUD_SKIP
	// what the panel shows in the status bar rows after this frame: the
	// repaint of a known r_hudshot, Sbar_Draw's own drawing over the tile,
	// which equals the repaint of the state saved just now, or anything else
	if (!r_hud_skipnow)
		r_hudpanel_gen = r_hud_viewed && r_hud_quiet && !cl.intermission && (r_hud_fromshot || saved)
			? r_hudshot_gen : -1;
	r_hud_viewed = r_hud_quiet = r_hud_fromshot = r_hud_skipnow = false;
#endif
	r_hud_sbbefore = -1;
}

#ifdef QOG_HUD_SKIP
/*
QuakeOG (Agent AK): VID_Update's R_HudFrameEnd. Returns the rows to send:
r_zalias_row when the status bar rows were left alone and the panel shows
them, else vid.height. palette_same is false when the palette changed since
the panel last got every row: then the rows left alone are repainted now
(nothing drew over them since the view, SCR_QogHudQuiet) and all are sent.
*/
int R_HudFrameEndRows (qboolean palette_same)
{
	int		rows = vid.height;

	if (r_hud_skipnow)
	{
		if (palette_same)
			rows = r_zalias_row;
		else
		{
			R_HudRepaint ();
			r_hud_skipnow = false;
		}
	}
	R_HudFrameEnd ();
	return rows;
}
#endif
#endif
qboolean	r_drawpolys;
qboolean	r_drawculledpolys;
qboolean	r_worldpolysbacktofront;
qboolean	r_recursiveaffinetriangles = true;
int			r_pixbytes = 1;
float		r_aliasuvscale = 1.0;
int			r_outofsurfaces;
int			r_outofedges;

qboolean	r_dowarp, r_dowarpold, r_viewchanged;

int			numbtofpolys;
btofpoly_t	*pbtofpolys;
mvertex_t	*r_pcurrentvertbase;

int			c_surf;
int			r_maxsurfsseen, r_maxedgesseen, r_cnumsurfs;
qboolean	r_surfsonstack;
int			r_clipflags;

byte		*r_warpbuffer;

byte		*r_stack_start;

qboolean	r_fov_greater_than_90;

//
// view origin
//
vec3_t	vup, base_vup;
vec3_t	vpn, base_vpn;
vec3_t	vright, base_vright;
vec3_t	r_origin;

//
// screen size info
//
refdef_t	r_refdef;
float		xcenter, ycenter;
float		xscale, yscale;
float		xscaleinv, yscaleinv;
float		xscaleshrink, yscaleshrink;
float		aliasxscale, aliasyscale, aliasxcenter, aliasycenter;

int		screenwidth;

float	pixelAspect;
float	screenAspect;
float	verticalFieldOfView;
float	xOrigin, yOrigin;

mplane_t	screenedge[4];

//
// refresh flags
//
int		r_framecount = 1;	// so frame counts initialized to 0 don't match
int		r_visframecount;
int		d_spanpixcount;
int		r_polycount;
int		r_drawnpolycount;
int		r_wholepolycount;

#define		VIEWMODNAME_LENGTH	256
char		viewmodname[VIEWMODNAME_LENGTH+1];
int			modcount;

int			*pfrustum_indexes[4];
int			r_frustum_indexes[4*6];

int		reinit_surfcache = 1;	// if 1, surface cache is currently empty and
								// must be reinitialized for current cache size

mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

float		r_aliastransition, r_resfudge;

lightstyleval_t	d_lightstylevalue[256];	// 8.8 fraction of base light value (QuakeOG: a short with QOG_LIGHTSTYLE_SHORT, r_shared.h)

float	dp_time1, dp_time2, db_time1, db_time2, rw_time1, rw_time2;
float	se_time1, se_time2, de_time1, de_time2, dv_time1, dv_time2;

void R_MarkLeaves (void);

cvar_t	r_draworder = {"r_draworder","0"};
cvar_t	r_speeds = {"r_speeds","0"};
cvar_t	r_timegraph = {"r_timegraph","0"};
cvar_t	r_graphheight = {"r_graphheight","10"};
cvar_t	r_clearcolor = {"r_clearcolor","2"};
cvar_t	r_waterwarp = {"r_waterwarp","1"};
cvar_t	r_fullbright = {"r_fullbright","0"};
cvar_t	r_drawentities = {"r_drawentities","1"};
cvar_t	r_drawviewmodel = {"r_drawviewmodel","1"};
cvar_t	r_aliasstats = {"r_polymodelstats","0"};
cvar_t	r_dspeeds = {"r_dspeeds","0"};
cvar_t	r_drawflat = {"r_drawflat", "0"};
cvar_t	r_ambient = {"r_ambient", "0"};
cvar_t	r_reportsurfout = {"r_reportsurfout", "0"};
cvar_t	r_maxsurfs = {"r_maxsurfs", "0"};
cvar_t	r_numsurfs = {"r_numsurfs", "0"};
cvar_t	r_reportedgeout = {"r_reportedgeout", "0"};
cvar_t	r_maxedges = {"r_maxedges", "0"};
cvar_t	r_numedges = {"r_numedges", "0"};
cvar_t	r_aliastransbase = {"r_aliastransbase", "200"};
cvar_t	r_aliastransadj = {"r_aliastransadj", "100"};
#ifdef QOG_RFAST
// QuakeOG (Agent AA): r_fast 1 draws the world with the Fast lit span drawer
// (d_litspan.h), approximate and hard to see, r_fast 0 exactly as before.
// Archived. The board client (QOG_RFAST_ON) defaults to 1, every other
// build to 0.
#ifdef QOG_RFAST_ON
cvar_t	r_fast = {"r_fast", "1", true};
#else
cvar_t	r_fast = {"r_fast", "0", true};
#endif
int		r_qogfast;		// r_fast as an int, fixed for the whole view (R_RenderView_)
#endif

extern cvar_t	scr_fov;

void CreatePassages (void);
void SetVisibilityByPassages (void);

/*
==================
R_InitTextures
==================
*/
void	R_InitTextures (void)
{
	int		x,y, m;
	byte	*dest;
	
// create a simple checkerboard texture for the default
	r_notexture_mip = Hunk_AllocName (sizeof(texture_t) + 16*16+8*8+4*4+2*2, "notexture");
	
	r_notexture_mip->width = r_notexture_mip->height = 16;
	r_notexture_mip->mips[0] = (byte *)(r_notexture_mip + 1);
	r_notexture_mip->mips[1] = r_notexture_mip->mips[0] + 16*16;
	r_notexture_mip->mips[2] = r_notexture_mip->mips[1] + 8*8;
	r_notexture_mip->mips[3] = r_notexture_mip->mips[2] + 4*4;

	for (m=0 ; m<4 ; m++)
	{
		dest = r_notexture_mip->mips[m];
		for (y=0 ; y< (16>>m) ; y++)
			for (x=0 ; x< (16>>m) ; x++)
			{
				if (  (y< (8>>m) ) ^ (x< (8>>m) ) )
					*dest++ = 0;
				else
					*dest++ = 0xff;
			}
	}	
}

/*
===============
R_Init
===============
*/
void R_Init (void)
{
	int		dummy;
	
// get stack position so we can guess if we are going to overflow
	r_stack_start = (byte *)&dummy;
	
	R_InitTurb ();
	
	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);	
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);	

	Cvar_RegisterVariable (&r_draworder);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_timegraph);
	Cvar_RegisterVariable (&r_graphheight);
	Cvar_RegisterVariable (&r_drawflat);
	Cvar_RegisterVariable (&r_ambient);
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_aliasstats);
	Cvar_RegisterVariable (&r_dspeeds);
	Cvar_RegisterVariable (&r_reportsurfout);
	Cvar_RegisterVariable (&r_maxsurfs);
	Cvar_RegisterVariable (&r_numsurfs);
	Cvar_RegisterVariable (&r_reportedgeout);
	Cvar_RegisterVariable (&r_maxedges);
	Cvar_RegisterVariable (&r_numedges);
	Cvar_RegisterVariable (&r_aliastransbase);
	Cvar_RegisterVariable (&r_aliastransadj);
#ifdef QOG_RFAST
	Cvar_RegisterVariable (&r_fast);	// QuakeOG (Agent AA)
#endif
#ifdef QOG_RFAST_STATS
	{
		void D_QogRfastStats_f (void);
		Cmd_AddCommand ("qog_rfaststats", D_QogRfastStats_f);	// QuakeOG (Agent AA), PC measurement
	}
#endif

	Cvar_SetValue ("r_maxedges", (float)NUMSTACKEDGES);
	Cvar_SetValue ("r_maxsurfs", (float)NUMSTACKSURFACES);

	view_clipplanes[0].leftedge = true;
	view_clipplanes[1].rightedge = true;
	view_clipplanes[1].leftedge = view_clipplanes[2].leftedge =
			view_clipplanes[3].leftedge = false;
	view_clipplanes[0].rightedge = view_clipplanes[2].rightedge =
			view_clipplanes[3].rightedge = false;

	r_refdef.xOrigin = XCENTERING;
	r_refdef.yOrigin = YCENTERING;

	R_InitParticles ();

// TODO: collect 386-specific code in one place
#if	id386
	Sys_MakeCodeWriteable ((long)R_EdgeCodeStart,
					     (long)R_EdgeCodeEnd - (long)R_EdgeCodeStart);
#endif	// id386

	D_Init ();
}

/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;
	
// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		LEAF_EFRAGS (cl.worldmodel, &cl.worldmodel->leafs[i]) = NULL;
		 	
	r_viewleaf = NULL;
	R_ClearParticles ();

	r_cnumsurfs = r_maxsurfs.value;

	if (r_cnumsurfs <= MINSURFACES)
		r_cnumsurfs = MINSURFACES;

	if (r_cnumsurfs > NUMSTACKSURFACES)
	{
#ifdef QOG_EDGES_IN_ZBUFFER
		// QuakeOG: with room for QOG_SPAN_MIN spans past the last surface
		surfaces = Hunk_AllocName ((r_cnumsurfs + SPAN_RESERVE_SURFS) * sizeof(surf_t), "surfaces");
#else
		surfaces = Hunk_AllocName (r_cnumsurfs * sizeof(surf_t), "surfaces");
#endif
		surface_p = surfaces;
		surf_max = &surfaces[r_cnumsurfs];
		r_surfsonstack = false;
	// surface 0 doesn't really exist; it's just a dummy because index 0
	// is used to indicate no edge attached to surface
		surfaces--;
		R_SurfacePatch ();
	}
	else
	{
		r_surfsonstack = true;
	}

	r_maxedgesseen = 0;
	r_maxsurfsseen = 0;

	r_numallocatededges = r_maxedges.value;

	if (r_numallocatededges < MINEDGES)
		r_numallocatededges = MINEDGES;

#ifdef QOG_SURF_T_SMALL
	// QuakeOG: 16-bit surface links and 8-bit flags (r_shared.h)
	if (r_cnumsurfs >= 0x10000)
		Sys_Error ("R_NewMap: %d surfaces, 16-bit surface links allow %d",
			r_cnumsurfs, 0xFFFF);
	{
		typedef char surf_flags_fit[(SURF_DRAWBACKGROUND <= 0xFF) ? 1 : -1];
	}
#endif

#ifdef QOG_EDGE_T_PACKED
	// QuakeOG: packed edges link with 16-bit byte offsets and name their
	// owner by a 16-bit world edge index (r_shared.h)
	if ((r_numallocatededges + EDGE_SENTINELS + 1) * sizeof(edge_t) > 0x10000)
		Sys_Error ("R_NewMap: %d edges, 16-bit edge links allow %d",
			r_numallocatededges, (int)(0x10000 / sizeof(edge_t)) - EDGE_SENTINELS - 1);
	if (cl.worldmodel->numedges >= EDGE_OWNER_NONE)
		Sys_Error ("R_NewMap: %d map edges, packed edges allow %d",
			cl.worldmodel->numedges, EDGE_OWNER_NONE - 1);
#endif

#ifdef QOG_EDGES_IN_ZBUFFER
	// QuakeOG: the edge table ends at the end of the z-buffer, the brush clip
	// buffers sit at its start (R_EdgeDrawing). auxedges, if any, is only
	// where R_QogEdgesToStack moves the edges (r_edge.c).
	if (!r_ztemp_base || ZEDGE_BYTES (r_numallocatededges) + (int)BMODEL_CLIP_BYTES > r_ztemp_bytes)
		Sys_Error ("R_NewMap: a z-buffer of %d bytes holds no table of %d edges",
			r_ztemp_bytes, r_numallocatededges);
#endif

	if (r_numallocatededges <= NUMSTACKEDGES)
	{
		auxedges = NULL;
	}
	else
	{
#ifdef QOG_EDGE_T_PACKED
		// QuakeOG: room for the sentinels in front, see R_EdgeDrawing
#ifdef QOG_SPANS_IN_EDGES
		auxedges = Hunk_AllocName ((r_numallocatededges + EDGE_SENTINELS + SPAN_RESERVE_EDGES) *
								   sizeof(edge_t), "edges");
#else
		auxedges = Hunk_AllocName ((r_numallocatededges + EDGE_SENTINELS) * sizeof(edge_t),
								   "edges");
#endif
#else
		auxedges = Hunk_AllocName (r_numallocatededges * sizeof(edge_t),
								   "edges");
#endif
	}

#ifdef QOG_EDGECACHE_ZBUFFER
	// QuakeOG: the world's 16-bit edge cache (r_draw.c) is only used while
	// R_RenderWorld runs, so it lives at the start of the z-buffer, in front
	// of the edge table with QOG_EDGES_IN_ZBUFFER (R_EdgeDrawing). A map with
	// more edges than that room has it on the hunk, as the model loader did.
	{
		int	room = r_ztemp_bytes;

#ifdef QOG_EDGES_IN_ZBUFFER
		room -= ZEDGE_BYTES (r_numallocatededges);
#endif
#ifdef QOG_EDGECACHE_TEST_HUNK	// only to test the hunk path
		room = 0;
#endif
		if (r_numallocatededges >= 0x7FFF)
			Sys_Error ("R_NewMap: %d edges, the 16-bit edge cache allows %d",
				r_numallocatededges, 0x7FFF - 1);
		r_qog_edgecachebytes = cl.worldmodel->numedges * sizeof(unsigned short);
		if (r_ztemp_base && r_qog_edgecachebytes <= room)
			r_qog_edgecache = (unsigned short *)r_ztemp_base;
#ifdef QOG_EDGECACHE_FRAME
		// QuakeOG: else in the frame buffer's first rows, which nothing
		// reads before the world's spans draw over them (R_EdgeDrawing
		// checks every frame that the 3D view covers them). For a smaller
		// z-buffer (QOG_ZBUFFER8), whose room the edge table takes.
		else if (r_qog_edgecachebytes <= vid.rowbytes * QOG_EDGECACHE_FRAME)
			r_qog_edgecache = (unsigned short *)vid.buffer;
#endif
		else
			r_qog_edgecache = Hunk_AllocName (r_qog_edgecachebytes, "edgecache");
	}
#endif

	r_dowarpold = false;
	r_viewchanged = false;
#ifdef PASSAGES
CreatePassages ();
#endif
}


/*
===============
R_SetVrect
===============
*/
void R_SetVrect (vrect_t *pvrectin, vrect_t *pvrect, int lineadj)
{
	int		h;
	float	size;

	size = scr_viewsize.value > 100 ? 100 : scr_viewsize.value;
	if (cl.intermission)
	{
		size = 100;
#ifndef QOG_MAX_VIEWSIZE	// QuakeOG: keep the status bar rows out of the 3D view (z-buffer size)
		lineadj = 0;
#endif
	}
	size /= 100;

	h = pvrectin->height - lineadj;
	pvrect->width = pvrectin->width * size;
	if (pvrect->width < 96)
	{
		size = 96.0 / pvrectin->width;
		pvrect->width = 96;	// min for icons
	}
	pvrect->width &= ~7;
	pvrect->height = pvrectin->height * size;
	if (pvrect->height > pvrectin->height - lineadj)
		pvrect->height = pvrectin->height - lineadj;

	pvrect->height &= ~1;

	pvrect->x = (pvrectin->width - pvrect->width)/2;
	pvrect->y = (h - pvrect->height)/2;

	{
		if (lcd_x.value)
		{
			pvrect->y >>= 1;
			pvrect->height >>= 1;
		}
	}
}


/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
#ifdef QOG_RENDER_SCALE
vrect_t		r_qog_screenvrect;	// QuakeOG: where the scaled view goes on screen

/*
===============
R_QogScaleUp

QuakeOG: enlarges the view drawn in r_refdef.vrect to r_qog_screenvrect
(same top left corner) in place, nearest pixel. Rows and pixels are written
from the last to the first, and each one reads a source pixel at or before
its own position that is not yet overwritten.
===============
*/
static void R_QogScaleUp (void)
{
	vrect_t	*src = &r_refdef.vrect, *dst = &r_qog_screenvrect;
	short	xmap[MAXWIDTH];
	byte	*row, *srow;
	int		x, y;

	if (src->width == dst->width && src->height == dst->height)
		return;
	for (x = 0 ; x < dst->width ; x++)
		xmap[x] = x * src->width / dst->width;
	for (y = dst->height - 1 ; y >= 0 ; y--)
	{
		srow = vid.buffer + (src->y + y * src->height / dst->height) * vid.rowbytes + src->x;
		row = vid.buffer + (dst->y + y) * vid.rowbytes + dst->x;
		for (x = dst->width - 1 ; x >= 0 ; x--)
			row[x] = srow[xmap[x]];
	}
}
#endif

void R_ViewChanged (vrect_t *pvrect, int lineadj, float aspect)
{
	int		i;
	float	res_scale;

	r_viewchanged = true;

	R_SetVrect (pvrect, &r_refdef.vrect, lineadj);

#ifdef QOG_RENDER_SCALE
	// QuakeOG (changes the picture, off in the shipped flag lists): the 3D
	// view is drawn at QOG_RENDER_SCALE percent of its screen size into the
	// top left of its screen rectangle, and R_QogScaleUp enlarges it in place
	// before the 2D overlay is drawn at full resolution. The field of view is
	// unchanged. Fewer pixels to draw, and with QOG_ZBUFFER_VIEW the z-buffer
	// only covers the smaller view.
	r_qog_screenvrect = r_refdef.vrect;
	r_refdef.vrect.width = (r_refdef.vrect.width * QOG_RENDER_SCALE / 100) & ~7;
	r_refdef.vrect.height = (r_refdef.vrect.height * QOG_RENDER_SCALE / 100) & ~1;
#endif

	r_refdef.horizontalFieldOfView = 2.0 * tan (r_refdef.fov_x/360*M_PI);
	r_refdef.fvrectx = (float)r_refdef.vrect.x;
	r_refdef.fvrectx_adj = (float)r_refdef.vrect.x - 0.5;
	r_refdef.vrect_x_adj_shift20 = (r_refdef.vrect.x<<20) + (1<<19) - 1;
	r_refdef.fvrecty = (float)r_refdef.vrect.y;
	r_refdef.fvrecty_adj = (float)r_refdef.vrect.y - 0.5;
	r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
	r_refdef.vrectright_adj_shift20 = (r_refdef.vrectright<<20) + (1<<19) - 1;
	r_refdef.fvrectright = (float)r_refdef.vrectright;
	r_refdef.fvrectright_adj = (float)r_refdef.vrectright - 0.5;
	r_refdef.vrectrightedge = (float)r_refdef.vrectright - 0.99;
	r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
	r_refdef.fvrectbottom = (float)r_refdef.vrectbottom;
	r_refdef.fvrectbottom_adj = (float)r_refdef.vrectbottom - 0.5;

	r_refdef.aliasvrect.x = (int)(r_refdef.vrect.x * r_aliasuvscale);
	r_refdef.aliasvrect.y = (int)(r_refdef.vrect.y * r_aliasuvscale);
	r_refdef.aliasvrect.width = (int)(r_refdef.vrect.width * r_aliasuvscale);
	r_refdef.aliasvrect.height = (int)(r_refdef.vrect.height * r_aliasuvscale);
	r_refdef.aliasvrectright = r_refdef.aliasvrect.x +
			r_refdef.aliasvrect.width;
	r_refdef.aliasvrectbottom = r_refdef.aliasvrect.y +
			r_refdef.aliasvrect.height;

	pixelAspect = aspect;
	xOrigin = r_refdef.xOrigin;
	yOrigin = r_refdef.yOrigin;
	
	screenAspect = r_refdef.vrect.width*pixelAspect /
			r_refdef.vrect.height;
// 320*200 1.0 pixelAspect = 1.6 screenAspect
// 320*240 1.0 pixelAspect = 1.3333 screenAspect
// proper 320*200 pixelAspect = 0.8333333

	verticalFieldOfView = r_refdef.horizontalFieldOfView / screenAspect;

// values for perspective projection
// if math were exact, the values would range from 0.5 to to range+0.5
// hopefully they wll be in the 0.000001 to range+.999999 and truncate
// the polygon rasterization will never render in the first row or column
// but will definately render in the [range] row and column, so adjust the
// buffer origin to get an exact edge to edge fill
	xcenter = ((float)r_refdef.vrect.width * XCENTERING) +
			r_refdef.vrect.x - 0.5;
	aliasxcenter = xcenter * r_aliasuvscale;
	ycenter = ((float)r_refdef.vrect.height * YCENTERING) +
			r_refdef.vrect.y - 0.5;
	aliasycenter = ycenter * r_aliasuvscale;

	xscale = r_refdef.vrect.width / r_refdef.horizontalFieldOfView;
	aliasxscale = xscale * r_aliasuvscale;
	xscaleinv = 1.0 / xscale;
	yscale = xscale * pixelAspect;
	aliasyscale = yscale * r_aliasuvscale;
	yscaleinv = 1.0 / yscale;
	xscaleshrink = (r_refdef.vrect.width-6)/r_refdef.horizontalFieldOfView;
	yscaleshrink = xscaleshrink*pixelAspect;

// left side clip
	screenedge[0].normal[0] = -1.0 / (xOrigin*r_refdef.horizontalFieldOfView);
	screenedge[0].normal[1] = 0;
	screenedge[0].normal[2] = 1;
	screenedge[0].type = PLANE_ANYZ;
	
// right side clip
	screenedge[1].normal[0] =
			1.0 / ((1.0-xOrigin)*r_refdef.horizontalFieldOfView);
	screenedge[1].normal[1] = 0;
	screenedge[1].normal[2] = 1;
	screenedge[1].type = PLANE_ANYZ;
	
// top side clip
	screenedge[2].normal[0] = 0;
	screenedge[2].normal[1] = -1.0 / (yOrigin*verticalFieldOfView);
	screenedge[2].normal[2] = 1;
	screenedge[2].type = PLANE_ANYZ;
	
// bottom side clip
	screenedge[3].normal[0] = 0;
	screenedge[3].normal[1] = 1.0 / ((1.0-yOrigin)*verticalFieldOfView);
	screenedge[3].normal[2] = 1;	
	screenedge[3].type = PLANE_ANYZ;
	
	for (i=0 ; i<4 ; i++)
		VectorNormalize (screenedge[i].normal);

	res_scale = sqrt ((double)(r_refdef.vrect.width * r_refdef.vrect.height) /
			          (320.0 * 152.0)) *
			(2.0 / r_refdef.horizontalFieldOfView);
	r_aliastransition = r_aliastransbase.value * res_scale;
	r_resfudge = r_aliastransadj.value * res_scale;

	if (scr_fov.value <= 90.0)
		r_fov_greater_than_90 = false;
	else
		r_fov_greater_than_90 = true;

// TODO: collect 386-specific code in one place
#if	id386
	if (r_pixbytes == 1)
	{
		Sys_MakeCodeWriteable ((long)R_Surf8Start,
						     (long)R_Surf8End - (long)R_Surf8Start);
		colormap = vid.colormap;
		R_Surf8Patch ();
	}
	else
	{
		Sys_MakeCodeWriteable ((long)R_Surf16Start,
						     (long)R_Surf16End - (long)R_Surf16Start);
		colormap = vid.colormap16;
		R_Surf16Patch ();
	}
#endif	// id386

	D_ViewChanged ();
}


/*
===============
R_MarkLeaves
===============
*/
void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int		i;

	if (r_oldviewleaf == r_viewleaf)
		return;
	
	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);
		
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
	{
		if (vis[i>>3] & (1<<(i&7)))
		{
			node = (mnode_t *)&cl.worldmodel->leafs[i+1];
			do
			{
				if (NODE_VISFRAME_IS (cl.worldmodel, node, r_visframecount))	// QuakeOG
					break;
				NODE_VISFRAME_SET (cl.worldmodel, node, r_visframecount);
				node = node->parent;
			} while (node);
		}
	}
}


/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (void)
{
	int			i, j;
	int			lnum;
	alight_t	lighting;
// FIXME: remove and do real lighting
	float		lightvec[3] = {-1, 0, 0};
	vec3_t		dist;
	float		add;

	if (!r_drawentities.value)
		return;

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];

		if (currententity == CL_ENTITY_IFANY (cl.viewentity))
			continue;	// don't draw the player

		switch (currententity->model->type)
		{
		case mod_sprite:
			VectorCopy (currententity->origin, r_entorigin);
			VectorSubtract (r_origin, r_entorigin, modelorg);
			R_DrawSprite ();
			break;

		case mod_alias:
			VectorCopy (currententity->origin, r_entorigin);
			VectorSubtract (r_origin, r_entorigin, modelorg);

		// see if the bounding box lets us trivially reject, also sets
		// trivial accept status
			if (R_AliasCheckBBox ())
			{
				j = R_LightPoint (currententity->origin);
	
				lighting.ambientlight = j;
				lighting.shadelight = j;

				lighting.plightvec = lightvec;

				for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
				{
					if (cl_dlights[lnum].die >= cl.time)
					{
						VectorSubtract (currententity->origin,
										cl_dlights[lnum].origin,
										dist);
						add = cl_dlights[lnum].radius - Length(dist);
	
						if (add > 0)
							lighting.ambientlight += add;
					}
				}
	
			// clamp lighting so it doesn't overbright as much
				if (lighting.ambientlight > 128)
					lighting.ambientlight = 128;
				if (lighting.ambientlight + lighting.shadelight > 192)
					lighting.shadelight = 192 - lighting.ambientlight;

				R_AliasDrawModel (&lighting);
			}

			break;

		default:
			break;
		}
	}
}

/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
// FIXME: remove and do real lighting
	float		lightvec[3] = {-1, 0, 0};
	int			j;
	int			lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	
	if (!r_drawviewmodel.value || r_fov_greater_than_90)
		return;

	if (cl.items & IT_INVISIBILITY)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	VectorCopy (currententity->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	VectorCopy (vup, viewlightvec);
	VectorInverse (viewlightvec);

	j = R_LightPoint (currententity->origin);

	if (j < 24)
		j = 24;		// allways give some light on gun
	r_viewlighting.ambientlight = j;
	r_viewlighting.shadelight = j;

// add dynamic lights		
	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (!dl->radius)
			continue;
		if (dl->die < cl.time)
			continue;

		VectorSubtract (currententity->origin, dl->origin, dist);
		add = dl->radius - Length(dist);
		if (add > 0)
			r_viewlighting.ambientlight += add;
	}

// clamp lighting so it doesn't overbright as much
	if (r_viewlighting.ambientlight > 128)
		r_viewlighting.ambientlight = 128;
	if (r_viewlighting.ambientlight + r_viewlighting.shadelight > 192)
		r_viewlighting.shadelight = 192 - r_viewlighting.ambientlight;

	r_viewlighting.plightvec = lightvec;

#ifdef QUAKE2
	cl.light_level = r_viewlighting.ambientlight;
#endif

	R_AliasDrawModel (&r_viewlighting);
}


/*
=============
R_BmodelCheckBBox
=============
*/
int R_BmodelCheckBBox (model_t *clmodel, float *minmaxs)
{
	int			i, *pindex, clipflags;
	vec3_t		acceptpt, rejectpt;
	double		d;

	clipflags = 0;

	if (currententity->angles[0] || currententity->angles[1]
		|| currententity->angles[2])
	{
		for (i=0 ; i<4 ; i++)
		{
			d = DotProduct (currententity->origin, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d <= -clmodel->radius)
				return BMODEL_FULLY_CLIPPED;

			if (d <= clmodel->radius)
				clipflags |= (1<<i);
		}
	}
	else
	{
		for (i=0 ; i<4 ; i++)
		{
		// generate accept and reject points
		// FIXME: do with fast look-ups or integer tests based on the sign bit
		// of the floating point values

			pindex = pfrustum_indexes[i];

			rejectpt[0] = minmaxs[pindex[0]];
			rejectpt[1] = minmaxs[pindex[1]];
			rejectpt[2] = minmaxs[pindex[2]];
			
#ifdef QOG_BMODEL_BBOXF
			// QuakeOG (Agent AQ): id's d is a float dot product less a float
			// dist in double, which is exact in sign and zero only when the
			// two are equal, so d <= 0 is the float compare dot <= dist
			if (DotProduct (rejectpt, view_clipplanes[i].normal) <= view_clipplanes[i].dist)
				return BMODEL_FULLY_CLIPPED;
#else
			d = DotProduct (rejectpt, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d <= 0)
				return BMODEL_FULLY_CLIPPED;
#endif

			acceptpt[0] = minmaxs[pindex[3+0]];
			acceptpt[1] = minmaxs[pindex[3+1]];
			acceptpt[2] = minmaxs[pindex[3+2]];

#ifdef QOG_BMODEL_BBOXF
			if (DotProduct (acceptpt, view_clipplanes[i].normal) <= view_clipplanes[i].dist)
				clipflags |= (1<<i);
#else
			d = DotProduct (acceptpt, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d <= 0)
				clipflags |= (1<<i);
#endif
		}
	}

	return clipflags;
}


#ifdef QOG_BMODEL_FRUSTUM
/*
QuakeOG (Agent AQ): R_TransformFrustum makes each view plane's normal from
vright, vup and vpn alone and its dist as DotProduct (modelorg, normal).
A brush entity that does not turn usually keeps the world's vectors bit for
bit (R_QogIdentityRotate changes only the sign of some zeros), and the
planes put back after it come from the world's vectors too. So while the
normals in view_clipplanes are known to be those of the world's vectors
(*pworld), such an entity and the put back planes take them and compute
only the four dists. Anything else runs R_TransformFrustum. The same inputs
give the same bits, and no copy of the planes is kept.
*/
extern void R_QogRotateBmodelVecs (void);

static qboolean R_QogWorldVecs (void)
{
	return !memcmp (vright, base_vright, sizeof(vec3_t)) && !memcmp (vup, base_vup, sizeof(vec3_t)) &&
		   !memcmp (vpn, base_vpn, sizeof(vec3_t));
}

static void R_QogFrustum (qboolean *pworld)
{
	int		i;

	if (R_QogWorldVecs ())
	{
		if (*pworld)
		{
			QOG_AQ (AQ_BFRDIST, 1);
			for (i=0 ; i<4 ; i++)
				view_clipplanes[i].dist = DotProduct (modelorg, view_clipplanes[i].normal);
			return;
		}
		QOG_AQ (AQ_BFRFULL, 1);
		R_TransformFrustum ();
		*pworld = true;
		return;
	}
	QOG_AQ (AQ_BFRFULL, 1);
	R_TransformFrustum ();
	*pworld = false;
}
#endif

/*
=============
R_DrawBEntitiesOnList
=============
*/
void R_DrawBEntitiesOnList (void)
{
	int			i, j, k, clipflags;
	vec3_t		oldorigin;
	model_t		*clmodel;
	float		minmaxs[6];
#ifdef QOG_BMODEL_FRUSTUM
	qboolean	qworld = false;	// QuakeOG (Agent AQ): R_QogFrustum
#endif

	if (!r_drawentities.value)
		return;

	VectorCopy (modelorg, oldorigin);
	insubmodel = true;
	r_dlightframecount = r_framecount;

	for (i=0 ; i<cl_numvisedicts ; i++)
	{
		currententity = cl_visedicts[i];
#ifdef QOG_SURF_T_TINY
		r_qog_visedict = i;		// QuakeOG (Agent S): the surfaces' entity (SURF_ENTITY)
#endif

		switch (currententity->model->type)
		{
		case mod_brush:

			clmodel = currententity->model;

		// see if the bounding box lets us trivially reject, also sets
		// trivial accept status
			for (j=0 ; j<3 ; j++)
			{
				minmaxs[j] = currententity->origin[j] +
						clmodel->mins[j];
				minmaxs[3+j] = currententity->origin[j] +
						clmodel->maxs[j];
			}

			clipflags = R_BmodelCheckBBox (clmodel, minmaxs);
			QOG_AQ (AQ_BENTS, 1);
			QOG_AQ (AQ_BBOXREJ, clipflags == BMODEL_FULLY_CLIPPED);

			if (clipflags != BMODEL_FULLY_CLIPPED)
			{
#ifdef QOG_AQ_STATS
				unsigned	aqwc[3] = { qog_wc_count[QOG_WC_CLIPEDGE], qog_wc_count[QOG_WC_EMITEDGE], qog_wc_count[QOG_WC_PROJECT] };

				QOG_AQ (AQ_BROTANG, currententity->angles[0] || currententity->angles[1] || currententity->angles[2]);
#endif
				VectorCopy (currententity->origin, r_entorigin);
				VectorSubtract (r_origin, r_entorigin, modelorg);
			// FIXME: is this needed?
				VectorCopy (modelorg, r_worldmodelorg);
		
				r_pcurrentvertbase = clmodel->vertexes;
		
			// FIXME: stop transforming twice
#ifdef QOG_BMODEL_FRUSTUM
				R_QogRotateBmodelVecs ();	// QuakeOG (Agent AQ)
				R_QogFrustum (&qworld);
#else
				R_RotateBmodel ();
#endif
#ifdef QOG_FRONT_FAST
				R_QogFxSetup ();	// QuakeOG (Agent AJ): Fast mode's integer front end
#endif

			// calculate dynamic lighting for bmodel if it's not an
			// instanced model
				if (clmodel->firstmodelsurface != 0)
				{
					for (k=0 ; k<MAX_DLIGHTS ; k++)
					{
						if ((cl_dlights[k].die < cl.time) ||
							(!cl_dlights[k].radius))
						{
							continue;
						}

						R_MarkLights (&cl_dlights[k], 1<<k,
							clmodel->nodes + clmodel->hulls[0].firstclipnode);
						QOG_AQ (AQ_BMARK, 1);
					}
				}

			// if the driver wants polygons, deliver those. Z-buffering is on
			// at this point, so no clipping to the world tree is needed, just
			// frustum clipping
				if (r_drawpolys | r_drawculledpolys)
				{
					R_ZDrawSubmodelPolys (clmodel);
				}
				else
				{
					r_pefragtopnode = NULL;

					for (j=0 ; j<3 ; j++)
					{
						r_emins[j] = minmaxs[j];
						r_emaxs[j] = minmaxs[3+j];
					}

					R_SplitEntityOnNode2 (cl.worldmodel->nodes);
					QOG_AQ (AQ_BNOTOP, !r_pefragtopnode);

					if (r_pefragtopnode)
					{
						ENT_TOPNODE (currententity) = r_pefragtopnode;

						if (r_pefragtopnode->contents >= 0)
						{
						// not a leaf; has to be clipped to the world BSP
							r_clipflags = clipflags;
							QOG_AQ (AQ_BCLIPPED, 1);
							R_DrawSolidClippedSubmodelPolygons (clmodel);
						}
						else
						{
						// falls entirely in one leaf, so we just put all the
						// edges in the edge list and let 1/z sorting handle
						// drawing order
							QOG_AQ (AQ_BONELEAF, 1);
							R_DrawSubmodelPolygons (clmodel, clipflags);
						}

						ENT_TOPNODE (currententity) = NULL;
					}
				}
#ifdef QOG_AQ_STATS
				QOG_AQ (AQ_BCLIPEDGE, qog_wc_count[QOG_WC_CLIPEDGE] - aqwc[0]);
				QOG_AQ (AQ_BEMIT, qog_wc_count[QOG_WC_EMITEDGE] - aqwc[1]);
				QOG_AQ (AQ_BPROJ, qog_wc_count[QOG_WC_PROJECT] - aqwc[2]);
#endif

			// put back world rotation and frustum clipping		
			// FIXME: R_RotateBmodel should just work off base_vxx
				VectorCopy (base_vpn, vpn);
				VectorCopy (base_vup, vup);
				VectorCopy (base_vright, vright);
				VectorCopy (base_modelorg, modelorg);
				VectorCopy (oldorigin, modelorg);
#ifdef QOG_BMODEL_FRUSTUM
				R_QogFrustum (&qworld);	// QuakeOG (Agent AQ)
#else
				R_TransformFrustum ();
#endif
			}

			break;

		default:
			break;
		}
	}

	insubmodel = false;
}


/*
================
R_EdgeDrawing
================
*/
int		r_qog_peakedges, r_qog_peaksurfs;	// QuakeOG: see the end of R_EdgeDrawing
#ifdef QOG_SURF_T_TINY
int		r_qog_visedict;						// QuakeOG (Agent S): see r_shared.h
#endif
int		r_qog_peakedgesurfbytes;			// QuakeOG: most edge plus surface bytes in one frame

void R_EdgeDrawing (void)
{
#ifdef QOG_EDGE_T_PACKED
	// QuakeOG: the four sentinel edges come first, then the edge table.
	// r_edgebase is one entry before the sentinels, so offset 0 (EDGE_NONE)
	// never addresses an edge. 24-byte edges gain nothing from id's cache
	// line alignment, so the table is not aligned.
	extern medge_t		*r_qog_owneredges;
	extern unsigned int	r_qog_ownerbytes;
#if defined(QOG_EDGES_IN_ZBUFFER)
	// QuakeOG: no edge table here, it is at the end of the z-buffer (below)
#elif defined(QOG_SPANS_IN_EDGES)
	// QuakeOG: R_ScanEdges keeps its spans in the part of this table the
	// edges left free, with QOG_SPAN_MIN spans of room past the edge limit
	edge_t	ledges[EDGE_SENTINELS + NUMSTACKEDGES + SPAN_RESERVE_EDGES];
#else
	edge_t	ledges[EDGE_SENTINELS + NUMSTACKEDGES];
#endif
#else
	edge_t	ledges[NUMSTACKEDGES +
				((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];
#endif
#ifdef QOG_EDGES_IN_ZBUFFER
	// QuakeOG: R_ScanEdges keeps its spans in the part of this table the
	// surfaces left free, with QOG_SPAN_MIN spans of room past the limit
	surf_t	lsurfs[NUMSTACKSURFACES + SPAN_RESERVE_SURFS +
				((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];
#else
	surf_t	lsurfs[NUMSTACKSURFACES +
				((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];
#endif

#ifdef QOG_EDGE_T_PACKED
#ifdef QOG_EDGES_IN_ZBUFFER
	// QuakeOG: z-buffer layout while the edges are built (R_RenderWorld and
	// R_DrawBEntitiesOnList), byte offsets from r_ztemp_base:
	//   0: the world's edge cache with QOG_EDGECACHE_ZBUFFER (R_RenderWorld
	//      only), then the brush clip buffers (r_bsp.c, brush entities only)
	//   r_ztemp_bytes - ZEDGE_BYTES: the four sentinels and the edge table
	// Nothing has drawn into the z-buffer yet this frame. R_ScanEdges moves
	// the used part of the table to the end and draws spans above it only.
	r_edgesentinels = (edge_t *)(r_ztemp_base + r_ztemp_bytes - ZEDGE_BYTES (r_numallocatededges));
#else
	r_edgesentinels = auxedges ? auxedges : ledges;
#endif
	r_edgebase = (byte *)r_edgesentinels - sizeof(edge_t);
	r_edges = r_edgesentinels + EDGE_SENTINELS;
#ifdef QOG_SPANS_IN_EDGES
	r_spanpool_end = (byte *)(r_edges + (auxedges ? r_numallocatededges : NUMSTACKEDGES) +
							  SPAN_RESERVE_EDGES);
#endif
	// id's sentinels were zeroed statics, and R_ScanEdges sets only some fields
#ifdef QOG_VID_SCANRACE
	VID_QogRaceMark (1);	// QuakeOG (Agent AP): the world's front end
#endif
#if defined(QOG_VID_SCANRACE) && defined(QOG_EDGES_IN_ZBUFFER)
	// QuakeOG (Agent AK): the last frame's scanout may still read the start
	// of the z-buffer (the status bar rows), which a big edge table reaches
	VID_QogScanFree (r_edgesentinels, r_ztemp_base + r_ztemp_bytes);
#endif
	memset (r_edgesentinels, 0, EDGE_SENTINELS * sizeof(edge_t));
	r_qog_owneredges = cl.worldmodel->edges;
	r_qog_ownerbytes = cl.worldmodel->numedges * sizeof(medge_t);
#else
	if (auxedges)
	{
		r_edges = auxedges;
	}
	else
	{
		r_edges =  (edge_t *)
				(((long)&ledges[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
	}
#endif

	if (r_surfsonstack)
	{
		surfaces =  (surf_t *)
				(((long)&lsurfs[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
		surf_max = &surfaces[r_cnumsurfs];
	// surface 0 doesn't really exist; it's just a dummy because index 0
	// is used to indicate no edge attached to surface
		surfaces--;
		R_SurfacePatch ();
	}
#ifdef QOG_EDGES_IN_ZBUFFER
	r_spanpool_end = r_surfsonstack ? (byte *)&lsurfs[sizeof(lsurfs) / sizeof(lsurfs[0])] :
					 (byte *)(surfaces + 1 + r_cnumsurfs + SPAN_RESERVE_SURFS);
#endif

	R_BeginEdgeFrame ();

#ifdef QOG_EDGECACHE_ZBUFFER
#ifdef QOG_EDGECACHE_FRAME
	extern pixel_t	*d_viewbuffer;	// d_local.h

	// QuakeOG: a cache in the frame buffer needs the 3D view to draw over
	// all of it in this frame (a full-width view from the top, drawn in the
	// frame buffer itself) and nothing on screen (the previous frame's
	// scanout has ended, VID_Update waits for it)
#ifdef QOG_RTURBO
	// QuakeOG (Agent AO): a Turbo view places the caches itself, after its
	// packed pixels when that saves the wait for the last scanout (r_qogturbo.c)
	void	R_QogTurboCaches (void);

	R_QogTurboCaches ();
	if (!r_qog_turboview)
#endif
	if ((byte *)r_qog_edgecache == vid.buffer
		&& (r_refdef.vrect.x || r_refdef.vrect.y || r_refdef.vrect.width != vid.width
			|| r_refdef.vrect.height * (int)vid.rowbytes < r_qog_edgecachebytes
			|| d_viewbuffer != vid.buffer))
		Sys_Error ("R_EdgeDrawing: the 3D view does not cover the edge cache in the frame buffer");
#endif
	// QuakeOG: entries from earlier frames never match (r_draw.c), and the
	// z-buffer has held z since, so the cache starts empty every frame
#ifdef QOG_VID_SCANRACE
	// QuakeOG (Agent AK): in the frame buffer's first rows, which the last
	// frame's scanout reads first
	VID_QogScanFree (r_qog_edgecache, (byte *)r_qog_edgecache + r_qog_edgecachebytes);
#endif
	memset (r_qog_edgecache, 0xFF, r_qog_edgecachebytes);
#endif

	if (r_dspeeds.value)
	{
		rw_time1 = Sys_FloatTime ();
	}

	{
	QOG_PH_T (ph);	// QuakeOG
#ifdef QOG_VERTCACHE
	R_QogVertCacheBegin ();	// QuakeOG (Agent Z): world vertices projected once (r_draw.c)
#endif
	R_RenderWorld ();
#if defined(QOG_VERTCACHE) && !defined(QOG_FRONT_FAST)
	R_QogVertCacheEnd ();
#endif
	QOG_PH_ADD (QOG_PH_WORLD, ph);
	}

#ifdef QOG_VID_SCANRACE
	VID_QogRaceMark (2);	// QuakeOG (Agent AP): after the world's front end
	// QuakeOG (Agent AP): the brush entities' front end overlaps the last
	// frame's scanout too. Of the frame and the z-buffer it writes only the
	// edge table (released above) and the brush clip buffers at the start
	// of the z-buffer, which hold the status bar rows a scanout may still
	// send. The wait for the scanout's end is before R_ScanEdges, which
	// draws anywhere in the frame and the z-buffer.
#if defined(QOG_EDGES_IN_ZBUFFER)
	VID_QogScanFree (r_ztemp_base, r_ztemp_base + BMODEL_CLIP_BYTES);
#else
	VID_QogScanDone ();
#endif
#endif

	if (r_drawculledpolys)
	{
#ifdef QOG_VID_SCANRACE
		VID_QogScanDone ();
#endif
		R_ScanEdges ();
	}

// only the world can be drawn back to front with no z reads or compares, just
// z writes, so have the driver turn z compares on now
	D_TurnZOn ();

	if (r_dspeeds.value)
	{
		rw_time2 = Sys_FloatTime ();
		db_time1 = rw_time2;
	}

	{
	QOG_PH_T (ph);	// QuakeOG
	R_DrawBEntitiesOnList ();
#ifdef QOG_FRONT_FAST
	// QuakeOG (Agent AJ): the Fast header after the cache serves the brush
	// entities too (the cache itself is for world faces only)
	R_QogVertCacheEnd ();
#endif
	QOG_PH_ADD (QOG_PH_BMODELS, ph);
	}
#ifdef QOG_VID_SCANRACE
	VID_QogRaceMark (3);	// QuakeOG (Agent AP): after the brush entities
	// QuakeOG (Agent AK): from here on the view draws anywhere in the frame
	// and the z-buffer (spans): the last frame's scanout has to be over. The
	// front end above (world and, Agent AP, brush entities) overlapped it.
	VID_QogScanDone ();
#endif

	if (r_dspeeds.value)
	{
		db_time2 = Sys_FloatTime ();
		se_time1 = db_time2;
	}

	if (!r_dspeeds.value)
	{
		VID_UnlockBuffer ();
		S_ExtraUpdate ();	// don't let sound get messed up if going slow
		VID_LockBuffer ();
	}
	
	// QuakeOG: high-water marks, to size these tables from measurements
	// (qog_mem prints them)
	if (edge_p - r_edges > r_qog_peakedges)
		r_qog_peakedges = edge_p - r_edges;
	if (surface_p - surfaces > r_qog_peaksurfs)
		r_qog_peaksurfs = surface_p - surfaces;
	if ((int)((edge_p - r_edges) * sizeof(edge_t) + (surface_p - surfaces) * sizeof(surf_t)) > r_qog_peakedgesurfbytes)
		r_qog_peakedgesurfbytes = (edge_p - r_edges) * sizeof(edge_t) + (surface_p - surfaces) * sizeof(surf_t);

	if (!(r_drawpolys | r_drawculledpolys))
	{
		QOG_PH_T (ph);	// QuakeOG
		R_ScanEdges ();
		QOG_PH_ADD (QOG_PH_SCAN, ph);
	}
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView_ (void)
{
#ifdef QOG_WARP_INPLACE
	// QuakeOG: underwater, the view is drawn into the frame buffer and
	// D_WarpScreen distorts it in place, so no 64 KB warp buffer on the stack
	if (vid.rowbytes != WARP_WIDTH
#ifdef QOG_RTURBO
		&& !r_qog_turboview	// QuakeOG (Agent AO): a Turbo view's rows are half as wide (r_qogturbo.c)
#endif
		)
		Sys_Error ("R_RenderView: in-place underwater warp needs %d bytes per row", WARP_WIDTH);
	r_warpbuffer = vid.buffer;
#else
	byte	warpbuffer[WARP_WIDTH * WARP_HEIGHT];

	r_warpbuffer = warpbuffer;
#endif

	QOG_PH_T (ph_total);	// QuakeOG
	QOG_PH_T (ph);

	if (r_timegraph.value || r_speeds.value || r_dspeeds.value)
		r_time1 = Sys_FloatTime ();

#ifdef QOG_RFAST
	r_qogfast = r_fast.value != 0;	// QuakeOG (Agent AA): one mode for the whole view
#endif
	R_SetupFrame ();

#ifdef PASSAGES
SetVisibilityByPassages ();
#else
	R_MarkLeaves ();	// done here so we know if we're in water
#endif
	QOG_PH_ADD (QOG_PH_SETUP, ph);	// QuakeOG

// make FDIV fast. This reduces timing precision after we've been running for a
// while, so we don't do it globally.  This also sets chop mode, and we do it
// here so that setup stuff like the refresh area calculations match what's
// done in screen.c
	Sys_LowFPPrecision ();

	if (!cl_entities[0].model || !cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");
		
	if (!r_dspeeds.value)
	{
		VID_UnlockBuffer ();
		S_ExtraUpdate ();	// don't let sound get messed up if going slow
		VID_LockBuffer ();
	}
	
	R_EdgeDrawing ();

	if (!r_dspeeds.value)
	{
		VID_UnlockBuffer ();
		S_ExtraUpdate ();	// don't let sound get messed up if going slow
		VID_LockBuffer ();
	}
	
	if (r_dspeeds.value)
	{
		se_time2 = Sys_FloatTime ();
		de_time1 = se_time2;
	}

	{
	QOG_PH_T (ph);	// QuakeOG
	R_DrawEntitiesOnList ();
	QOG_PH_ADD (QOG_PH_ENTITIES, ph);
	}

	if (r_dspeeds.value)
	{
		de_time2 = Sys_FloatTime ();
		dv_time1 = de_time2;
	}

	{
	QOG_PH_T (ph);	// QuakeOG
	R_DrawViewModel ();
	QOG_PH_ADD (QOG_PH_VIEWMODEL, ph);
	}

	if (r_dspeeds.value)
	{
		dv_time2 = Sys_FloatTime ();
		dp_time1 = Sys_FloatTime ();
	}

	{
	QOG_PH_T (ph);	// QuakeOG
	R_DrawParticles ();
	QOG_PH_ADD (QOG_PH_PARTICLES, ph);
	}

	if (r_dspeeds.value)
		dp_time2 = Sys_FloatTime ();

#ifdef QOG_RENDER_SCALE
	R_QogScaleUp ();
	if (r_dowarp)
	{
		// QuakeOG: the scaled up view fills the screen rectangle, so warp that
		vrect_t	drawn = r_refdef.vrect;

		r_refdef.vrect = r_qog_screenvrect;
		D_WarpScreen ();
		r_refdef.vrect = drawn;
	}
#else
	if (r_dowarp)
		D_WarpScreen ();
#endif

	V_SetContentsColor (r_viewleaf->contents);

	if (r_timegraph.value)
		R_TimeGraph ();

	if (r_aliasstats.value)
		R_PrintAliasStats ();
		
	if (r_speeds.value)
		R_PrintTimes ();

	if (r_dspeeds.value)
		R_PrintDSpeeds ();

	if (r_reportsurfout.value && r_outofsurfaces)
		Con_Printf ("Short %d surfaces\n", r_outofsurfaces);

	if (r_reportedgeout.value && r_outofedges)
		Con_Printf ("Short roughly %d edges\n", r_outofedges * 2 / 3);

// back to high floating-point precision
	Sys_HighFPPrecision ();
	QOG_PH_ADD (QOG_PH_TOTAL, ph_total);	// QuakeOG
	QOG_PH_COUNT (QOG_PC_FRAMES, 1);
#ifdef QOG_AQ_STATS
	// QuakeOG (Agent AQ): PC measurement, sums since the start every 100 views
	if (++qog_aq[AQ_VIEWS] % 100 == 0)
	{
		int		i;
		char	line[600];
		int		k = 0;

		k += sprintf (line + k, "qog_aqstats");
		for (i=0 ; i<AQ_N ; i++)
			k += sprintf (line + k, " %u", qog_aq[i]);
#ifdef QOG_PHASE_XIP
		k += sprintf (line + k, " wc");
		for (i=0 ; i<QOG_WC_N ; i++)
			k += sprintf (line + k, " %u", qog_wc_count[i]);
#endif
		Sys_Printf ("%s\n", line);
	}
#endif
}

void R_RenderView (void)
{
	int		dummy;
	int		delta;
	
	delta = (byte *)&dummy - r_stack_start;
	if (delta < -10000 || delta > 10000)
		Sys_Error ("R_RenderView: called without enough stack");

	if ( Hunk_LowMark() & 3 )
		Sys_Error ("Hunk is missaligned");

	if ( (long)(&dummy) & 3 )
		Sys_Error ("Stack is missaligned");

	if ( (long)(&r_warpbuffer) & 3 )
		Sys_Error ("Globals are missaligned");

#ifdef QOG_LINK_RX_ZTEMP
	{	// QuakeOG (Agent Y): the link's receive buffer shares the z-buffer (net_link.c)
		void Link_RxCheckIdle (void);
		Link_RxCheckIdle ();
	}
#endif
#ifdef QOG_RTURBO
	{	// QuakeOG (Agent AO): r_fast 2 draws the view at half width (r_qogturbo.c)
		void	R_QogTurboBegin (void), R_QogTurboEnd (void);

		R_QogTurboBegin ();
		R_RenderView_ ();
		R_QogTurboEnd ();
	}
#else
	R_RenderView_ ();
#endif

#ifdef QOG_HUD_IN_ZBUFFER
	// QuakeOG: the screen rows from r_zalias_row down (the status bar) share
	// memory with the start of the z-buffer, which this view has just
	// overwritten. Repaint what a separate frame buffer would still hold:
	// the tile background a full screen update leaves there, and, when
	// Sbar_Draw is going to skip this frame (sb_updates), the status bar as
	// it was last drawn, from the state saved by R_HudFrameEnd. Sbar_Draw
	// redraws by itself in the other frames. The 2D overlay comes after this.
	if (r_zalias_row < vid.height)
	{
		QOG_PH_T (hp_hud);	// QuakeOG (Agent V)
#ifdef QOG_HUD_SKIP
		extern int	sb_updates;
		qboolean	SCR_QogHudQuiet (void);

		// QuakeOG (Agent AK): the panel already shows the repaint of this
		// r_hudshot and nothing else will draw there: leave the rows
		r_hud_viewed = true;
		r_hud_quiet = SCR_QogHudQuiet ();
		if (r_hud_quiet && sb_updates >= vid.numpages && !cl.intermission && r_hudshot_valid
			&& r_hudpanel_gen == r_hudshot_gen)
		{
			r_hud_sbbefore = sb_updates;
			r_hud_skipnow = true;
#ifdef QOG_HUD_SKIP_CHECK
			R_HudRepaint ();	// PC test builds: vid_pc.c checks it against the panel
#endif
		}
		else
#endif
		R_HudRepaint ();
		QOG_HP_ADD (QOG_HP_HUD, hp_hud);	// QuakeOG
	}
#endif
}

/*
================
R_InitTurb
================
*/
void R_InitTurb (void)
{
#ifdef QOG_CONST_RTABLES
// QuakeOG: the tables are constants (r_turbtab.h). The PC build checks them
// against id's formulas once, so a changed formula or size cannot go unseen.
#ifdef HOST_TEST
	int		i;

	for (i=0 ; i<(SIN_BUFFER_SIZE) ; i++)
	{
		int	s = AMP + sin(i*3.14159*2/CYCLE)*AMP;
		int	t = AMP2 + sin(i*3.14159*2/CYCLE)*AMP2;
		if (s != sintable[i] || t != intsintable[i])
			Sys_Error ("R_InitTurb: r_turbtab.h differs at %d", i);
	}
#endif
#else
	int		i;
	
	for (i=0 ; i<(SIN_BUFFER_SIZE) ; i++)
	{
		sintable[i] = AMP + sin(i*3.14159*2/CYCLE)*AMP;
		intsintable[i] = AMP2 + sin(i*3.14159*2/CYCLE)*AMP2;	// AMP2, not 20
	}
#endif
}

