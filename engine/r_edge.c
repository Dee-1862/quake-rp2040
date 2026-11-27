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
// r_edge.c

#include "quakedef.h"
#include "r_local.h"
#include "qog_bmodel.h"	// QuakeOG, board cost model counts (QOG_BOARDMODEL)
#include "qog_aqstats.h"	// QuakeOG, front end work counts (QOG_AQ_STATS)
#ifdef QOG_EDGES_IN_ZBUFFER
#include "d_local.h"	// QuakeOG, d_pzbuffer, d_zwidth (R_QogScanRows)
#endif

#if 0
// FIXME
the complex cases add new polys on most lines, so dont optimize for keeping them the same
have multiple free span lists to try to get better coherence?
low depth complexity -- 1 to 3 or so

this breaks spans at every edge, even hidden ones (bad)

have a sentinal at both ends?
#endif


edge_t	*auxedges;
edge_t	*r_edges, *edge_p, *edge_max;

surf_t	*surfaces, *surface_p, *surf_max;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack

edgeref_t	newedges[MAXHEIGHT];	// QuakeOG, edgeref_t, see r_shared.h
edgeref_t	removeedges[MAXHEIGHT];

espan_t	*span_p, *max_span_p;
#ifdef QOG_ESPAN_T_PACKED
byte	*r_spanbase;	// QuakeOG, see espan_t in r_shared.h
#if defined(QOG_SPANS_IN_EDGES) || defined(QOG_EDGES_IN_ZBUFFER)
byte	*r_spanpool_end;	// QuakeOG, end of the span room, see r_local.h
#endif
// QuakeOG, span measurements for the PC qog_rpeaks command (port/pc/vid_pc.c)
int		r_qog_peakspanroom, r_qog_minspanroom, r_qog_peakframespans, r_qog_peakflushspans;
#endif

int		r_currentkey;

extern	int	screenwidth;

int	current_iv;

int	edge_head_u_shift20, edge_tail_u_shift20;

static void (*pdrawfunc)(void);

#ifdef QOG_EDGE_T_PACKED
edge_t	*r_edgesentinels;	// QuakeOG, in the edge buffer, see r_local.h
byte	*r_edgebase;
#else
edge_t	edge_head;
edge_t	edge_tail;
edge_t	edge_aftertail;
edge_t	edge_sentinel;
#endif

float	fv;

void R_GenerateSpans (void);
void R_GenerateSpansBackward (void);

void R_LeadingEdge (edge_t *edge);
void R_LeadingEdgeBackwards (edge_t *edge);
void R_TrailingEdge (surf_t *surf, edge_t *edge);


//=============================================================================


/*
==============
R_DrawCulledPolys
==============
*/
void R_DrawCulledPolys (void)
{
	surf_t			*s;
	msurface_t		*pface;

	currententity = &cl_entities[0];

	if (r_worldpolysbacktofront)
	{
		for (s=surface_p-1 ; s>&surfaces[1] ; s--)
		{
			if (!s->spans)
				continue;

			if (!(s->flags & SURF_DRAWBACKGROUND))
			{
				pface = (msurface_t *)SURF_DATA (s);	// QuakeOG, SURF_DATA
				R_RenderPoly (pface, 15);
			}
		}
	}
	else
	{
		for (s = &surfaces[1] ; s<surface_p ; s++)
		{
			if (!s->spans)
				continue;

			if (!(s->flags & SURF_DRAWBACKGROUND))
			{
				pface = (msurface_t *)SURF_DATA (s);	// QuakeOG, SURF_DATA
				R_RenderPoly (pface, 15);
			}
		}
	}
}


/*
==============
R_BeginEdgeFrame
==============
*/
void R_BeginEdgeFrame (void)
{
	int		v;

	edge_p = r_edges;
	edge_max = &r_edges[r_numallocatededges];

	surface_p = &surfaces[2];	// background is surface 1,
								//  surface 0 is a dummy
	surfaces[1].spans = SPAN_NONE;	// no background spans yet (QuakeOG, SPAN_NONE)
	surfaces[1].flags = SURF_DRAWBACKGROUND;

// put the background behind everything in the world
	if (r_draworder.value)
	{
		pdrawfunc = R_GenerateSpansBackward;
		surfaces[1].key = 0;
		r_currentkey = 1;
	}
	else
	{
		pdrawfunc = R_GenerateSpans;
		surfaces[1].key = SURF_KEY_BACK;	// QuakeOG, 0x7FFFFFFF, or 0xFFFF for 16-bit keys
		r_currentkey = 0;
	}

// FIXME: set with memset
	for (v=r_refdef.vrect.y ; v<r_refdef.vrectbottom ; v++)
	{
		newedges[v] = removeedges[v] = EDGE_NONE;
	}
}


#if	!id386

/*
==============
R_InsertNewEdges

Adds the edges in the linked list edgestoadd, adding them to the edges in the
linked list edgelist.  edgestoadd is assumed to be sorted on u, and non-empty (this is actually newedges[v]).  edgelist is assumed to be sorted on u, with a
sentinel at the end (actually, this is the active edge table starting at
edge_head.next).
==============
*/
// QuakeOG, links through EDGE_PTR/EDGE_REF (r_shared.h), id's pointers by default
void R_InsertNewEdges (edge_t *edgestoadd, edge_t *edgelist)
{
	edgeref_t	next_edge;

	do
	{
		next_edge = edgestoadd->next;
edgesearch:
		if (edgelist->u >= edgestoadd->u)
			goto addedge;
		edgelist=EDGE_PTR(edgelist->next);
		if (edgelist->u >= edgestoadd->u)
			goto addedge;
		edgelist=EDGE_PTR(edgelist->next);
		if (edgelist->u >= edgestoadd->u)
			goto addedge;
		edgelist=EDGE_PTR(edgelist->next);
		if (edgelist->u >= edgestoadd->u)
			goto addedge;
		edgelist=EDGE_PTR(edgelist->next);
		goto edgesearch;

	// insert edgestoadd before edgelist
addedge:
		edgestoadd->next = EDGE_REF(edgelist);
		edgestoadd->prev = edgelist->prev;
		EDGE_PTR(edgelist->prev)->next = EDGE_REF(edgestoadd);
		edgelist->prev = EDGE_REF(edgestoadd);
	} while (next_edge != EDGE_NONE && (edgestoadd = EDGE_PTR(next_edge)));
}

#endif	// !id386
	

#if	!id386

/*
==============
R_RemoveEdges
==============
*/
void R_RemoveEdges (edge_t *pedge)
{

	do
	{
		EDGE_PTR(pedge->next)->prev = pedge->prev;	// QuakeOG, EDGE_PTR
		EDGE_PTR(pedge->prev)->next = pedge->next;
	} while (pedge->nextremove != EDGE_NONE && (pedge = EDGE_PTR(pedge->nextremove)));
}

#endif	// !id386


#if	!id386

#ifndef QOG_SCAN_FAST	// QuakeOG, else further down
/*
==============
R_StepActiveU
==============
*/
void R_StepActiveU (edge_t *pedge)
{
	edge_t		*pnext_edge, *pwedge;

	while (1)
	{
nextedge:
		pedge->u += pedge->u_step;
		if (pedge->u < EDGE_PTR(pedge->prev)->u)
			goto pushback;
		pedge = EDGE_PTR(pedge->next);
			
		pedge->u += pedge->u_step;
		if (pedge->u < EDGE_PTR(pedge->prev)->u)
			goto pushback;
		pedge = EDGE_PTR(pedge->next);
			
		pedge->u += pedge->u_step;
		if (pedge->u < EDGE_PTR(pedge->prev)->u)
			goto pushback;
		pedge = EDGE_PTR(pedge->next);
			
		pedge->u += pedge->u_step;
		if (pedge->u < EDGE_PTR(pedge->prev)->u)
			goto pushback;
		pedge = EDGE_PTR(pedge->next);
			
		goto nextedge;		
		
pushback:
		if (pedge == &edge_aftertail)
			return;
			
	// push it back to keep it sorted		
		pnext_edge = EDGE_PTR(pedge->next);	// QuakeOG, EDGE_PTR/EDGE_REF

	// pull the edge out of the edge list
		EDGE_PTR(pedge->next)->prev = pedge->prev;
		EDGE_PTR(pedge->prev)->next = pedge->next;

	// find out where the edge goes in the edge list
		pwedge = EDGE_PTR(EDGE_PTR(pedge->prev)->prev);

		while (pwedge->u > pedge->u)
		{
			pwedge = EDGE_PTR(pwedge->prev);
		}

	// put the edge back into the edge list
		pedge->next = pwedge->next;
		pedge->prev = EDGE_REF(pwedge);
		EDGE_PTR(pedge->next)->prev = EDGE_REF(pedge);
		pwedge->next = EDGE_REF(pedge);

		pedge = pnext_edge;
		if (pedge == &edge_tail)
			return;
	}
}
#endif	// QOG_SCAN_FAST

#endif	// !id386


/*
==============
R_CleanupSpan
==============
*/
void R_CleanupSpan ()
{
	surf_t	*surf;
	int		iu;
	espan_t	*span;

// now that we've reached the right edge of the screen, we're done with any
// unfinished surfaces, so emit a span for whatever's on top
	surf = SURF_PTR(surfaces[1].next);
	iu = edge_tail_u_shift20;
	if (iu > surf->last_u)
	{
		span = span_p++;
		span->u = surf->last_u;
		span->count = iu - span->u;
		span->v = current_iv;
		span->pnext = surf->spans;
		surf->spans = SPAN_REF(span);	// QuakeOG
	}

// reset spanstate for all surfaces in the surface stack
	do
	{
		surf->spanstate = 0;
		surf = SURF_PTR(surf->next);
	} while (surf != &surfaces[1]);
}


/*
==============
R_LeadingEdgeBackwards
==============
*/
void R_LeadingEdgeBackwards (edge_t *edge)
{
	espan_t			*span;
	surf_t			*surf, *surf2;
	int				iu;

// it's adding a new surface in, so find the correct place
	surf = &surfaces[edge->surfs[1]];

// don't start a span if this is an inverted span, with the end
// edge preceding the start edge (that is, we've already seen the
// end edge)
	if (++surf->spanstate == 1)
	{
		surf2 = SURF_PTR(surfaces[1].next);

		if (surf->key > surf2->key)
			goto newtop;

	// if it's two surfaces on the same plane, the one that's already
	// active is in front, so keep going unless it's a bmodel
		if (surf->insubmodel && (surf->key == surf2->key))
		{
		// must be two bmodels in the same leaf; don't care, because they'll
		// never be farthest anyway
			goto newtop;
		}

continue_search:

		do
		{
			surf2 = SURF_PTR(surf2->next);
		} while (surf->key < surf2->key);

		if (surf->key == surf2->key)
		{
		// if it's two surfaces on the same plane, the one that's already
		// active is in front, so keep going unless it's a bmodel
			if (!surf->insubmodel)
				goto continue_search;

		// must be two bmodels in the same leaf; don't care which is really
		// in front, because they'll never be farthest anyway
		}

		goto gotposition;

newtop:
	// emit a span (obscures current top)
		iu = edge->u >> 20;

		if (iu > surf2->last_u)
		{
			span = span_p++;
			span->u = surf2->last_u;
			span->count = iu - span->u;
			span->v = current_iv;
			span->pnext = surf2->spans;
			surf2->spans = SPAN_REF(span);	// QuakeOG
		}

		// set last_u on the new span
		surf->last_u = iu;
				
gotposition:
	// insert before surf2
		surf->next = SURF_REF(surf2);	// QuakeOG, SURF_PTR/SURF_REF
		surf->prev = surf2->prev;
		SURF_PTR(surf2->prev)->next = SURF_REF(surf);
		surf2->prev = SURF_REF(surf);
	}
}


/*
==============
R_TrailingEdge
==============
*/
void R_TrailingEdge (surf_t *surf, edge_t *edge)
{
	espan_t			*span;
	int				iu;

// don't generate a span if this is an inverted span, with the end
// edge preceding the start edge (that is, we haven't seen the
// start edge yet)
	if (--surf->spanstate == 0)
	{
		if (surf->insubmodel)
			r_bmodelactive--;

		if (surf == SURF_PTR(surfaces[1].next))	// QuakeOG, SURF_PTR
		{
		// emit a span (current top going away)
			iu = edge->u >> 20;
			if (iu > surf->last_u)
			{
				span = span_p++;
				span->u = surf->last_u;
				span->count = iu - span->u;
				span->v = current_iv;
				span->pnext = surf->spans;
				surf->spans = SPAN_REF(span);	// QuakeOG
			}

		// set last_u on the surface below
			SURF_PTR(surf->next)->last_u = iu;
		}

		SURF_PTR(surf->prev)->next = surf->next;
		SURF_PTR(surf->next)->prev = surf->prev;
	}
}


#if	!id386

#ifdef QOG_LEADZ_FLOAT
/*
==============
R_QogZFront

QuakeOG, id's 1/z order of two brush entity surfaces that share
a key, in float with id's doubles only near a tie. Returns 1 where id's
code finds surf in front of surf2 (newzibottom >= testzi, or newzitop >=
testzi and surf's d_zistepu >= surf2's), else 0.

id computes newzi = (double)(zo + fv*zv) + fu*(double)zu, with the first
part in float and the second product exact in double, then multiplies by
the double 0.99 or 1.01 and compares with testzi made the same way. Here
a = zo + fv*zv is id's own float, the rest is float, b = fu*zu, n = a + b,
n*0.99f, n*1.01f and t the same for surf2. Each float step rounds by at
most 2^-24 of its size and id's doubles by 2^-53, and 0.99f and 1.01f
differ from id's doubles by less than 2^-24 of themselves, so the float
difference d = n*0.99f - t is within 11.2 2^-24 S of id's (S the sum of
|a| and |b| of both surfaces), which is below 2^(e-17) for e the largest
exponent of the four. Only |d| below that (or anything not finite) takes
id's double code. Under 2^-100 the bound is taken as 2^-100, so flushed
denormal products cannot matter.
==============
*/
#if defined(__GNUC__)
typedef unsigned int __attribute__((may_alias))	r_qogbits_t;
#else
typedef unsigned int							r_qogbits_t;
#endif
#define QOG_FBITS(f)	(*(r_qogbits_t *)&(f))

static int R_QogZFrontDouble (surf_t *surf, surf_t *surf2, edge_t *edge)
{
	double	fu, newzi, testzi, newzitop, newzibottom;

	fu = (float)(edge->u - 0xFFFFF) * (1.0 / 0x100000);
	newzi = surf->d_ziorigin + fv*surf->d_zistepv +
			fu*surf->d_zistepu;
	newzibottom = newzi * 0.99;

	testzi = surf2->d_ziorigin + fv*surf2->d_zistepv +
			fu*surf2->d_zistepu;

	if (newzibottom >= testzi)
		return 1;

	newzitop = newzi * 1.01;
	if (newzitop >= testzi)
	{
		if (surf->d_zistepu >= surf2->d_zistepu)
			return 1;
	}
	return 0;
}

/*
QuakeOG (QOG_SCAN_FAST), surf's half of the compare (its fu, its
float part a, its b, n = a + b and the larger exponent of a and b) depends
only on surf, the edge and the scan line, and 96% of the compares in demo3
come from R_LeadingEdge's search loop, which compares the same surf with
several surfaces of its key. So that half is kept for the edge in a cache
the caller clears (the same statements, the same bits).
*/
typedef struct
{
	int				have;
	float			fu, n;
	unsigned int	e;
} r_qogzc_t;

static int R_QogZFrontC (surf_t *surf, surf_t *surf2, edge_t *edge, r_qogzc_t *c)
{
	float			fu, a1, b1, a2, b2, n, t, d;
	unsigned int	e, ea, lim, db;

	if (!c->have)
	{
		fu = (float)(edge->u - 0xFFFFF);
		if (QOG_FBITS (fu) & 0x7FFFFFFF)
			QOG_FBITS (fu) -= 20u << 23;		// times 2^-20, exact, id's fu
		a1 = surf->d_ziorigin + fv*surf->d_zistepv;				// id's float part
		b1 = fu*surf->d_zistepu;
		e = QOG_FBITS (a1) & 0x7F800000;
		ea = QOG_FBITS (b1) & 0x7F800000;
		if (ea > e) e = ea;
		c->fu = fu;
		c->n = a1 + b1;
		c->e = e;
		c->have = 1;
	}
	fu = c->fu;
	n = c->n;
	e = c->e;
	a2 = surf2->d_ziorigin + fv*surf2->d_zistepv;
	b2 = fu*surf2->d_zistepu;

	// the largest biased exponent of the four
	ea = QOG_FBITS (a2) & 0x7F800000;
	if (ea > e) e = ea;
	ea = QOG_FBITS (b2) & 0x7F800000;
	if (ea > e) e = ea;
	if (e == 0x7F800000)
		goto ids;		// not finite
	lim = e > (44u << 23) ? e - (17u << 23) : (27u << 23);	// 2^(e-17), at least 2^-100

	t = a2 + b2;
#ifdef QOG_LEADZ_BITS
	/*
	QuakeOG, most compares are clear from the bits of n and t alone, with
	the same result as the float code below and never one it would leave to
	id's doubles. For n and t positive and not below 2^(e-9), bits that
	differ by more than 2^18 mean the larger value is over 1/64 larger, so
	n*0.99f - t and n*1.01f - t have the sign of that side by more than
	2^(e-17). Bits within 2^15 mean the two are within 0.4%, so the first
	is below -2^(e-17) and the second above 2^(e-17), and id's order is
	the one of the steps.
	*/
	{
		int				nb = (int)QOG_FBITS (n), tb = (int)QOG_FBITS (t), diff;
		unsigned int	lo = e - (9u << 23);

		if ((nb | tb) >= 0 && e - (44u << 23) < (156u << 23) &&
			(unsigned int)nb >= lo && (unsigned int)tb >= lo)
		{
			diff = nb - tb;
			if (diff > 0x40000)
				return 1;
			if (diff < -0x40000)
				return 0;
			if ((unsigned int)(diff + 0x8000) <= 0x10000u)
			{
				QOG_AQ (AQ_LEADZNEAR, 1);
				return surf->d_zistepu >= surf2->d_zistepu;
			}
		}
		QOG_AQ (AQ_LEADZMID, 1);
	}
#endif
	d = n*0.99f - t;
	db = QOG_FBITS (d) & 0x7FFFFFFF;
	if (db > lim)
	{
		if (!(QOG_FBITS (d) & 0x80000000))
			return 1;	// newzibottom >= testzi
	}
	else
		goto ids;

	d = n*1.01f - t;
	db = QOG_FBITS (d) & 0x7FFFFFFF;
	if (db <= lim)
		goto ids;
	if (QOG_FBITS (d) & 0x80000000)
		return 0;		// newzitop < testzi
	return surf->d_zistepu >= surf2->d_zistepu;

ids:
	QOG_AQ (AQ_LEADZFB, 1);
	return R_QogZFrontDouble (surf, surf2, edge);
}

static int R_QogZFront (surf_t *surf, surf_t *surf2, edge_t *edge)
{
	r_qogzc_t	c;

	c.have = 0;
	return R_QogZFrontC (surf, surf2, edge, &c);
}

#ifdef QOG_LEADZ_CHECK
// PC check build, both ways on every compare, mismatches counted
unsigned	r_qog_leadzn, r_qog_leadzbad;

static int R_QogZFrontCheck (surf_t *surf, surf_t *surf2, edge_t *edge)
{
	int	a = R_QogZFront (surf, surf2, edge);
	int	b = R_QogZFrontDouble (surf, surf2, edge);

	r_qog_leadzn++;
	if (a != b)
		r_qog_leadzbad++;
	if ((r_qog_leadzn & 0xFFFF) == 0)
		Sys_Printf ("qog_leadzcheck %u compares, %u differ\n", r_qog_leadzn, r_qog_leadzbad);
	return b;
}
#define R_QOGZFRONT	R_QogZFrontCheck

static int R_QogZFrontCCheck (surf_t *surf, surf_t *surf2, edge_t *edge, r_qogzc_t *c)
{
	int	a = R_QogZFrontC (surf, surf2, edge, c);
	int	b = R_QogZFrontDouble (surf, surf2, edge);

	r_qog_leadzn++;
	if (a != b)
		r_qog_leadzbad++;
	if ((r_qog_leadzn & 0xFFFF) == 0)
		Sys_Printf ("qog_leadzcheck %u compares, %u differ\n", r_qog_leadzn, r_qog_leadzbad);
	return b;
}
#define R_QOGZFRONTC	R_QogZFrontCCheck
#else
#define R_QOGZFRONT	R_QogZFront
#define R_QOGZFRONTC	R_QogZFrontC
#endif
#endif

/*
==============
R_LeadingEdge
==============
*/
void R_LeadingEdge (edge_t *edge)
{
	espan_t			*span;
	surf_t			*surf, *surf2;
	int				iu;
#ifndef QOG_LEADZ_FLOAT
	double			fu, newzi, testzi, newzitop, newzibottom;
#endif

	if (edge->surfs[1])
	{
	// it's adding a new surface in, so find the correct place
		surf = &surfaces[edge->surfs[1]];

	// don't start a span if this is an inverted span, with the end
	// edge preceding the start edge (that is, we've already seen the
	// end edge)
		if (++surf->spanstate == 1)
		{
			if (surf->insubmodel)
				r_bmodelactive++;

			surf2 = SURF_PTR(surfaces[1].next);

			if (surf->key < surf2->key)
				goto newtop;

		// if it's two surfaces on the same plane, the one that's already
		// active is in front, so keep going unless it's a bmodel
			if (surf->insubmodel && (surf->key == surf2->key))
			{
			// must be two bmodels in the same leaf; sort on 1/z
				QOG_AQ (AQ_LEADZ, 1);
#ifdef QOG_LEADZ_FLOAT
				if (R_QOGZFRONT (surf, surf2, edge))	// QuakeOG
					goto newtop;
#else
				fu = (float)(edge->u - 0xFFFFF) * (1.0 / 0x100000);
				newzi = surf->d_ziorigin + fv*surf->d_zistepv +
						fu*surf->d_zistepu;
				newzibottom = newzi * 0.99;

				testzi = surf2->d_ziorigin + fv*surf2->d_zistepv +
						fu*surf2->d_zistepu;

				if (newzibottom >= testzi)
				{
					goto newtop;
				}

				newzitop = newzi * 1.01;
				if (newzitop >= testzi)
				{
					if (surf->d_zistepu >= surf2->d_zistepu)
					{
						goto newtop;
					}
				}
#endif
			}

continue_search:

			do
			{
				surf2 = SURF_PTR(surf2->next);
			} while (surf->key > surf2->key);

			if (surf->key == surf2->key)
			{
			// if it's two surfaces on the same plane, the one that's already
			// active is in front, so keep going unless it's a bmodel
				if (!surf->insubmodel)
					goto continue_search;

			// must be two bmodels in the same leaf; sort on 1/z
				QOG_AQ (AQ_LEADZ, 1);
#ifdef QOG_LEADZ_FLOAT
				if (R_QOGZFRONT (surf, surf2, edge))	// QuakeOG
					goto gotposition;
#else
				fu = (float)(edge->u - 0xFFFFF) * (1.0 / 0x100000);
				newzi = surf->d_ziorigin + fv*surf->d_zistepv +
						fu*surf->d_zistepu;
				newzibottom = newzi * 0.99;

				testzi = surf2->d_ziorigin + fv*surf2->d_zistepv +
						fu*surf2->d_zistepu;

				if (newzibottom >= testzi)
				{
					goto gotposition;
				}

				newzitop = newzi * 1.01;
				if (newzitop >= testzi)
				{
					if (surf->d_zistepu >= surf2->d_zistepu)
					{
						goto gotposition;
					}
				}
#endif

				goto continue_search;
			}

			goto gotposition;

newtop:
		// emit a span (obscures current top)
			iu = edge->u >> 20;

			if (iu > surf2->last_u)
			{
				span = span_p++;
				span->u = surf2->last_u;
				span->count = iu - span->u;
				span->v = current_iv;
				span->pnext = surf2->spans;
				surf2->spans = SPAN_REF(span);	// QuakeOG
			}

			// set last_u on the new span
			surf->last_u = iu;
				
gotposition:
		// insert before surf2
			surf->next = SURF_REF(surf2);	// QuakeOG, SURF_PTR/SURF_REF
			surf->prev = surf2->prev;
			SURF_PTR(surf2->prev)->next = SURF_REF(surf);
			surf2->prev = SURF_REF(surf);
		}
	}
}


#ifdef QOG_SCAN_FAST
#ifndef QOG_LEADZ_FLOAT
#error "QOG_SCAN_FAST needs QOG_LEADZ_FLOAT (R_QOGZFRONT)"
#endif
/*
==============
R_GenerateSpans

QuakeOG, id's R_GenerateSpans, R_TrailingEdge and R_LeadingEdge
as one loop with the same statements in the same order. The engine is
built with -fno-strict-aliasing, so every store made GCC load again the
globals behind the links (r_edgebase, surfaces, r_spanbase, span_p,
current_iv), and the two calls per edge saved and restored registers. Here
those values are locals for the whole scan line (nothing in the loop
changes them, the 1/z order reads only the surfaces, the edge and fv), and
span_p and r_bmodelactive are written back before R_CleanupSpan, which
reads span_p. r_bmodelactive is only counted, never read, so its final
value is id's.
==============
*/
QOG_SCANRAM("R_GenerateSpans")
void R_GenerateSpans (void)
{
	edge_t			*edge, *tail;
	surf_t			*surfs, *head, *surf, *surf2;
	espan_t			*span, *sp;
	int				iu, v, bmact;
	unsigned		s0, s1;
	r_qogzc_t		zc;	// surf's half of the 1/z compares of one edge
#ifdef QOG_EDGE_T_PACKED
	byte			*ebase = r_edgebase;
#define QE(r)		((edge_t *)(ebase + (r)))
#else
#define QE(r)		(r)
#endif
#ifdef QOG_ESPAN_T_PACKED
	byte			*sbase = r_spanbase;
#define QSPREF(p)	((espanref_t)((byte *)(p) - sbase))
#else
#define QSPREF(p)	(p)
#endif
#ifdef QOG_SURF_T_SMALL
#define QS(r)		(surfs + (r))
#define QSREF(p)	((surfref_t)((p) - surfs))
#else
#define QS(r)		(r)
#define QSREF(p)	(p)
#endif

	QOG_BM (QOG_BM_SCANLINES, 1);	// QuakeOG
	QOG_AQ (AQ_SCANLINES, 1);
	surfs = surfaces;
	head = &surfs[1];
	sp = span_p;
	v = current_iv;
	bmact = 0;
	tail = &edge_tail;

// clear active surfaces to just the background surface
	head->next = head->prev = QSREF(head);
	head->last_u = edge_head_u_shift20;

// generate spans
	for (edge=QE(edge_head.next) ; edge != tail; edge=QE(edge->next))
	{
		QOG_BM (QOG_BM_SCANEDGES, 1);	// QuakeOG
		QOG_AQ (AQ_SCANEDGES, 1);
		s0 = edge->surfs[0];
		s1 = edge->surfs[1];
		if (s0)
		{
		// it has a left surface, so a surface is going away for this span
		// (id's R_TrailingEdge)
			surf = &surfs[s0];
			if (--surf->spanstate == 0)
			{
				if (surf->insubmodel)
					bmact--;

				if (surf == QS(head->next))
				{
				// emit a span (current top going away)
					iu = edge->u >> 20;
					if (iu > surf->last_u)
					{
						span = sp++;
						span->u = surf->last_u;
						span->count = iu - span->u;
						span->v = v;
						span->pnext = surf->spans;
						surf->spans = QSPREF(span);
					}

				// set last_u on the surface below
					QS(surf->next)->last_u = iu;
				}

				QS(surf->prev)->next = surf->next;
				QS(surf->next)->prev = surf->prev;
			}

			if (!s1)
				continue;
		}
		else if (!s1)
			continue;	// id's R_LeadingEdge does nothing without a right surface

	// it's adding a new surface in, so find the correct place (id's
	// R_LeadingEdge)
		surf = &surfs[s1];

	// don't start a span if this is an inverted span, with the end
	// edge preceding the start edge (that is, we've already seen the
	// end edge)
		if (++surf->spanstate == 1)
		{
			if (surf->insubmodel)
				bmact++;

			zc.have = 0;
			surf2 = QS(head->next);

			if (surf->key < surf2->key)
				goto newtop;

		// if it's two surfaces on the same plane, the one that's already
		// active is in front, so keep going unless it's a bmodel
			if (surf->insubmodel && (surf->key == surf2->key))
			{
			// must be two bmodels in the same leaf; sort on 1/z
				QOG_AQ (AQ_LEADZ, 1);
				if (R_QOGZFRONTC (surf, surf2, edge, &zc))
					goto newtop;
			}

continue_search:

			do
			{
				surf2 = QS(surf2->next);
			} while (surf->key > surf2->key);

			if (surf->key == surf2->key)
			{
			// if it's two surfaces on the same plane, the one that's already
			// active is in front, so keep going unless it's a bmodel
				if (!surf->insubmodel)
					goto continue_search;

			// must be two bmodels in the same leaf; sort on 1/z
				QOG_AQ (AQ_LEADZ, 1);
				QOG_AQ (AQ_LEADZ2, 1);
				QOG_AQ (AQ_LEADZHIT, zc.have);
				if (R_QOGZFRONTC (surf, surf2, edge, &zc))
					goto gotposition;

				goto continue_search;
			}

			goto gotposition;

newtop:
		// emit a span (obscures current top)
			iu = edge->u >> 20;

			if (iu > surf2->last_u)
			{
				span = sp++;
				span->u = surf2->last_u;
				span->count = iu - span->u;
				span->v = v;
				span->pnext = surf2->spans;
				surf2->spans = QSPREF(span);
			}

			// set last_u on the new span
			surf->last_u = iu;

gotposition:
		// insert before surf2
			surf->next = QSREF(surf2);
			surf->prev = surf2->prev;
			QS(surf2->prev)->next = QSREF(surf);
			surf2->prev = QSREF(surf);
		}
	}

	span_p = sp;
	r_bmodelactive = bmact;
	R_CleanupSpan ();
#undef QE
#undef QSPREF
#undef QS
#undef QSREF
}

/*
==============
R_StepActiveU

QuakeOG, id's loop with r_edgebase, the tail and the aftertail
sentinel in locals, for the same reason as above.
==============
*/
QOG_SCANRAM("R_StepActiveU")
void R_StepActiveU (edge_t *pedge)
{
	edge_t		*pnext_edge, *pwedge, *tail, *aftertail;
#ifdef QOG_EDGE_T_PACKED
	byte		*ebase = r_edgebase;
#define QE(r)		((edge_t *)(ebase + (r)))
#define QEREF(p)	((edgeref_t)((byte *)(p) - ebase))
#else
#define QE(r)		(r)
#define QEREF(p)	(p)
#endif

	tail = &edge_tail;
	aftertail = &edge_aftertail;
	while (1)
	{
		do
		{
			pedge->u += pedge->u_step;
			if (pedge->u < QE(pedge->prev)->u)
				break;
			pedge = QE(pedge->next);
		} while (1);

		if (pedge == aftertail)
			return;

	// push it back to keep it sorted
		pnext_edge = QE(pedge->next);

	// pull the edge out of the edge list
		QE(pedge->next)->prev = pedge->prev;
		QE(pedge->prev)->next = pedge->next;

	// find out where the edge goes in the edge list
		pwedge = QE(QE(pedge->prev)->prev);

		while (pwedge->u > pedge->u)
		{
			pwedge = QE(pwedge->prev);
		}

	// put the edge back into the edge list
		pedge->next = pwedge->next;
		pedge->prev = QEREF(pwedge);
		QE(pedge->next)->prev = QEREF(pedge);
		pwedge->next = QEREF(pedge);

		pedge = pnext_edge;
		if (pedge == tail)
			return;
	}
#undef QE
#undef QEREF
}

#else	// QOG_SCAN_FAST

/*
==============
R_GenerateSpans
==============
*/
void R_GenerateSpans (void)
{
	edge_t			*edge;
	surf_t			*surf;

	r_bmodelactive = 0;
	QOG_BM (QOG_BM_SCANLINES, 1);	// QuakeOG
	QOG_AQ (AQ_SCANLINES, 1);

// clear active surfaces to just the background surface
	surfaces[1].next = surfaces[1].prev = SURF_REF(&surfaces[1]);	// QuakeOG
	surfaces[1].last_u = edge_head_u_shift20;

// generate spans
	for (edge=EDGE_PTR(edge_head.next) ; edge != &edge_tail; edge=EDGE_PTR(edge->next))	// QuakeOG
	{			
		QOG_BM (QOG_BM_SCANEDGES, 1);	// QuakeOG
		QOG_AQ (AQ_SCANEDGES, 1);
		if (edge->surfs[0])
		{
		// it has a left surface, so a surface is going away for this span
			surf = &surfaces[edge->surfs[0]];

			R_TrailingEdge (surf, edge);

			if (!edge->surfs[1])
				continue;
		}

		R_LeadingEdge (edge);
	}

	R_CleanupSpan ();
}
#endif	// QOG_SCAN_FAST

#endif	// !id386


/*
==============
R_GenerateSpansBackward
==============
*/
void R_GenerateSpansBackward (void)
{
	edge_t			*edge;

	r_bmodelactive = 0;

// clear active surfaces to just the background surface
	surfaces[1].next = surfaces[1].prev = SURF_REF(&surfaces[1]);	// QuakeOG
	surfaces[1].last_u = edge_head_u_shift20;

// generate spans
	for (edge=EDGE_PTR(edge_head.next) ; edge != &edge_tail; edge=EDGE_PTR(edge->next))	// QuakeOG
	{			
		QOG_BM (QOG_BM_SCANEDGES, 1);	// QuakeOG
		if (edge->surfs[0])
			R_TrailingEdge (&surfaces[edge->surfs[0]], edge);

		if (edge->surfs[1])
			R_LeadingEdgeBackwards (edge);
	}

	R_CleanupSpan ();
}


#ifdef QOG_EDGES_IN_ZBUFFER
/*
==============
QuakeOG, R_ScanEdges with the edge table in the z-buffer

The edge table sits at the end of the z-buffer, and a span writes z only in
its own scan line's row (d_pzbuffer[v * d_zwidth + u]). Spans are made top
to bottom, so the spans of rows above the edge table can be drawn whenever
the span buffer fills, as id does, while those of rows that still hold
needed edges have to wait. The buffer is flushed.
 - when it is full and every span in it lies above the edges (id's flush)
 - before the first scan line whose row reaches the edges, so that the
   buffer is empty when the waiting starts
 - when it is full while spans wait. First the edges no longer needed (their
   last scan line is done) are dropped and the rest moved to the end of the
   z-buffer (R_QogMoveEdges). If every waiting span then lies above them,
   the buffer is flushed and the scan goes on.
 - if not, the edges still needed are moved out of the z-buffer, to auxedges
   or a table on the stack of the old size (R_QogEdgesToStack), and the scan
   goes on with id's flushing. This is the exact fallback.
 - after the last scan line, when no edge is needed any more
Each pixel is drawn once, by one span, whichever flush draws it, so the
picture is the same as id's. The surfaces and spans cannot live in the
z-buffer, the last flush writes every z row while it still reads them.
==============
*/
#if defined(__GNUC__)
#define QOG_NOINLINE	__attribute__((noinline))
#define QOG_ALLOCA(n)	__builtin_alloca (n)
#elif defined(_MSC_VER)
#include <malloc.h>
#define QOG_NOINLINE	__declspec(noinline)
#define QOG_ALLOCA(n)	_alloca (n)
#else
#error "QOG_EDGES_IN_ZBUFFER needs alloca"
#endif

#ifdef QOG_ZEDGE_STACK_GUARD
// QuakeOG, the board reads none of these PC measurements, and its
// display has no static RAM to spare, so R_QogFlushNoZ's three ints take
// their place there
int		r_qog_zstack;
#define QOG_ZMEASURE(x)	((void)0)
#else
int		r_qog_zforced, r_qog_zcompacts, r_qog_zstack, r_qog_peakheldspans;	// measurements
#define QOG_ZMEASURE(x)	(x)
#endif
#ifdef QOG_CL_PEAKS
int		r_qog_zstackedges;	// QuakeOG, measurement, R_QogEdgesToStack
#endif

static byte		*r_qog_edgeend;		// end of the edge table
static espan_t	*r_qog_basespan;	// the span buffer
static int		r_qog_framespans;	// measurement only

#if defined(QOG_ZEDGE_STACK_GUARD) || defined(QOG_ZEDGE_FORCE_NOZ) || defined(QOG_ZEDGE_COMPARE)
// QuakeOG, the graceful end of the edge fallback, see R_QogFlushNoZ
#define QOG_ZEDGE_NOZ
#ifdef QOG_ZEDGE_COMPARE
int				r_qog_zforcenoz;	// PC test, V_RenderView draws a fallback view both ways
#endif
int				r_qog_znoz;			// flushes without z so far
#ifndef QOG_ZEDGE_STACK_GUARD
int				r_qog_znozrows;		// PC measurement, their rows
#endif
static int		r_qog_noztop, r_qog_nozend;	// rows top to end - 1 drawn without z in this view, none if end is 0
#endif

#define EDGE_LIVE	0xFFFE		// marks in edge_t.owner, see R_QogMoveEdges
#define EDGE_DEAD	0xFFFF
#define NEWREF(r)	((r) == EDGE_NONE ? EDGE_NONE : EDGE_PTR(r)->owner)
// end of the z bytes that the spans of scan lines up to iv write
#define ZROWS_END(iv)	((byte *)(d_pzbuffer + ((iv) + 1) * (int)d_zwidth))

/*
==============
R_QogMarkEdges

QuakeOG, marks the edges needed after scan line iv, the active list from
edge_head to edge_sentinel, and the lists of later scan lines. The owner
field, which only R_RenderFace reads, holds the mark. Returns their number.
==============
*/
static int R_QogMarkEdges (int iv, int bottom)
{
	edge_t		*e;
	edgeref_t	r;
	int			w, count;

	for (e=r_edgesentinels ; e<(edge_t *)r_qog_edgeend ; e++)
		e->owner = EDGE_DEAD;
	count = 0;
	for (r = EDGE_REF(&edge_head) ; ; r = EDGE_PTR(r)->next)
	{
		EDGE_PTR(r)->owner = EDGE_LIVE;
		count++;
		if (r == EDGE_REF(&edge_sentinel))
			break;
	}
	for (w=iv+1 ; w<=bottom ; w++)
	{
		for (r = newedges[w] ; r != EDGE_NONE ; r = EDGE_PTR(r)->next)
		{
			EDGE_PTR(r)->owner = EDGE_LIVE;
			count++;
		}
	}
	return count;
}

/*
==============
R_QogMoveEdges

QuakeOG, moves the edges R_QogMarkEdges marked, in their order, so that
they end at dstend, and rewrites every link to them for the new r_edgebase
newbase. The others are dropped. owner holds the new offset meanwhile.
==============
*/
static void R_QogMoveEdges (int iv, int bottom, byte *dstend, byte *newbase)
{
	edge_t		*e, *first, *last, *newsentinels;
	byte		*dst;
	edgeref_t	r, next;
	int			w;

	first = r_edgesentinels;
	last = (edge_t *)r_qog_edgeend;

// their new offsets, from the end down
	dst = dstend;
	for (e=last-1 ; e>=first ; e--)
	{
		if (e->owner == EDGE_LIVE)
		{
			dst -= sizeof(edge_t);
			e->owner = (unsigned short)(dst - newbase);
		}
	}

// the links, each read before it is rewritten. The sentinels' unused links
// are EDGE_NONE (R_EdgeDrawing clears them), and the edges of later scan
// lines have no prev yet (R_InsertNewEdges sets it).
	for (r = EDGE_REF(&edge_head) ; ; r = next)
	{
		e = EDGE_PTR(r);
		next = e->next;
		e->next = NEWREF(next);
		e->prev = NEWREF(e->prev);
		e->nextremove = NEWREF(e->nextremove);
		if (e == &edge_sentinel)
			break;
	}
	for (w=iv+1 ; w<=bottom ; w++)
	{
		for (r = newedges[w] ; r != EDGE_NONE ; r = next)
		{
			e = EDGE_PTR(r);
			next = e->next;
			e->next = NEWREF(next);
			e->nextremove = NEWREF(e->nextremove);
		}
		newedges[w] = NEWREF(newedges[w]);
		removeedges[w] = NEWREF(removeedges[w]);
	}

// move them, from the end down, an edge never moves down, so none
// overwrites one not yet moved
	newsentinels = (edge_t *)(newbase + r_edgesentinels->owner);
	for (e=last-1 ; e>=first ; e--)
	{
		if (e->owner != EDGE_DEAD)
			memmove (newbase + e->owner, e, sizeof(edge_t));
	}
	r_edgesentinels = newsentinels;
	r_edgebase = newbase;
	r_qog_edgeend = dstend;
}

#ifdef QOG_VID_ROWRACE
/*
==============
R_QogRowFree

QuakeOG, while r_qog_rowrace (r_main.c R_EdgeDrawing), the last
frame's scanout may still be sending the view's rows. Spans drawn now lie
on scan lines down to iv, so they wait until the scanout has read the
frame's rows down to iv (in a Turbo view its packed rows, screenwidth bytes
each). Called before each flush and before surfaces go to core 1.
==============
*/
int			r_qog_rowrace;
static int	r_qog_rowfreed;		// rows the scanout has released so far

static void R_QogRowFree (int iv)
{
	if (!r_qog_rowrace || iv < r_qog_rowfreed)
		return;
	VID_QogScanFree (d_viewbuffer, d_viewbuffer + (iv + 1) * screenwidth);
	r_qog_rowfreed = iv + 1;
}
#else
#define R_QogRowFree(iv)	((void)0)
#endif

/*
==============
R_QogFlushSpans

QuakeOG, id's mid-scan flush
==============
*/
static void R_QogFlushSpans (void)
{
	surf_t	*s;

	R_QogRowFree (current_iv);	// QuakeOG, the spans lie on lines down to this one

	VID_UnlockBuffer ();
	S_ExtraUpdate ();	// don't let sound get messed up if going slow
	VID_LockBuffer ();

	if (r_drawculledpolys)
	{
		R_DrawCulledPolys ();
	}
	else
	{
		D_DrawSurfaces ();
	}

// clear the surface span pointers
	for (s = &surfaces[1] ; s<surface_p ; s++)
		s->spans = SPAN_NONE;

	r_qog_framespans += span_p - r_qog_basespan;	// measurement only
	if (span_p - r_qog_basespan > r_qog_peakflushspans)
		r_qog_peakflushspans = span_p - r_qog_basespan;
	span_p = r_qog_basespan;
}

#ifdef QOG_ZEDGE_NOZ
/*
==============
R_QogFlushNoZ

QuakeOG, the fallback when the edges still needed cannot move
to the stack either (the board's stack, QOG_ZEDGE_STACK_GUARD). It used to
stop the game with an error. Now the waiting spans are drawn at once, with
every pixel as id draws it, but their z goes to the frame buffer's last
view row (bottom), which no span of this frame has drawn yet and the last
flush draws over, so the edges in the z-buffer stay intact. Their rows are
noted (one range from the first to the last such row of the view, no static
RAM to spare for more), and R_ScanEdges sets the z of those rows to the
farthest value once no edge is needed. So the world is drawn exactly, and only the depth test
of models, sprites and particles in those rows of this one frame changes.
they show in front of the world there. Costs no stack.
==============
*/
static void R_QogFlushNoZ (int iv, int bottom)
{
	d_zval_t	*zbuffer;
	unsigned	zwidth;
	int			v;

#ifdef QOG_DRAW_CORE1
	D_QogPipeSync ();	// QuakeOG, core 1 done before the z-buffer moves
#endif
	r_qog_znoz++;
	v = r_qog_basespan->v;		// the buffer's spans are of scan lines v to iv
#ifndef QOG_ZEDGE_STACK_GUARD
	r_qog_znozrows += iv + 1 - v;
#endif
	if (!r_qog_nozend)
		r_qog_noztop = v;
	r_qog_nozend = iv + 1;

	zbuffer = d_pzbuffer;
	zwidth = d_zwidth;
	R_QogRowFree (bottom);	// QuakeOG, the z goes to the view's last row
	// every span's z lands in one row, d_pzbuffer + 0 * v + u, u < screenwidth
	d_pzbuffer = (d_zval_t *)(d_viewbuffer + bottom * screenwidth);
	d_zwidth = 0;
	R_QogFlushSpans ();
	d_pzbuffer = zbuffer;
	d_zwidth = zwidth;
}

/*
==============
R_QogClearNoZ

QuakeOG, the farthest z in the rows R_QogFlushNoZ drew, once
the edge table in the z-buffer is no longer needed. Rows inside the range
that a normal flush drew lose their z as well, only in this view.
==============
*/
static void R_QogClearNoZ (void)
{
	int		v;

	for (v = r_qog_noztop ; v < r_qog_nozend ; v++)
		memset (d_pzbuffer + v * (int)d_zwidth + r_refdef.vrect.x, 0,
			r_refdef.vrect.width * sizeof(d_zval_t));
	r_qog_nozend = 0;
}
#endif

static qboolean R_QogEdgesToStack (int iv, int bottom);

#ifdef QOG_DRAW_CORE1
/*
==============
QuakeOG, the scan's part of the two-core pipeline (d_edge.c)

Before the first scan line each world surface gets the last scan line of
its edges (an edge's last line is the line whose removeedges list holds
it). At the end of that line no later line can add a span to the surface,
so its span list is the one the flush would draw, and it goes to core 1
(D_QogPipePush), if the z rows of its spans lie above the edge table.
On the board's 32-byte surfaces the line is kept in entref, which only a
brush entity's surface uses (SURF_ENTITY), else in a table of its own.
==============
*/
#if MAXHEIGHT > 256
#error "QOG_DRAW_CORE1 keeps a scan line in a byte"
#endif
#ifdef QOG_SURF_T_TINY
#define SURF_LASTROW(s)		((s)->entref)
#define SURF_LASTROW_OK(si)	1
#else
static byte	r_qog_lastrow[NUMSTACKSURFACES + 2];
#define SURF_LASTROW(s)		(r_qog_lastrow[(s) - surfaces])
#define SURF_LASTROW_OK(si)	((si) < NUMSTACKSURFACES + 2)
#endif

static void R_QogPipeLastRows (int top, int bottom)
{
	edgeref_t	r;
	edge_t		*e;
	unsigned	si;
	int			w, k;

	for (w = top ; w <= bottom ; w++)
	{
		for (r = removeedges[w] ; r != EDGE_NONE ; r = e->nextremove)
		{
			e = EDGE_PTR(r);
			for (k = 0 ; k < 2 ; k++)
			{
				si = e->surfs[k];
				if (si > 1 && SURF_LASTROW_OK (si) && !surfaces[si].insubmodel)
					SURF_LASTROW (&surfaces[si]) = (byte)w;
			}
		}
	}
}

// the lit world surfaces whose last scan line iv was, to core 1
static void R_QogPipeRow (int iv)
{
	edgeref_t	r;
	edge_t		*e;
	surf_t		*s;
	unsigned	si;
	int			k;

	for (r = removeedges[iv] ; r != EDGE_NONE ; r = e->nextremove)
	{
		e = EDGE_PTR(r);
		for (k = 0 ; k < 2 ; k++)
		{
			si = e->surfs[k];
			if (si <= 1 || !SURF_LASTROW_OK (si))
				continue;
			s = &surfaces[si];
			if (s->spans != SPAN_NONE && D_QOG_C1OK (s) && SURF_LASTROW (s) == iv)
			{
				R_QogRowFree (iv);	// QuakeOG, core 1 draws its lines down to iv
				D_QogPipePush (s);
			}
		}
	}
}
#endif

/*
==============
R_QogScanRows

QuakeOG, scan lines iv to bottom, see above. edgesinz is true while the
edge table is in the z-buffer.
==============
*/
static void R_QogScanRows (int iv, int bottom, qboolean edgesinz)
{
	for ( ; iv<bottom ; iv++)
	{
		current_iv = iv;
		fv = (float)iv;

	// mark that the head (background start) span is pre-included
		surfaces[1].spanstate = 1;

		if (newedges[iv])
		{
			R_InsertNewEdges (EDGE_PTR(newedges[iv]), EDGE_PTR(edge_head.next));
		}

		(*pdrawfunc) ();

	// the edges whose last scan line this was go before the flush (id
	// flushed first, neither touches what the other uses)
		if (removeedges[iv])
			R_RemoveEdges (EDGE_PTR(removeedges[iv]));

#ifdef QOG_DRAW_CORE1
	// QuakeOG, the surfaces that ended here, to core 1, when
	// their z rows are free of edges
		if (d_qogpipe_on && removeedges[iv] && (!edgesinz || ZROWS_END(iv) <= (byte *)r_edgesentinels))
			R_QogPipeRow (iv);
#endif

		if (!edgesinz)
		{
			if (span_p >= max_span_p)
				R_QogFlushSpans ();
		}
		else if (ZROWS_END(iv) <= (byte *)r_edgesentinels)
		{
		// every span so far lies above the edges, flush when full, and
		// before a scan line whose row reaches the edges
			if (span_p >= max_span_p)
			{
				R_QogFlushSpans ();
			}
			else if (ZROWS_END(iv + 1) > (byte *)r_edgesentinels && span_p > r_qog_basespan)
			{
				QOG_ZMEASURE (r_qog_zforced++);
				R_QogFlushSpans ();
			}
		}
		else
		{
			QOG_ZMEASURE (span_p - r_qog_basespan > r_qog_peakheldspans
				? r_qog_peakheldspans = span_p - r_qog_basespan : 0);
			if (span_p >= max_span_p)
			{
			// full while spans wait, drop the edges no longer needed
				QOG_ZMEASURE (r_qog_zcompacts++);
				R_QogMarkEdges (iv, bottom);
				R_QogMoveEdges (iv, bottom, r_qog_edgeend, r_edgebase);
				if (ZROWS_END(iv) > (byte *)r_edgesentinels)
				{
					if (R_QogEdgesToStack (iv, bottom))
						return;		// it scanned the rest
				}
				else
					R_QogFlushSpans ();
			}
		}

		if (edge_head.next != EDGE_REF(&edge_tail))
			R_StepActiveU (EDGE_PTR(edge_head.next));
	}

// do the last scan (no need to step or sort or remove on the last scan)

	current_iv = iv;
	fv = (float)iv;

// mark that the head (background start) span is pre-included
	surfaces[1].spanstate = 1;

	if (newedges[iv])
		R_InsertNewEdges (EDGE_PTR(newedges[iv]), EDGE_PTR(edge_head.next));

	(*pdrawfunc) ();

// measurement only
	QOG_ZMEASURE (edgesinz && span_p - r_qog_basespan > r_qog_peakheldspans
		? r_qog_peakheldspans = span_p - r_qog_basespan : 0);
	r_qog_framespans += span_p - r_qog_basespan;
	if (span_p - r_qog_basespan > r_qog_peakflushspans)
		r_qog_peakflushspans = span_p - r_qog_basespan;
	if (r_qog_framespans > r_qog_peakframespans)
		r_qog_peakframespans = r_qog_framespans;

// draw whatever's left in the span list, no edge is needed any more
	R_QogRowFree (iv);	// QuakeOG
	if (r_drawculledpolys)
		R_DrawCulledPolys ();
	else
		D_DrawSurfaces ();
}

/*
==============
R_QogEdgesToStack

QuakeOG, the span buffer is full of spans that wait for edges still needed.
Move those edges out of the z-buffer, to auxedges or a table on the stack
just large enough (at most the old table's size), and go on with id's
flushing. Not inlined, so the table is on the stack only when this runs.
Returns true when it scanned the rest of the view. Earlier work, with
QOG_ZEDGE_STACK_GUARD, when the stack cannot hold the table, it draws the
waiting spans without z instead (R_QogFlushNoZ) and returns false, and the
scan goes on with the edges in the z-buffer.
==============
*/
static QOG_NOINLINE qboolean R_QogEdgesToStack (int iv, int bottom)
{
	edge_t	*dst;
	int		n;

	r_qog_zstack++;
	n = R_QogMarkEdges (iv, bottom);
#ifdef QOG_CL_PEAKS
	{	// QuakeOG, measurement only, the most edges a fallback moved
		extern int	r_qog_zstackedges;
		if (n > r_qog_zstackedges)
			r_qog_zstackedges = n;
	}
#endif
#ifdef QOG_ZEDGE_STACK_GUARD
	// QuakeOG, a board stack is not sized for this table (the
	// port's qog_stack_bottom is its lowest byte). Earlier work, when it cannot
	// hold it, draw the waiting spans without z rather than stop the game
	if (!auxedges)
	{
		extern byte	*qog_stack_bottom;
		byte		here;
		int			left = &here - qog_stack_bottom;

		if (n * (int)sizeof(edge_t) + QOG_ZEDGE_STACK_GUARD > left)
		{
			R_QogFlushNoZ (iv, bottom);
			return false;
		}
	}
#endif
#if defined(QOG_ZEDGE_FORCE_NOZ) || defined(QOG_ZEDGE_COMPARE)
	// PC test builds only, every fallback takes the board's graceful path
#ifdef QOG_ZEDGE_COMPARE
	if (!auxedges && r_qog_zforcenoz)
#else
	if (!auxedges)
#endif
	{
		R_QogFlushNoZ (iv, bottom);
		return false;
	}
#endif
	if (auxedges)
		dst = auxedges;
	else
		dst = (edge_t *)QOG_ALLOCA (n * sizeof(edge_t));
	R_QogMoveEdges (iv, bottom, (byte *)(dst + n), (byte *)dst - sizeof(edge_t));
	R_QogFlushSpans ();

	if (edge_head.next != EDGE_REF(&edge_tail))
		R_StepActiveU (EDGE_PTR(edge_head.next));

	R_QogScanRows (iv + 1, bottom, false);
	return true;
}

/*
==============
R_ScanEdges

QuakeOG, QOG_EDGES_IN_ZBUFFER version, see above
==============
*/
void R_ScanEdges (void)
{
	int		numspans, bytes;
	byte	*dst;

#ifdef QOG_VID_ROWRACE
	// QuakeOG, the z-buffer region (z, the edge table moved
	// below, scratch) and the view's z-buffer (in a Turbo view the second
	// half of its rows, which a packed scanout never reads) wait here. The
	// spans write only rows down to the scan line reached (R_QogRowFree).
	if (r_qog_rowrace)
	{
		VID_QogScanFree (r_ztemp_base, r_ztemp_base + r_ztemp_bytes);
		VID_QogScanFree (d_pzbuffer, d_pzbuffer + (int)d_zwidth * r_refdef.vrectbottom);
	}
#endif

// move the used part of the edge table (the sentinels and the frame's
// edges, linked by offsets from r_edgebase) to the end of the z-buffer.
// The edge cache only looked at it while the edges were made.
	bytes = (byte *)edge_p - (byte *)r_edgesentinels;
	r_qog_edgeend = r_ztemp_base + r_ztemp_bytes;
	dst = r_qog_edgeend - bytes;
	if (dst != (byte *)r_edgesentinels)
	{
		memmove (dst, r_edgesentinels, bytes);
		r_edgebase += dst - (byte *)r_edgesentinels;
		r_edgesentinels = (edge_t *)dst;
	}
	r_edges = r_edgesentinels + EDGE_SENTINELS;
	edge_p = (edge_t *)r_qog_edgeend;

// the spans fill the surface table from the first unused entry on
// (r_local.h). 16-bit links reach (65536 / 8) - 1 spans.
	r_qog_basespan = (espan_t *)surface_p;
	numspans = (r_spanpool_end - (byte *)r_qog_basespan) / sizeof(espan_t);
	if (numspans > 0x10000 / sizeof(espan_t) - 1)
		numspans = 0x10000 / sizeof(espan_t) - 1;
#ifdef QOG_ZEDGE_TEST_SPANS
	// less room, only to test the edge moves and the stack fallback
	if (numspans > QOG_ZEDGE_TEST_SPANS)
		numspans = QOG_ZEDGE_TEST_SPANS;
#endif
	if (numspans < r_refdef.vrect.width + 1)
		Sys_Error ("R_ScanEdges: room for %d spans, a scan line needs %d (vrect %d %d %d %d, refdef %p, sentinels %p, edge_p %p, ztemp %p+%d, surfaces %p, surface_p %p, spanend %p)",
			numspans, r_refdef.vrect.width + 1,
			r_refdef.vrect.x, r_refdef.vrect.y, r_refdef.vrect.width, r_refdef.vrect.height,
			(void *)&r_refdef, (void *)r_edgesentinels, (void *)edge_p,
			(void *)r_ztemp_base, r_ztemp_bytes, (void *)surfaces, (void *)surface_p, (void *)r_spanpool_end);
	if (numspans > r_qog_peakspanroom)
		r_qog_peakspanroom = numspans;
	if (!r_qog_minspanroom || numspans < r_qog_minspanroom)
		r_qog_minspanroom = numspans;
	r_spanbase = (byte *)r_qog_basespan - sizeof(espan_t);
	max_span_p = &r_qog_basespan[numspans - r_refdef.vrect.width];
	span_p = r_qog_basespan;
	r_qog_framespans = 0;
#ifdef QOG_VID_ROWRACE
	r_qog_rowfreed = 0;		// QuakeOG
#endif

// clear active edges to just the background edges around the whole screen
	edge_head.u = r_refdef.vrect.x << 20;
	edge_head_u_shift20 = edge_head.u >> 20;
	edge_head.u_step = 0;
	edge_head.prev = EDGE_NONE;
	edge_head.next = EDGE_REF(&edge_tail);
	edge_head.surfs[0] = 0;
	edge_head.surfs[1] = 1;

	edge_tail.u = (r_refdef.vrectright << 20) + 0xFFFFF;
	edge_tail_u_shift20 = edge_tail.u >> 20;
	edge_tail.u_step = 0;
	edge_tail.prev = EDGE_REF(&edge_head);
	edge_tail.next = EDGE_REF(&edge_aftertail);
	edge_tail.surfs[0] = 1;
	edge_tail.surfs[1] = 0;

	edge_aftertail.u = -1;		// force a move
	edge_aftertail.u_step = 0;
	edge_aftertail.next = EDGE_REF(&edge_sentinel);
	edge_aftertail.prev = EDGE_REF(&edge_tail);

	edge_sentinel.u = 2000 << 24;		// make sure nothing sorts past this
	edge_sentinel.prev = EDGE_REF(&edge_aftertail);

#ifdef QOG_DRAW_CORE1
	// QuakeOG, core 1 draws surfaces while the scan goes on
	D_QogPipeBegin (!r_drawculledpolys);
	if (d_qogpipe_on)
		R_QogPipeLastRows (r_refdef.vrect.y, r_refdef.vrectbottom - 1);
#endif
	R_QogScanRows (r_refdef.vrect.y, r_refdef.vrectbottom - 1, true);
#ifdef QOG_DRAW_CORE1
	D_QogPipeEnd ();	// the last flush drew every surface, core 1's job returns
#endif
#ifdef QOG_ZEDGE_NOZ
	R_QogClearNoZ ();	// Earlier work, every edge is done with
#endif
}

#else	// !QOG_EDGES_IN_ZBUFFER

/*
==============
R_ScanEdges

Input: 
newedges[] array
	this has links to edges, which have links to surfaces

Output:
Each surface has a linked list of its visible spans
==============
*/
void R_ScanEdges (void)
{
	int		iv, bottom;
	int		framespans = 0;	// QuakeOG, measurement only
#if defined(QOG_SPANS_IN_EDGES)
	// QuakeOG, the spans fill the edge table from the first unused entry on
	// (r_local.h). Every edge of the frame is emitted by now, and the edge
	// cache only looks at entries below edge_p. 16-bit links reach
	// (65536 / 8) - 1 spans.
	espan_t	*basespan_p;
	surf_t	*s;
	int		numspans;

	basespan_p = (espan_t *)edge_p;
	numspans = (r_spanpool_end - (byte *)basespan_p) / sizeof(espan_t);
	if (numspans > 0x10000 / sizeof(espan_t) - 1)
		numspans = 0x10000 / sizeof(espan_t) - 1;
	if (numspans < r_refdef.vrect.width + 1)
		Sys_Error ("R_ScanEdges: room for %d spans, a scan line needs %d (vrect %d %d %d %d, refdef %p, sentinels %p, edge_p %p, ztemp %p+%d, surfaces %p, surface_p %p, spanend %p)",
			numspans, r_refdef.vrect.width + 1,
			r_refdef.vrect.x, r_refdef.vrect.y, r_refdef.vrect.width, r_refdef.vrect.height,
			(void *)&r_refdef, (void *)r_edgesentinels, (void *)edge_p,
			(void *)r_ztemp_base, r_ztemp_bytes, (void *)surfaces, (void *)surface_p, (void *)r_spanpool_end);
	if (numspans > r_qog_peakspanroom)
		r_qog_peakspanroom = numspans;
	if (!r_qog_minspanroom || numspans < r_qog_minspanroom)
		r_qog_minspanroom = numspans;
	r_spanbase = (byte *)basespan_p - sizeof(espan_t);
#define MAXSPANS_THISFRAME	numspans
#elif defined(QOG_ESPAN_T_PACKED)
	// QuakeOG, 8-byte spans gain nothing from cache line alignment, and the
	// 16-bit links reach (65536 / 8) - 1 spans (r_shared.h)
	espan_t	basespans[MAXSPANS];
	espan_t	*basespan_p;
	surf_t	*s;
	typedef char span_links_fit[((MAXSPANS + 1) * sizeof(espan_t) <= 0x10000) ? 1 : -1];

	basespan_p = basespans;
	r_spanbase = (byte *)basespans - sizeof(espan_t);
#else
	byte	basespans[MAXSPANS*sizeof(espan_t)+CACHE_SIZE];
	espan_t	*basespan_p;
	surf_t	*s;

	basespan_p = (espan_t *)
			((long)(basespans + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
#endif
#ifdef MAXSPANS_THISFRAME
	max_span_p = &basespan_p[MAXSPANS_THISFRAME - r_refdef.vrect.width];
#undef MAXSPANS_THISFRAME
#else
	max_span_p = &basespan_p[MAXSPANS - r_refdef.vrect.width];
#endif

	span_p = basespan_p;

// clear active edges to just the background edges around the whole screen
// FIXME: most of this only needs to be set up once
	edge_head.u = r_refdef.vrect.x << 20;
	edge_head_u_shift20 = edge_head.u >> 20;
	edge_head.u_step = 0;
	edge_head.prev = EDGE_NONE;	// QuakeOG, EDGE_NONE/EDGE_REF
	edge_head.next = EDGE_REF(&edge_tail);
	edge_head.surfs[0] = 0;
	edge_head.surfs[1] = 1;
	
	edge_tail.u = (r_refdef.vrectright << 20) + 0xFFFFF;
	edge_tail_u_shift20 = edge_tail.u >> 20;
	edge_tail.u_step = 0;
	edge_tail.prev = EDGE_REF(&edge_head);
	edge_tail.next = EDGE_REF(&edge_aftertail);
	edge_tail.surfs[0] = 1;
	edge_tail.surfs[1] = 0;
	
	edge_aftertail.u = -1;		// force a move
	edge_aftertail.u_step = 0;
	edge_aftertail.next = EDGE_REF(&edge_sentinel);
	edge_aftertail.prev = EDGE_REF(&edge_tail);

// FIXME: do we need this now that we clamp x in r_draw.c?
	edge_sentinel.u = 2000 << 24;		// make sure nothing sorts past this
	edge_sentinel.prev = EDGE_REF(&edge_aftertail);

//	
// process all scan lines
//
	bottom = r_refdef.vrectbottom - 1;

	for (iv=r_refdef.vrect.y ; iv<bottom ; iv++)
	{
		current_iv = iv;
		fv = (float)iv;

	// mark that the head (background start) span is pre-included
		surfaces[1].spanstate = 1;

		if (newedges[iv])
		{
			R_InsertNewEdges (EDGE_PTR(newedges[iv]), EDGE_PTR(edge_head.next));	// QuakeOG
		}

		(*pdrawfunc) ();

	// flush the span list if we can't be sure we have enough spans left for
	// the next scan
		if (span_p >= max_span_p)
		{
			VID_UnlockBuffer ();
			S_ExtraUpdate ();	// don't let sound get messed up if going slow
			VID_LockBuffer ();
		
			if (r_drawculledpolys)
			{
				R_DrawCulledPolys ();
			}
			else
			{
				D_DrawSurfaces ();
			}

		// clear the surface span pointers
			for (s = &surfaces[1] ; s<surface_p ; s++)
				s->spans = SPAN_NONE;	// QuakeOG

			framespans += span_p - basespan_p;	// QuakeOG, measurement only
			if (span_p - basespan_p > r_qog_peakflushspans)
				r_qog_peakflushspans = span_p - basespan_p;
			span_p = basespan_p;
		}

		if (removeedges[iv])
			R_RemoveEdges (EDGE_PTR(removeedges[iv]));	// QuakeOG

		if (edge_head.next != EDGE_REF(&edge_tail))
			R_StepActiveU (EDGE_PTR(edge_head.next));
	}

// do the last scan (no need to step or sort or remove on the last scan)

	current_iv = iv;
	fv = (float)iv;

// mark that the head (background start) span is pre-included
	surfaces[1].spanstate = 1;

	if (newedges[iv])
		R_InsertNewEdges (EDGE_PTR(newedges[iv]), EDGE_PTR(edge_head.next));	// QuakeOG

	(*pdrawfunc) ();

// QuakeOG, measurement only
	framespans += span_p - basespan_p;
	if (span_p - basespan_p > r_qog_peakflushspans)
		r_qog_peakflushspans = span_p - basespan_p;
	if (framespans > r_qog_peakframespans)
		r_qog_peakframespans = framespans;

// draw whatever's left in the span list
	if (r_drawculledpolys)
		R_DrawCulledPolys ();
	else
		D_DrawSurfaces ();
}

#endif	// !QOG_EDGES_IN_ZBUFFER

