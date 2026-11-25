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
// d_polyset.c: routines for drawing sets of polygons sharing the same
// texture (used for Alias models)

#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"
#include "r_qogalias.h"	// QuakeOG (Agent AI)

// TODO: put in span spilling to shrink list size
// !!! if this is changed, it must be changed in d_polysa.s too !!!
#define DPS_MAXSPANS			MAXHEIGHT+1	
									// 1 extra for spanpackage that marks end

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct {
	void			*pdest;
	d_zval_t		*pz;
	int				count;
	byte			*ptex;
	int				sfrac, tfrac, light, zi;
} spanpackage_t;

typedef struct {
	int		isflattop;
	int		numleftedges;
	int		*pleftedgevert0;
	int		*pleftedgevert1;
	int		*pleftedgevert2;
	int		numrightedges;
	int		*prightedgevert0;
	int		*prightedgevert1;
	int		*prightedgevert2;
} edgetable;

int	r_p0[6], r_p1[6], r_p2[6];

byte		*d_pcolormap;
#ifdef QOG_TRANSLATE_INLINE
// QuakeOG (Agent S): while an entity in player colours is drawn, its skin
// texels go through this 256-byte map before the colour map (r_alias.c),
// in place of id's 16 KB translated colour map
byte		*d_atranslate;
#define D_ATEX(t)	(d_atranslate ? d_atranslate[t] : (t))
#else
#define D_ATEX(t)	(t)
#endif

int			d_aflatcolor;
int			d_xdenom;

// QuakeOG: the triangle edge table is only read, so with QOG_CONST_RTABLES
// it stays in flash
QOG_RTABLE_CONST edgetable	*pedgetable;

QOG_RTABLE_CONST edgetable	edgetables[12] = {
	{0, 1, r_p0, r_p2, NULL, 2, r_p0, r_p1, r_p2 },
	{0, 2, r_p1, r_p0, r_p2,   1, r_p1, r_p2, NULL},
	{1, 1, r_p0, r_p2, NULL, 1, r_p1, r_p2, NULL},
	{0, 1, r_p1, r_p0, NULL, 2, r_p1, r_p2, r_p0 },
	{0, 2, r_p0, r_p2, r_p1,   1, r_p0, r_p1, NULL},
	{0, 1, r_p2, r_p1, NULL, 1, r_p2, r_p0, NULL},
	{0, 1, r_p2, r_p1, NULL, 2, r_p2, r_p0, r_p1 },
	{0, 2, r_p2, r_p1, r_p0,   1, r_p2, r_p0, NULL},
	{0, 1, r_p1, r_p0, NULL, 1, r_p1, r_p2, NULL},
	{1, 1, r_p2, r_p1, NULL, 1, r_p0, r_p1, NULL},
	{1, 1, r_p1, r_p0, NULL, 1, r_p2, r_p0, NULL},
	{0, 1, r_p0, r_p2, NULL, 1, r_p0, r_p1, NULL},
};

// FIXME: some of these can become statics
int				a_sstepxfrac, a_tstepxfrac, r_lstepx, a_ststepxwhole;
int				r_sstepx, r_tstepx, r_lstepy, r_sstepy, r_tstepy;
int				r_zistepx, r_zistepy;
int				d_aspancount, d_countextrastep;

spanpackage_t			*a_spans;
spanpackage_t			*d_pedgespanpackage;
static int				ystart;
byte					*d_pdest, *d_ptex;
d_zval_t				*d_pz;
int						d_sfrac, d_tfrac, d_light, d_zi;
int						d_ptexextrastep, d_sfracextrastep;
int						d_tfracextrastep, d_lightextrastep, d_pdestextrastep;
int						d_lightbasestep, d_pdestbasestep, d_ptexbasestep;
int						d_sfracbasestep, d_tfracbasestep;
int						d_ziextrastep, d_zibasestep;
int						d_pzextrastep, d_pzbasestep;

typedef struct {
	int		quotient;
	int		remainder;
} adivtab_t;

static adivtab_t	adivtab[32*32] = {
#include "adivtab.h"
};

#ifdef QOG_NO_ROWTABLES
// QuakeOG: skin rows are computed (one multiply) instead of read from the
// 480-entry skintable (1.9 KB of RAM). Same addresses.
#define D_SKINROW(t)	(skinstart + (t) * skinwidth)
#else
byte	*skintable[MAX_LBM_HEIGHT];
#define D_SKINROW(t)	skintable[t]
#endif
int		skinwidth;
byte	*skinstart;

void D_PolysetDrawSpans8 (spanpackage_t *pspanpackage);
void D_PolysetCalcGradients (int skinwidth);
void D_DrawSubdiv (void);
void D_DrawNonSubdiv (void);
void D_PolysetRecursiveTriangle (int *p1, int *p2, int *p3);
void D_PolysetSetEdgeTable (void);
void D_RasterizeAliasPolySmooth (void);
void D_PolysetScanLeftEdge (int height);

#if	!id386

/*
================
D_PolysetDraw
================
*/
void D_PolysetDraw (void)
{
	spanpackage_t	spans[DPS_MAXSPANS + 1 +
			((CACHE_SIZE - 1) / sizeof(spanpackage_t)) + 1];
						// one extra because of cache line pretouching

	a_spans = (spanpackage_t *)
			(((long)&spans[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));

	if (r_affinetridesc.drawtype)
	{
		D_DrawSubdiv ();
	}
	else
	{
		D_DrawNonSubdiv ();
	}
}


/*
================
D_PolysetDrawFinalVerts
================
*/
void D_PolysetDrawFinalVerts (finalvert_t *fv, int numverts)
{
	int		i, z;
	d_zval_t	*zbuf;

	for (i=0 ; i<numverts ; i++, fv++)
	{
	// valid triangle coordinates for filling can include the bottom and
	// right clip edges, due to the fill rule; these shouldn't be drawn
		if ((fv->v[0] < r_refdef.vrectright) &&
			(fv->v[1] < r_refdef.vrectbottom))
		{
			z = fv->v[5]>>16;
			z = D_ZQ(z);	// QuakeOG: QOG_ZBUFFER8
			zbuf = D_ZSPANTABLE(fv->v[1]) + fv->v[0];	// QuakeOG: QOG_NO_ROWTABLES
			if (z >= *zbuf)
			{
				int		pix;
				
				*zbuf = z;
				pix = D_SKINROW(fv->v[3]>>16)[fv->v[2]>>16];
				pix = ((byte *)acolormap)[D_ATEX (pix) + (fv->v[4] & 0xFF00) ];	// QuakeOG: D_ATEX
				d_viewbuffer[D_SCANTABLE(fv->v[1]) + fv->v[0]] = pix;
			}
		}
	}
}


/*
================
D_DrawSubdiv
================
*/
void D_DrawSubdiv (void)
{
	mtriangle_t		*ptri;
	finalvert_t		*pfv, *index0, *index1, *index2;
	int				i;
	int				lnumtriangles;

	pfv = r_affinetridesc.pfinalverts;
	ptri = r_affinetridesc.ptriangles;
	lnumtriangles = r_affinetridesc.numtriangles;

	for (i=0 ; i<lnumtriangles ; i++)
	{
		index0 = pfv + ptri[i].vertindex[0];
		index1 = pfv + ptri[i].vertindex[1];
		index2 = pfv + ptri[i].vertindex[2];

		QOG_AST(QAS_TRIS, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
		if (((index0->v[1]-index1->v[1]) *
			 (index0->v[0]-index2->v[0]) -
			 (index0->v[0]-index1->v[0]) * 
			 (index0->v[1]-index2->v[1])) >= 0)
		{
			QOG_AST(QAS_BACK, 1);
			continue;
		}
		QOG_AST(QAS_SUBTRI, 1);

		d_pcolormap = &((byte *)acolormap)[index0->v[4] & 0xFF00];

		if (ptri[i].facesfront)
		{
			D_PolysetRecursiveTriangle(index0->v, index1->v, index2->v);
		}
		else
		{
			int		s0, s1, s2;

			s0 = index0->v[2];
			s1 = index1->v[2];
			s2 = index2->v[2];

			if (index0->flags & ALIAS_ONSEAM)
				index0->v[2] += r_affinetridesc.seamfixupX16;
			if (index1->flags & ALIAS_ONSEAM)
				index1->v[2] += r_affinetridesc.seamfixupX16;
			if (index2->flags & ALIAS_ONSEAM)
				index2->v[2] += r_affinetridesc.seamfixupX16;

			D_PolysetRecursiveTriangle(index0->v, index1->v, index2->v);

			index0->v[2] = s0;
			index1->v[2] = s1;
			index2->v[2] = s2;
		}
	}
}


#if defined(QOG_ALIAS_TURBOTINY) && defined(QOG_RTURBO)
/*
================
D_QogTinyTriangle

QuakeOG (Agent AR): Turbo only (r_qog_turboview). A front-facing triangle
whose corners lie within 3 x 3 pixels (a distant model: 51 of the 85
triangles a Turbo view of timedemo demo3 rasterises with gradients cover 4
pixels or fewer, and 16 more cover none) is drawn without gradients, edge
tables or span packages, which cost about 2,000 cycles a triangle on the
board even when nothing is drawn. Each of the at most 9 pixels is tested
against the three edges (on an edge: drawn for top and left edges), and a
covered pixel takes the first corner's texel, light and depth with id's
depth test, as D_PolysetDrawFinalVerts draws a corner. Returns 0 when the
triangle is not that small.
================
*/
#if defined(_MSC_VER)
#define QOG_TINY_NOINLINE	__declspec(noinline)
#else
#define QOG_TINY_NOINLINE	__attribute__((noinline))	// its frame beside the rasterizer's, not under it
#endif
static QOG_TINY_NOINLINE int D_QogTinyTriangle (finalvert_t *a, finalvert_t *b, finalvert_t *c, int facesfront)
{
	int		x0 = a->v[0], y0 = a->v[1], x1 = b->v[0], y1 = b->v[1], x2 = c->v[0], y2 = c->v[1];
	int		minx = x0, maxx = x0, miny = y0, maxy = y0, x, y, s, z, col;
	int		e0x, e0y, e1x, e1y, e2x, e2y, t0, t1, t2;
	byte	*cmap, *row;

	if (x1 < minx) minx = x1; if (x1 > maxx) maxx = x1;
	if (x2 < minx) minx = x2; if (x2 > maxx) maxx = x2;
	if (y1 < miny) miny = y1; if (y1 > maxy) maxy = y1;
	if (y2 < miny) miny = y2; if (y2 > maxy) maxy = y2;
	if (maxx - minx > 2 || maxy - miny > 2)
		return 0;

	// edges in the triangle's winding (d_xdenom < 0 makes the third corner
	// positive for each edge): a pixel is inside when every edge function is
	// positive, or zero on a top or left edge
	e0x = x1 - x0; e0y = y1 - y0;
	e1x = x2 - x1; e1y = y2 - y1;
	e2x = x0 - x2; e2y = y0 - y2;
	t0 = (e0y < 0 || (e0y == 0 && e0x > 0)) ? 0 : 1;
	t1 = (e1y < 0 || (e1y == 0 && e1x > 0)) ? 0 : 1;
	t2 = (e2y < 0 || (e2y == 0 && e2x > 0)) ? 0 : 1;

	s = a->v[2];
	if (!facesfront && (a->flags & ALIAS_ONSEAM))
		s += r_affinetridesc.seamfixupX16;
	row = (byte *)r_affinetridesc.pskin + (a->v[3] >> 16) * r_affinetridesc.skinwidth;
	cmap = (byte *)acolormap + (a->v[4] & 0xFF00);
	col = cmap[D_ATEX (row[s >> 16])];
	z = a->v[5] >> 16;
	z = D_ZQ(z);
	for (y = miny ; y <= maxy ; y++)
	{
		if (y >= r_refdef.vrectbottom)
			break;
		for (x = minx ; x <= maxx ; x++)
		{
			d_zval_t	*zb;

			if (e0x * (y - y0) - e0y * (x - x0) < t0 ||
				e1x * (y - y1) - e1y * (x - x1) < t1 ||
				e2x * (y - y2) - e2y * (x - x2) < t2)
				continue;		// outside an edge (d_xdenom < 0 makes inside positive)
			if (x >= r_refdef.vrectright)
				continue;
			zb = D_ZSPANTABLE(y) + x;
			if (z >= *zb)
			{
				*zb = z;
				d_viewbuffer[D_SCANTABLE(y) + x] = col;
			}
		}
	}
	return 1;
}
#endif

/*
================
D_DrawNonSubdiv
================
*/
void D_DrawNonSubdiv (void)
{
	mtriangle_t		*ptri;
	finalvert_t		*pfv, *index0, *index1, *index2;
	int				i;
	int				lnumtriangles;

	pfv = r_affinetridesc.pfinalverts;
	ptri = r_affinetridesc.ptriangles;
	lnumtriangles = r_affinetridesc.numtriangles;

	for (i=0 ; i<lnumtriangles ; i++, ptri++)
	{
		index0 = pfv + ptri->vertindex[0];
		index1 = pfv + ptri->vertindex[1];
		index2 = pfv + ptri->vertindex[2];

		d_xdenom = (index0->v[1]-index1->v[1]) *
				(index0->v[0]-index2->v[0]) -
				(index0->v[0]-index1->v[0])*(index0->v[1]-index2->v[1]);

		QOG_AST(QAS_TRIS, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
		if (d_xdenom >= 0)
		{
			QOG_AST(QAS_BACK, 1);
			continue;
		}
#if defined(QOG_ALIAS_TURBOTINY) && defined(QOG_RTURBO)
		if (r_qog_turboview && D_QogTinyTriangle (index0, index1, index2, ptri->facesfront))
		{
			QOG_AST(QAS_TINYTRI, 1);	// QuakeOG (Agent AR): Turbo, no gradients
			continue;
		}
#endif

		r_p0[0] = index0->v[0];		// u
		r_p0[1] = index0->v[1];		// v
		r_p0[2] = index0->v[2];		// s
		r_p0[3] = index0->v[3];		// t
		r_p0[4] = index0->v[4];		// light
		r_p0[5] = index0->v[5];		// iz

		r_p1[0] = index1->v[0];
		r_p1[1] = index1->v[1];
		r_p1[2] = index1->v[2];
		r_p1[3] = index1->v[3];
		r_p1[4] = index1->v[4];
		r_p1[5] = index1->v[5];

		r_p2[0] = index2->v[0];
		r_p2[1] = index2->v[1];
		r_p2[2] = index2->v[2];
		r_p2[3] = index2->v[3];
		r_p2[4] = index2->v[4];
		r_p2[5] = index2->v[5];

		if (!ptri->facesfront)
		{
			if (index0->flags & ALIAS_ONSEAM)
				r_p0[2] += r_affinetridesc.seamfixupX16;
			if (index1->flags & ALIAS_ONSEAM)
				r_p1[2] += r_affinetridesc.seamfixupX16;
			if (index2->flags & ALIAS_ONSEAM)
				r_p2[2] += r_affinetridesc.seamfixupX16;
		}

		D_PolysetSetEdgeTable ();
		D_RasterizeAliasPolySmooth ();
	}
}


/*
================
D_PolysetRecursiveTriangle
================
*/
void D_PolysetRecursiveTriangle (int *lp1, int *lp2, int *lp3)
{
	int		*temp;
	int		d;
	int		new[6];
	int		z;
	d_zval_t	*zbuf;

	QOG_AST(QAS_SUBCALL, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
	d = lp2[0] - lp1[0];
	if (d < -1 || d > 1)
		goto split;
	d = lp2[1] - lp1[1];
	if (d < -1 || d > 1)
		goto split;

	d = lp3[0] - lp2[0];
	if (d < -1 || d > 1)
		goto split2;
	d = lp3[1] - lp2[1];
	if (d < -1 || d > 1)
		goto split2;

	d = lp1[0] - lp3[0];
	if (d < -1 || d > 1)
		goto split3;
	d = lp1[1] - lp3[1];
	if (d < -1 || d > 1)
	{
split3:
		temp = lp1;
		lp1 = lp3;
		lp3 = lp2;
		lp2 = temp;

		goto split;
	}

	return;			// entire tri is filled

split2:
	temp = lp1;
	lp1 = lp2;
	lp2 = lp3;
	lp3 = temp;

split:
// split this edge
	new[0] = (lp1[0] + lp2[0]) >> 1;
	new[1] = (lp1[1] + lp2[1]) >> 1;
	new[2] = (lp1[2] + lp2[2]) >> 1;
	new[3] = (lp1[3] + lp2[3]) >> 1;
	new[5] = (lp1[5] + lp2[5]) >> 1;

// draw the point if splitting a leading edge
	if (lp2[1] > lp1[1])
		goto nodraw;
	if ((lp2[1] == lp1[1]) && (lp2[0] < lp1[0]))
		goto nodraw;


	z = new[5]>>16;
	z = D_ZQ(z);	// QuakeOG: QOG_ZBUFFER8
	zbuf = D_ZSPANTABLE(new[1]) + new[0];	// QuakeOG: QOG_NO_ROWTABLES
	if (z >= *zbuf)
	{
		int		pix;
		
		*zbuf = z;
		QOG_AST(QAS_SUBPIX, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
		pix = d_pcolormap[D_ATEX (D_SKINROW(new[3]>>16)[new[2]>>16])];	// QuakeOG: D_ATEX
		d_viewbuffer[D_SCANTABLE(new[1]) + new[0]] = pix;
	}

nodraw:
// recursively continue
	D_PolysetRecursiveTriangle (lp3, lp1, new);
	D_PolysetRecursiveTriangle (lp3, new, lp2);
}

#endif	// !id386


/*
================
D_PolysetUpdateTables
================
*/
void D_PolysetUpdateTables (void)
{
	int		i;
	byte	*s;
	
	if (r_affinetridesc.skinwidth != skinwidth ||
		r_affinetridesc.pskin != skinstart)
	{
		skinwidth = r_affinetridesc.skinwidth;
		skinstart = r_affinetridesc.pskin;
#ifndef QOG_NO_ROWTABLES
		s = skinstart;
		for (i=0 ; i<MAX_LBM_HEIGHT ; i++, s+=skinwidth)
			skintable[i] = s;
#endif
	}
}


#if	!id386

/*
===================
D_PolysetScanLeftEdge
====================
*/
void D_PolysetScanLeftEdge (int height)
{

	do
	{
		d_pedgespanpackage->pdest = d_pdest;
		d_pedgespanpackage->pz = d_pz;
		d_pedgespanpackage->count = d_aspancount;
		d_pedgespanpackage->ptex = d_ptex;

		d_pedgespanpackage->sfrac = d_sfrac;
		d_pedgespanpackage->tfrac = d_tfrac;

	// FIXME: need to clamp l, s, t, at both ends?
		d_pedgespanpackage->light = d_light;
		d_pedgespanpackage->zi = d_zi;

		d_pedgespanpackage++;

		errorterm += erroradjustup;
		if (errorterm >= 0)
		{
			d_pdest += d_pdestextrastep;
			d_pz += d_pzextrastep;
			d_aspancount += d_countextrastep;
			d_ptex += d_ptexextrastep;
			d_sfrac += d_sfracextrastep;
			d_ptex += d_sfrac >> 16;

			d_sfrac &= 0xFFFF;
			d_tfrac += d_tfracextrastep;
			if (d_tfrac & 0x10000)
			{
				d_ptex += r_affinetridesc.skinwidth;
				d_tfrac &= 0xFFFF;
			}
			d_light += d_lightextrastep;
			d_zi += d_ziextrastep;
			errorterm -= erroradjustdown;
		}
		else
		{
			d_pdest += d_pdestbasestep;
			d_pz += d_pzbasestep;
			d_aspancount += ubasestep;
			d_ptex += d_ptexbasestep;
			d_sfrac += d_sfracbasestep;
			d_ptex += d_sfrac >> 16;
			d_sfrac &= 0xFFFF;
			d_tfrac += d_tfracbasestep;
			if (d_tfrac & 0x10000)
			{
				d_ptex += r_affinetridesc.skinwidth;
				d_tfrac &= 0xFFFF;
			}
			d_light += d_lightbasestep;
			d_zi += d_zibasestep;
		}
	} while (--height);
}

#endif	// !id386


/*
===================
D_PolysetSetUpForLineScan
====================
*/
void D_PolysetSetUpForLineScan(fixed8_t startvertu, fixed8_t startvertv,
		fixed8_t endvertu, fixed8_t endvertv)
{
	double		dm, dn;
	int			tm, tn;
	adivtab_t	*ptemp;

// TODO: implement x86 version

	errorterm = -1;

	tm = endvertu - startvertu;
	tn = endvertv - startvertv;

	if (((tm <= 16) && (tm >= -15)) &&
		((tn <= 16) && (tn >= -15)))
	{
		ptemp = &adivtab[((tm+15) << 5) + (tn+15)];
		ubasestep = ptemp->quotient;
		erroradjustup = ptemp->remainder;
		erroradjustdown = tn;
	}
	else
	{
#ifdef QOG_FLOAT_EXACT
	// QuakeOG (Agent AA): FloorDivMod's floor quotient and remainder on the
	// ints themselves, the same values without double arithmetic (tm and tn
	// are screen distances, so id's double quotient is exact)
		(void)dm;
		(void)dn;
		if (tn <= 0)
			Sys_Error ("FloorDivMod: bad denominator %d\n", tn);
		ubasestep = tm / tn;
		erroradjustup = tm % tn;
		if (erroradjustup < 0)
		{
			ubasestep--;
			erroradjustup += tn;
		}
		erroradjustdown = tn;
#else
		dm = (double)tm;
		dn = (double)tn;

		FloorDivMod (dm, dn, &ubasestep, &erroradjustup);

		erroradjustdown = dn;
#endif
	}
}


#if	!id386

#ifdef QOG_ALIAS_FAST
/*
================
D_QogGradStep, D_QogCeilDiv, D_QogCalcGradients

QuakeOG (Agent AI): the triangle gradients of Fast mode (r_fast 1,
QOG_ALIAS_FAST) in integers. Each step is the quotient of id's numerator by
the same denominator d_xdenom, with no float. The values are shifted right
by sh before the products so that they fit 32 bits (screen coordinates are
below 2^9, s and t below 2^25, 1/z below 2^31, light below 2^15), and
D_QogGradStep puts the shift back exactly: n * 2^sh / d truncated, from the
quotient and the remainder of n / d. Light keeps id's ceil. Unclipped
vertices have whole texels, so s and t lose nothing, 1/z keeps 21 bits.
================
*/
static int D_QogGradStep (int n, int d, int sh)
{
	int		q, r;

	q = n / d;
	r = n - q * d;
	return q * (1 << sh) + (r * (1 << sh)) / d;
}

static int D_QogCeilDiv (int n, int d)
{
	int		q;

	q = n / d;
	if (n != q * d && ((n < 0) == (d < 0)))
		q++;
	return q;
}

static void D_QogCalcGradients (int skinwidth)
{
	int		t0, t1, d;
	int		p01_minus_p21, p11_minus_p21, p00_minus_p20, p10_minus_p20;

	p00_minus_p20 = r_p0[0] - r_p2[0];
	p01_minus_p21 = r_p0[1] - r_p2[1];
	p10_minus_p20 = r_p1[0] - r_p2[0];
	p11_minus_p21 = r_p1[1] - r_p2[1];
	d = d_xdenom;	// below 0, the triangle faces the viewer

	t0 = r_p0[4] - r_p2[4];
	t1 = r_p1[4] - r_p2[4];
	r_lstepx = D_QogCeilDiv (t1 * p01_minus_p21 - t0 * p11_minus_p21, d);
	r_lstepy = D_QogCeilDiv (t1 * p00_minus_p20 - t0 * p10_minus_p20, -d);

	t0 = (r_p0[2] - r_p2[2]) >> 6;
	t1 = (r_p1[2] - r_p2[2]) >> 6;
	r_sstepx = D_QogGradStep (t1 * p01_minus_p21 - t0 * p11_minus_p21, d, 6);
	r_sstepy = D_QogGradStep (t1 * p00_minus_p20 - t0 * p10_minus_p20, -d, 6);

	t0 = (r_p0[3] - r_p2[3]) >> 6;
	t1 = (r_p1[3] - r_p2[3]) >> 6;
	r_tstepx = D_QogGradStep (t1 * p01_minus_p21 - t0 * p11_minus_p21, d, 6);
	r_tstepy = D_QogGradStep (t1 * p00_minus_p20 - t0 * p10_minus_p20, -d, 6);

	t0 = (r_p0[5] - r_p2[5]) >> 10;
	t1 = (r_p1[5] - r_p2[5]) >> 10;
	r_zistepx = D_QogGradStep (t1 * p01_minus_p21 - t0 * p11_minus_p21, d, 10);
	r_zistepy = D_QogGradStep (t1 * p00_minus_p20 - t0 * p10_minus_p20, -d, 10);

	a_sstepxfrac = r_sstepx & 0xFFFF;
	a_tstepxfrac = r_tstepx & 0xFFFF;

	a_ststepxwhole = skinwidth * (r_tstepx >> 16) + (r_sstepx >> 16);
}
#endif

/*
================
D_PolysetCalcGradients
================
*/
void D_PolysetCalcGradients (int skinwidth)
{
	float	xstepdenominv, ystepdenominv, t0, t1;
	float	p01_minus_p21, p11_minus_p21, p00_minus_p20, p10_minus_p20;

#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
	{
		D_QogCalcGradients (skinwidth);	// QuakeOG (Agent AI): Fast mode
		return;
	}
#endif
	p00_minus_p20 = r_p0[0] - r_p2[0];
	p01_minus_p21 = r_p0[1] - r_p2[1];
	p10_minus_p20 = r_p1[0] - r_p2[0];
	p11_minus_p21 = r_p1[1] - r_p2[1];

	xstepdenominv = QOG_FRECIP ((float)d_xdenom);	// QuakeOG: QOG_FLOAT_EXACT

	ystepdenominv = -xstepdenominv;

// ceil () for light so positive steps are exaggerated, negative steps
// diminished,  pushing us away from underflow toward overflow. Underflow is
// very visible, overflow is very unlikely, because of ambient lighting
	t0 = r_p0[4] - r_p2[4];
	t1 = r_p1[4] - r_p2[4];
	r_lstepx = (int)
			QOG_CEILF((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);	// QuakeOG: QOG_FLOAT_EXACT
	r_lstepy = (int)
			QOG_CEILF((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);	// QuakeOG: QOG_FLOAT_EXACT

	t0 = r_p0[2] - r_p2[2];
	t1 = r_p1[2] - r_p2[2];
	r_sstepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
			xstepdenominv);
	r_sstepy = (int)((t1 * p00_minus_p20 - t0* p10_minus_p20) *
			ystepdenominv);

	t0 = r_p0[3] - r_p2[3];
	t1 = r_p1[3] - r_p2[3];
	r_tstepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
			xstepdenominv);
	r_tstepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
			ystepdenominv);

	t0 = r_p0[5] - r_p2[5];
	t1 = r_p1[5] - r_p2[5];
	r_zistepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
			xstepdenominv);
	r_zistepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
			ystepdenominv);

#if	id386
	a_sstepxfrac = r_sstepx << 16;
	a_tstepxfrac = r_tstepx << 16;
#else
	a_sstepxfrac = r_sstepx & 0xFFFF;
	a_tstepxfrac = r_tstepx & 0xFFFF;
#endif

	a_ststepxwhole = skinwidth * (r_tstepx >> 16) + (r_sstepx >> 16);
}

#endif	// !id386


#if 0
byte gelmap[256];
void InitGel (byte *palette)
{
	int		i;
	int		r;

	for (i=0 ; i<256 ; i++)
	{
//		r = (palette[i*3]>>4);
		r = (palette[i*3] + palette[i*3+1] + palette[i*3+2])/(16*3);
		gelmap[i] = /* 64 */ 0 + r;
	}
}
#endif


#if	!id386

#ifdef QOG_ALIASSPAN_FAST
/*
================
D_PolysetDrawSpans8

QuakeOG (Agent AI): id's alias span drawer with the same pixels
(QOG_ALIASSPAN_FAST, exact). Its steps, the colour map and the skin width
are read once into locals: id's loop read every one of them from memory at
every pixel, because a byte store may alias any global. The texel is
skin[(T >> 16) * skinwidth + (S >> 16)] with S and T the span's 16.16
texture position, which is id's pointer and two fraction carries added up
(a_ststepxwhole and the fractions are r_sstepx and r_tstepx split at bit
16), one multiply on the RP2040. With the one-byte z-buffer and its near
steps (QOG_ZBUFFER8_NEAR) a span whose first and last depth lie in the same
linear piece of D_ZQ takes that piece's shift for every pixel: 1/z is
linear along the span, so every pixel between lies in the piece too.
Player colours (QOG_TRANSLATE_INLINE) go through d_atranslate as before.
================
*/
#define D_QOGTEX(t)		(t)
#define D_QOGTEXTR(t)	(tr[t])
#define D_QOGSPANLOOP(ZQ, TEX)										\
	do																\
	{																\
		int		lz = ZQ;											\
																	\
		if (lz >= *lpz)												\
		{															\
			*lpdest = cmap[TEX (lptex[(lt >> 16) * skinw + (ls >> 16)])	\
					+ (llight & 0xFF00)];							\
			*lpz = lz;												\
			QOG_AST(QAS_PIXW, 1);									\
		}															\
		lpdest++;													\
		lpz++;														\
		lzi += zistepx;												\
		llight += lstepx;											\
		ls += sstepx;												\
		lt += tstepx;												\
	} while (--lcount)
#define D_QOGSPANTR(ZQ)												\
	{																\
		if (tr)														\
			D_QOGSPANLOOP (ZQ, D_QOGTEXTR);							\
		else														\
			D_QOGSPANLOOP (ZQ, D_QOGTEX);							\
	}

void D_PolysetDrawSpans8 (spanpackage_t *pspanpackage)
{
	int			lcount, aspancount, err;
	int			adjup, adjdown, cextra, ubase;
	int			zistepx, lstepx, sstepx, tstepx, skinw;
	byte		*cmap, *tr;

	aspancount = d_aspancount;
	err = errorterm;
	adjup = erroradjustup;
	adjdown = erroradjustdown;
	cextra = d_countextrastep;
	ubase = ubasestep;
	zistepx = r_zistepx;
	lstepx = r_lstepx;
	sstepx = r_sstepx;
	tstepx = r_tstepx;
	skinw = r_affinetridesc.skinwidth;
	cmap = (byte *)acolormap;
#ifdef QOG_TRANSLATE_INLINE
	tr = d_atranslate;
#else
	tr = NULL;
#endif

	do
	{
		lcount = aspancount - pspanpackage->count;

		err += adjup;
		if (err >= 0)
		{
			aspancount += cextra;
			err -= adjdown;
		}
		else
		{
			aspancount += ubase;
		}

		if (lcount)
		{
			byte		*lpdest, *lptex;
			d_zval_t	*lpz;
			int			ls, lt, llight, lzi;

			QOG_AST(QAS_SPANS, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
			QOG_AST(QAS_PIX, lcount);
			lpdest = pspanpackage->pdest;
			lptex = pspanpackage->ptex;
			lpz = pspanpackage->pz;
			ls = pspanpackage->sfrac;
			lt = pspanpackage->tfrac;
			llight = pspanpackage->light;
			lzi = pspanpackage->zi;

#if defined(QOG_ZBUFFER8) && defined(QOG_ZBUFFER8_NEAR)
			{
				int		z0, z1;

				z0 = lzi >> 16;
				z1 = (lzi + zistepx * (lcount - 1)) >> 16;
				if (z0 >= 0 && z1 >= 0 && z0 < 1024 && z1 < 1024)
					D_QOGSPANTR (lzi >> 19)
				else if (z0 >= 1024 && z1 >= 1024 &&
					z0 < 1024 + (128 << QOG_ZBUFFER8_NEAR) &&
					z1 < 1024 + (128 << QOG_ZBUFFER8_NEAR))
					D_QOGSPANTR (((lzi - (1024 << 16)) >> (16 + QOG_ZBUFFER8_NEAR)) + 128)
				else
					D_QOGSPANTR (D_ZQ(lzi >> 16))
			}
#elif defined(QOG_ZBUFFER8)
			D_QOGSPANTR (D_ZQ(lzi >> 16))
#else
			D_QOGSPANTR (lzi >> 16)
#endif
		}

		pspanpackage++;
	} while (pspanpackage->count != -999999);

	d_aspancount = aspancount;
	errorterm = err;
}
#else	// QOG_ALIASSPAN_FAST
/*
================
D_PolysetDrawSpans8
================
*/
#ifdef QOG_TRANSLATE_INLINE
/*
================
D_PolysetDrawSpans8Translated

QuakeOG (Agent S): D_PolysetDrawSpans8 with each texel through
d_atranslate, a copy so id's loop stays as it is
================
*/
static void D_PolysetDrawSpans8Translated (spanpackage_t *pspanpackage)
{
	int		lcount;
	byte	*lpdest;
	byte	*lptex;
	int		lsfrac, ltfrac;
	int		llight;
	int		lzi;
	d_zval_t	*lpz;
	byte	*tr = d_atranslate;

	do
	{
		lcount = d_aspancount - pspanpackage->count;

		errorterm += erroradjustup;
		if (errorterm >= 0)
		{
			d_aspancount += d_countextrastep;
			errorterm -= erroradjustdown;
		}
		else
		{
			d_aspancount += ubasestep;
		}

		if (lcount)
		{
			QOG_AST(QAS_SPANS, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
			QOG_AST(QAS_PIX, lcount);
			lpdest = pspanpackage->pdest;
			lptex = pspanpackage->ptex;
			lpz = pspanpackage->pz;
			lsfrac = pspanpackage->sfrac;
			ltfrac = pspanpackage->tfrac;
			llight = pspanpackage->light;
			lzi = pspanpackage->zi;

			do
			{
#ifdef QOG_ZBUFFER8
				int	lz = lzi >> 16;

				lz = D_ZQ(lz);
				if (lz >= *lpz)
				{
					*lpdest = ((byte *)acolormap)[tr[*lptex] + (llight & 0xFF00)];
					*lpz = lz;
					QOG_AST(QAS_PIXW, 1);
				}
#else
				if ((lzi >> 16) >= *lpz)
				{
					*lpdest = ((byte *)acolormap)[tr[*lptex] + (llight & 0xFF00)];
					*lpz = lzi >> 16;
					QOG_AST(QAS_PIXW, 1);
				}
#endif
				lpdest++;
				lzi += r_zistepx;
				lpz++;
				llight += r_lstepx;
				lptex += a_ststepxwhole;
				lsfrac += a_sstepxfrac;
				lptex += lsfrac >> 16;
				lsfrac &= 0xFFFF;
				ltfrac += a_tstepxfrac;
				if (ltfrac & 0x10000)
				{
					lptex += r_affinetridesc.skinwidth;
					ltfrac &= 0xFFFF;
				}
			} while (--lcount);
		}

		pspanpackage++;
	} while (pspanpackage->count != -999999);
}
#endif

void D_PolysetDrawSpans8 (spanpackage_t *pspanpackage)
{
	int		lcount;
	byte	*lpdest;
	byte	*lptex;
	int		lsfrac, ltfrac;
	int		llight;
	int		lzi;
	d_zval_t	*lpz;

#ifdef QOG_TRANSLATE_INLINE
	if (d_atranslate)
	{
		D_PolysetDrawSpans8Translated (pspanpackage);
		return;
	}
#endif
	do
	{
		lcount = d_aspancount - pspanpackage->count;

		errorterm += erroradjustup;
		if (errorterm >= 0)
		{
			d_aspancount += d_countextrastep;
			errorterm -= erroradjustdown;
		}
		else
		{
			d_aspancount += ubasestep;
		}

		if (lcount)
		{
			QOG_AST(QAS_SPANS, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
			QOG_AST(QAS_PIX, lcount);
			lpdest = pspanpackage->pdest;
			lptex = pspanpackage->ptex;
			lpz = pspanpackage->pz;
			lsfrac = pspanpackage->sfrac;
			ltfrac = pspanpackage->tfrac;
			llight = pspanpackage->light;
			lzi = pspanpackage->zi;

			do
			{
#ifdef QOG_ZBUFFER8
				int	lz = lzi >> 16;

				lz = D_ZQ(lz);
				if (lz >= *lpz)
				{
					*lpdest = ((byte *)acolormap)[*lptex + (llight & 0xFF00)];
					*lpz = lz;
					QOG_AST(QAS_PIXW, 1);
				}
#else
				if ((lzi >> 16) >= *lpz)
				{
					*lpdest = ((byte *)acolormap)[*lptex + (llight & 0xFF00)];
// gel mapping					*lpdest = gelmap[*lpdest];
					*lpz = lzi >> 16;
					QOG_AST(QAS_PIXW, 1);
				}
#endif
				lpdest++;
				lzi += r_zistepx;
				lpz++;
				llight += r_lstepx;
				lptex += a_ststepxwhole;
				lsfrac += a_sstepxfrac;
				lptex += lsfrac >> 16;
				lsfrac &= 0xFFFF;
				ltfrac += a_tstepxfrac;
				if (ltfrac & 0x10000)
				{
					lptex += r_affinetridesc.skinwidth;
					ltfrac &= 0xFFFF;
				}
			} while (--lcount);
		}

		pspanpackage++;
	} while (pspanpackage->count != -999999);
}
#endif	// QOG_ALIASSPAN_FAST
#endif	// !id386


/*
================
D_PolysetFillSpans8
================
*/
void D_PolysetFillSpans8 (spanpackage_t *pspanpackage)
{
	int				color;

// FIXME: do z buffering

	color = d_aflatcolor++;

	while (1)
	{
		int		lcount;
		byte	*lpdest;

		lcount = pspanpackage->count;

		if (lcount == -1)
			return;

		if (lcount)
		{
			lpdest = pspanpackage->pdest;

			do
			{
				*lpdest++ = color;
			} while (--lcount);
		}

		pspanpackage++;
	}
}

/*
================
D_RasterizeAliasPolySmooth
================
*/
void D_RasterizeAliasPolySmooth (void)
{
	int				initialleftheight, initialrightheight;
	int				*plefttop, *prighttop, *pleftbottom, *prightbottom;
	int				working_lstepx, originalcount;
#ifdef QOG_ALIAS_STATS
	unsigned		pix0 = qog_astats[qog_astat_vm][QAS_PIX];	// QuakeOG (Agent AI)

	QOG_AST(QAS_SMOOTH, 1);
#endif

	plefttop = pedgetable->pleftedgevert0;
	prighttop = pedgetable->prightedgevert0;

	pleftbottom = pedgetable->pleftedgevert1;
	prightbottom = pedgetable->prightedgevert1;

	initialleftheight = pleftbottom[1] - plefttop[1];
	initialrightheight = prightbottom[1] - prighttop[1];

//
// set the s, t, and light gradients, which are consistent across the triangle
// because being a triangle, things are affine
//
	D_PolysetCalcGradients (r_affinetridesc.skinwidth);

//
// rasterize the polygon
//

//
// scan out the top (and possibly only) part of the left edge
//
	d_pedgespanpackage = a_spans;

	ystart = plefttop[1];
	d_aspancount = plefttop[0] - prighttop[0];

	d_ptex = (byte *)r_affinetridesc.pskin + (plefttop[2] >> 16) +
			(plefttop[3] >> 16) * r_affinetridesc.skinwidth;
#if	id386
	d_sfrac = (plefttop[2] & 0xFFFF) << 16;
	d_tfrac = (plefttop[3] & 0xFFFF) << 16;
#else
	d_sfrac = plefttop[2] & 0xFFFF;
	d_tfrac = plefttop[3] & 0xFFFF;
#endif
	d_light = plefttop[4];
	d_zi = plefttop[5];

	d_pdest = (byte *)d_viewbuffer +
			ystart * screenwidth + plefttop[0];
	d_pz = d_pzbuffer + ystart * d_zwidth + plefttop[0];

	if (initialleftheight == 1)
	{
		d_pedgespanpackage->pdest = d_pdest;
		d_pedgespanpackage->pz = d_pz;
		d_pedgespanpackage->count = d_aspancount;
		d_pedgespanpackage->ptex = d_ptex;

		d_pedgespanpackage->sfrac = d_sfrac;
		d_pedgespanpackage->tfrac = d_tfrac;

	// FIXME: need to clamp l, s, t, at both ends?
		d_pedgespanpackage->light = d_light;
		d_pedgespanpackage->zi = d_zi;

		d_pedgespanpackage++;
	}
	else
	{
		D_PolysetSetUpForLineScan(plefttop[0], plefttop[1],
							  pleftbottom[0], pleftbottom[1]);

	#if	id386
		d_pzbasestep = (d_zwidth + ubasestep) << 1;
		d_pzextrastep = d_pzbasestep + 2;
	#else
		d_pzbasestep = d_zwidth + ubasestep;
		d_pzextrastep = d_pzbasestep + 1;
	#endif

		d_pdestbasestep = screenwidth + ubasestep;
		d_pdestextrastep = d_pdestbasestep + 1;

	// TODO: can reuse partial expressions here

	// for negative steps in x along left edge, bias toward overflow rather than
	// underflow (sort of turning the floor () we did in the gradient calcs into
	// ceil (), but plus a little bit)
		if (ubasestep < 0)
			working_lstepx = r_lstepx - 1;
		else
			working_lstepx = r_lstepx;

		d_countextrastep = ubasestep + 1;
		d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) +
				((r_tstepy + r_tstepx * ubasestep) >> 16) *
				r_affinetridesc.skinwidth;
	#if	id386
		d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) << 16;
		d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) << 16;
	#else
		d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
		d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
	#endif
		d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
		d_zibasestep = r_zistepy + r_zistepx * ubasestep;

		d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) +
				((r_tstepy + r_tstepx * d_countextrastep) >> 16) *
				r_affinetridesc.skinwidth;
	#if	id386
		d_sfracextrastep = (r_sstepy + r_sstepx*d_countextrastep) << 16;
		d_tfracextrastep = (r_tstepy + r_tstepx*d_countextrastep) << 16;
	#else
		d_sfracextrastep = (r_sstepy + r_sstepx*d_countextrastep) & 0xFFFF;
		d_tfracextrastep = (r_tstepy + r_tstepx*d_countextrastep) & 0xFFFF;
	#endif
		d_lightextrastep = d_lightbasestep + working_lstepx;
		d_ziextrastep = d_zibasestep + r_zistepx;

		D_PolysetScanLeftEdge (initialleftheight);
	}

//
// scan out the bottom part of the left edge, if it exists
//
	if (pedgetable->numleftedges == 2)
	{
		int		height;

		plefttop = pleftbottom;
		pleftbottom = pedgetable->pleftedgevert2;

		height = pleftbottom[1] - plefttop[1];

// TODO: make this a function; modularize this function in general

		ystart = plefttop[1];
		d_aspancount = plefttop[0] - prighttop[0];
		d_ptex = (byte *)r_affinetridesc.pskin + (plefttop[2] >> 16) +
				(plefttop[3] >> 16) * r_affinetridesc.skinwidth;
		d_sfrac = 0;
		d_tfrac = 0;
		d_light = plefttop[4];
		d_zi = plefttop[5];

		d_pdest = (byte *)d_viewbuffer + ystart * screenwidth + plefttop[0];
		d_pz = d_pzbuffer + ystart * d_zwidth + plefttop[0];

		if (height == 1)
		{
			d_pedgespanpackage->pdest = d_pdest;
			d_pedgespanpackage->pz = d_pz;
			d_pedgespanpackage->count = d_aspancount;
			d_pedgespanpackage->ptex = d_ptex;

			d_pedgespanpackage->sfrac = d_sfrac;
			d_pedgespanpackage->tfrac = d_tfrac;

		// FIXME: need to clamp l, s, t, at both ends?
			d_pedgespanpackage->light = d_light;
			d_pedgespanpackage->zi = d_zi;

			d_pedgespanpackage++;
		}
		else
		{
			D_PolysetSetUpForLineScan(plefttop[0], plefttop[1],
								  pleftbottom[0], pleftbottom[1]);

			d_pdestbasestep = screenwidth + ubasestep;
			d_pdestextrastep = d_pdestbasestep + 1;

	#if	id386
			d_pzbasestep = (d_zwidth + ubasestep) << 1;
			d_pzextrastep = d_pzbasestep + 2;
	#else
			d_pzbasestep = d_zwidth + ubasestep;
			d_pzextrastep = d_pzbasestep + 1;
	#endif

			if (ubasestep < 0)
				working_lstepx = r_lstepx - 1;
			else
				working_lstepx = r_lstepx;

			d_countextrastep = ubasestep + 1;
			d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) +
					((r_tstepy + r_tstepx * ubasestep) >> 16) *
					r_affinetridesc.skinwidth;
	#if	id386
			d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) << 16;
			d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) << 16;
	#else
			d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
			d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
	#endif
			d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
			d_zibasestep = r_zistepy + r_zistepx * ubasestep;

			d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) +
					((r_tstepy + r_tstepx * d_countextrastep) >> 16) *
					r_affinetridesc.skinwidth;
	#if	id386
			d_sfracextrastep = ((r_sstepy+r_sstepx*d_countextrastep) & 0xFFFF)<<16;
			d_tfracextrastep = ((r_tstepy+r_tstepx*d_countextrastep) & 0xFFFF)<<16;
	#else
			d_sfracextrastep = (r_sstepy+r_sstepx*d_countextrastep) & 0xFFFF;
			d_tfracextrastep = (r_tstepy+r_tstepx*d_countextrastep) & 0xFFFF;
	#endif
			d_lightextrastep = d_lightbasestep + working_lstepx;
			d_ziextrastep = d_zibasestep + r_zistepx;

			D_PolysetScanLeftEdge (height);
		}
	}

// scan out the top (and possibly only) part of the right edge, updating the
// count field
	d_pedgespanpackage = a_spans;

	D_PolysetSetUpForLineScan(prighttop[0], prighttop[1],
						  prightbottom[0], prightbottom[1]);
	d_aspancount = 0;
	d_countextrastep = ubasestep + 1;
	originalcount = a_spans[initialrightheight].count;
	a_spans[initialrightheight].count = -999999; // mark end of the spanpackages
	D_PolysetDrawSpans8 (a_spans);

// scan out the bottom part of the right edge, if it exists
	if (pedgetable->numrightedges == 2)
	{
		int				height;
		spanpackage_t	*pstart;

		pstart = a_spans + initialrightheight;
		pstart->count = originalcount;

		d_aspancount = prightbottom[0] - prighttop[0];

		prighttop = prightbottom;
		prightbottom = pedgetable->prightedgevert2;

		height = prightbottom[1] - prighttop[1];

		D_PolysetSetUpForLineScan(prighttop[0], prighttop[1],
							  prightbottom[0], prightbottom[1]);

		d_countextrastep = ubasestep + 1;
		a_spans[initialrightheight + height].count = -999999;
											// mark end of the spanpackages
		D_PolysetDrawSpans8 (pstart);
	}
#ifdef QOG_ALIAS_STATS
	pix0 = qog_astats[qog_astat_vm][QAS_PIX] - pix0;	// QuakeOG (Agent AI)
	QOG_AST(QAS_ZERO, pix0 == 0);
	QOG_AST(QAS_TINY, pix0 >= 1 && pix0 <= 4);
#endif
}


/*
================
D_PolysetSetEdgeTable
================
*/
void D_PolysetSetEdgeTable (void)
{
	int			edgetableindex;

	edgetableindex = 0;	// assume the vertices are already in
						//  top to bottom order

//
// determine which edges are right & left, and the order in which
// to rasterize them
//
	if (r_p0[1] >= r_p1[1])
	{
		if (r_p0[1] == r_p1[1])
		{
			if (r_p0[1] < r_p2[1])
				pedgetable = &edgetables[2];
			else
				pedgetable = &edgetables[5];

			return;
		}
		else
		{
			edgetableindex = 1;
		}
	}

	if (r_p0[1] == r_p2[1])
	{
		if (edgetableindex)
			pedgetable = &edgetables[8];
		else
			pedgetable = &edgetables[9];

		return;
	}
	else if (r_p1[1] == r_p2[1])
	{
		if (edgetableindex)
			pedgetable = &edgetables[10];
		else
			pedgetable = &edgetables[11];

		return;
	}

	if (r_p0[1] > r_p2[1])
		edgetableindex += 2;

	if (r_p1[1] > r_p2[1])
		edgetableindex += 4;

	pedgetable = &edgetables[edgetableindex];
}


#if 0

void D_PolysetRecursiveDrawLine (int *lp1, int *lp2)
{
	int		d;
	int		new[6];
	int 	ofs;
	
	d = lp2[0] - lp1[0];
	if (d < -1 || d > 1)
		goto split;
	d = lp2[1] - lp1[1];
	if (d < -1 || d > 1)
		goto split;

	return;	// line is completed

split:
// split this edge
	new[0] = (lp1[0] + lp2[0]) >> 1;
	new[1] = (lp1[1] + lp2[1]) >> 1;
	new[5] = (lp1[5] + lp2[5]) >> 1;
	new[2] = (lp1[2] + lp2[2]) >> 1;
	new[3] = (lp1[3] + lp2[3]) >> 1;
	new[4] = (lp1[4] + lp2[4]) >> 1;

// draw the point
	ofs = D_SCANTABLE(new[1]) + new[0];	// QuakeOG: QOG_NO_ROWTABLES
	if (new[5] > d_pzbuffer[ofs])
	{
		int		pix;
		
		d_pzbuffer[ofs] = new[5];
		pix = D_SKINROW(new[3]>>16)[new[2]>>16];
//		pix = ((byte *)acolormap)[pix + (new[4] & 0xFF00)];
		d_viewbuffer[ofs] = pix;
	}

// recursively continue
	D_PolysetRecursiveDrawLine (lp1, new);
	D_PolysetRecursiveDrawLine (new, lp2);
}

void D_PolysetRecursiveTriangle2 (int *lp1, int *lp2, int *lp3)
{
	int		d;
	int		new[4];
	
	d = lp2[0] - lp1[0];
	if (d < -1 || d > 1)
		goto split;
	d = lp2[1] - lp1[1];
	if (d < -1 || d > 1)
		goto split;
	return;

split:
// split this edge
	new[0] = (lp1[0] + lp2[0]) >> 1;
	new[1] = (lp1[1] + lp2[1]) >> 1;
	new[5] = (lp1[5] + lp2[5]) >> 1;
	new[2] = (lp1[2] + lp2[2]) >> 1;
	new[3] = (lp1[3] + lp2[3]) >> 1;
	new[4] = (lp1[4] + lp2[4]) >> 1;

	D_PolysetRecursiveDrawLine (new, lp3);

// recursively continue
	D_PolysetRecursiveTriangle (lp1, new, lp3);
	D_PolysetRecursiveTriangle (new, lp2, lp3);
}

#endif

