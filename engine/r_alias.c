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
// r_alias.c: routines for setting up to draw alias models

#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"	// FIXME: shouldn't be needed (is needed for patch
						// right now, but that should move)
#include "r_qogalias.h"	// QuakeOG (Agent AI)

#ifdef QOG_ALIAS_STATS
// QuakeOG (Agent AI): PC measurement of the alias and particle drawers' work
unsigned	qog_astats[2][QAS_N];
int			qog_astat_vm;

void R_QogAliasStatsView (void)
{
	static int		lastmodel;
	static int		frame0;
	int				s;

	if (cl.worldmodel != (model_t *)lastmodel || r_framecount < frame0)
	{
		lastmodel = (int)cl.worldmodel;
		memset (qog_astats, 0, sizeof(qog_astats));
		frame0 = r_framecount;
	}
	if ((r_framecount - frame0) % 100 || r_framecount == frame0)
		return;
	for (s=0 ; s<2 ; s++)
	{
		unsigned	*a = qog_astats[s];

		Con_Printf ("qog_aliasstats %s views %d check %u drawn %u trivial %u subdivm %u verts %u tris %u back %u"
				" smooth %u zero %u tiny %u spans %u pix %u pixw %u subtri %u subcall %u subpix %u cliptri %u"
				" parts %u partvis %u partpix %u\n",
				s ? "vm" : "ents", r_framecount - frame0, a[QAS_CHECK], a[QAS_DRAWN], a[QAS_TRIVIAL],
				a[QAS_SUBDIVM], a[QAS_VERTS], a[QAS_TRIS], a[QAS_BACK], a[QAS_SMOOTH], a[QAS_ZERO],
				a[QAS_TINY], a[QAS_SPANS], a[QAS_PIX], a[QAS_PIXW], a[QAS_SUBTRI], a[QAS_SUBCALL],
				a[QAS_SUBPIX], a[QAS_CLIPTRI], a[QAS_PARTS], a[QAS_PARTVIS], a[QAS_PARTPIX]);
	}
}
#endif

#define LIGHT_MIN	5		// lowest light value we'll allow, to avoid the
							//  need for inner-loop light clamping

mtriangle_t		*ptriangles;
affinetridesc_t	r_affinetridesc;

void *			acolormap;	// FIXME: should go away

trivertx_t		*r_apverts;

// TODO: these probably will go away with optimized rasterization
mdl_t				*pmdl;
vec3_t				r_plightvec;
int					r_ambientlight;
float				r_shadelight;
aliashdr_t			*paliashdr;
finalvert_t			*pfinalverts;
auxvert_t			*pauxverts;
static float		ziscale;
static model_t		*pmodel;

static vec3_t		alias_forward, alias_right, alias_up;

static maliasskindesc_t	*pskindesc;

int				r_amodels_drawn;
int				a_skinwidth;
int				r_anumverts;

float	aliastransform[3][4];

typedef struct {
	int	index0;
	int	index1;
} aedge_t;

static QOG_RTABLE_CONST aedge_t	aedges[12] = {	// QuakeOG: const with QOG_CONST_RTABLES
{0, 1}, {1, 2}, {2, 3}, {3, 0},
{4, 5}, {5, 6}, {6, 7}, {7, 4},
{0, 5}, {1, 4}, {2, 7}, {3, 6}
};

#define NUMVERTEXNORMALS	162

// QuakeOG: QOG_CONST_RTABLES keeps the normal table in flash (it is only read)
QOG_RTABLE_CONST float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

void R_AliasTransformAndProjectFinalVerts (finalvert_t *fv,
	stvert_t *pstverts);
void R_AliasSetUpTransform (int trivial_accept);
void R_AliasTransformVector (vec3_t in, vec3_t out);
void R_AliasTransformFinalVert (finalvert_t *fv, auxvert_t *av,
	trivertx_t *pverts, stvert_t *pstverts);
void R_AliasProjectFinalVert (finalvert_t *fv, auxvert_t *av);
#ifdef QOG_ALIAS_FAST
// QuakeOG (Agent AI): kept out of R_AliasCheckBBox, whose frame stays id's
#if defined(__GNUC__)
#define QOG_ANOINLINE	__attribute__((noinline))
#elif defined(_MSC_VER)
#define QOG_ANOINLINE	__declspec(noinline)
#else
#define QOG_ANOINLINE
#endif
static QOG_ANOINLINE qboolean R_QogAliasSphereOut (void);
static QOG_ANOINLINE qboolean R_QogAliasCheckBBoxFast (maliasframedesc_t *pframedesc);
#endif


/*
================
R_AliasCheckBBox
================
*/
qboolean R_AliasCheckBBox (void)
{
	int					i, flags, frame, numv;
	aliashdr_t			*pahdr;
	float				zi, basepts[8][3], v0, v1, frac;
	finalvert_t			*pv0, *pv1, viewpts[16];
	auxvert_t			*pa0, *pa1, viewaux[16];
	maliasframedesc_t	*pframedesc;
	qboolean			zclipped, zfullyclipped;
	unsigned			anyclip, allclip;
	int					minz;
	
// expand, rotate, and translate points into worldspace

	currententity->trivial_accept = 0;
#ifdef QOG_ALIAS_STATS
	qog_astat_vm = 0;	// QuakeOG (Agent AI)
	QOG_AST(QAS_CHECK, 1);
#endif
	pmodel = currententity->model;
	pahdr = Mod_Extradata (pmodel);
	pmdl = (mdl_t *)((byte *)pahdr + pahdr->model);

#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
	{
		// QuakeOG (Agent AI): Fast mode, the integer transform below
		if (R_QogAliasSphereOut ())
			return false;	// wholly outside the view
	}
	else
#endif
	R_AliasSetUpTransform (0);

// construct the base bounding box for this frame
	frame = currententity->frame;
// TODO: don't repeat this check when drawing?
	if ((frame >= pmdl->numframes) || (frame < 0))
	{
		Con_DPrintf ("No such frame %d %s\n", frame,
				pmodel->name);
		frame = 0;
	}

	pframedesc = &pahdr->frames[frame];

#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
		return R_QogAliasCheckBBoxFast (pframedesc);	// QuakeOG (Agent AI)
#endif

// x worldspace coordinates
	basepts[0][0] = basepts[1][0] = basepts[2][0] = basepts[3][0] =
			(float)pframedesc->bboxmin.v[0];
	basepts[4][0] = basepts[5][0] = basepts[6][0] = basepts[7][0] =
			(float)pframedesc->bboxmax.v[0];

// y worldspace coordinates
	basepts[0][1] = basepts[3][1] = basepts[5][1] = basepts[6][1] =
			(float)pframedesc->bboxmin.v[1];
	basepts[1][1] = basepts[2][1] = basepts[4][1] = basepts[7][1] =
			(float)pframedesc->bboxmax.v[1];

// z worldspace coordinates
	basepts[0][2] = basepts[1][2] = basepts[4][2] = basepts[5][2] =
			(float)pframedesc->bboxmin.v[2];
	basepts[2][2] = basepts[3][2] = basepts[6][2] = basepts[7][2] =
			(float)pframedesc->bboxmax.v[2];

	zclipped = false;
	zfullyclipped = true;

	minz = 9999;
	for (i=0; i<8 ; i++)
	{
		R_AliasTransformVector  (&basepts[i][0], &viewaux[i].fv[0]);

		if (viewaux[i].fv[2] < ALIAS_Z_CLIP_PLANE)
		{
		// we must clip points that are closer than the near clip plane
			viewpts[i].flags = ALIAS_Z_CLIP;
			zclipped = true;
		}
		else
		{
			if (viewaux[i].fv[2] < minz)
				minz = viewaux[i].fv[2];
			viewpts[i].flags = 0;
			zfullyclipped = false;
		}
	}

	
	if (zfullyclipped)
	{
		return false;	// everything was near-z-clipped
	}

	numv = 8;

	if (zclipped)
	{
	// organize points by edges, use edges to get new points (possible trivial
	// reject)
		for (i=0 ; i<12 ; i++)
		{
		// edge endpoints
			pv0 = &viewpts[aedges[i].index0];
			pv1 = &viewpts[aedges[i].index1];
			pa0 = &viewaux[aedges[i].index0];
			pa1 = &viewaux[aedges[i].index1];

		// if one end is clipped and the other isn't, make a new point
			if (pv0->flags ^ pv1->flags)
			{
				frac = (ALIAS_Z_CLIP_PLANE - pa0->fv[2]) /
					   (pa1->fv[2] - pa0->fv[2]);
				viewaux[numv].fv[0] = pa0->fv[0] +
						(pa1->fv[0] - pa0->fv[0]) * frac;
				viewaux[numv].fv[1] = pa0->fv[1] +
						(pa1->fv[1] - pa0->fv[1]) * frac;
				viewaux[numv].fv[2] = ALIAS_Z_CLIP_PLANE;
				viewpts[numv].flags = 0;
				numv++;
			}
		}
	}

// project the vertices that remain after clipping
	anyclip = 0;
	allclip = ALIAS_XY_CLIP_MASK;

// TODO: probably should do this loop in ASM, especially if we use floats
	for (i=0 ; i<numv ; i++)
	{
	// we don't need to bother with vertices that were z-clipped
		if (viewpts[i].flags & ALIAS_Z_CLIP)
			continue;

		zi = QOG_FRECIP (viewaux[i].fv[2]);	// QuakeOG: QOG_FLOAT_EXACT

	// FIXME: do with chop mode in ASM, or convert to float
		v0 = (viewaux[i].fv[0] * xscale * zi) + xcenter;
		v1 = (viewaux[i].fv[1] * yscale * zi) + ycenter;

		flags = 0;

		if (v0 < r_refdef.fvrectx)
			flags |= ALIAS_LEFT_CLIP;
		if (v1 < r_refdef.fvrecty)
			flags |= ALIAS_TOP_CLIP;
		if (v0 > r_refdef.fvrectright)
			flags |= ALIAS_RIGHT_CLIP;
		if (v1 > r_refdef.fvrectbottom)
			flags |= ALIAS_BOTTOM_CLIP;

		anyclip |= flags;
		allclip &= flags;
	}

	if (allclip)
		return false;	// trivial reject off one side

	currententity->trivial_accept = !anyclip & !zclipped;

	if (currententity->trivial_accept)
	{
		if (minz > (r_aliastransition + (pmdl->size * r_resfudge)))
		{
			currententity->trivial_accept |= 2;
		}
	}

	return true;
}


/*
================
R_AliasTransformVector
================
*/
void R_AliasTransformVector (vec3_t in, vec3_t out)
{
	out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
	out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
	out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}


/*
================
R_AliasPreparePoints

General clipped case
================
*/
#ifdef QOG_ALIAS_CORE1
/*
QuakeOG (Agent Z): the two cores share an alias model's vertices, core 0
the even ones and core 1 the odd ones (QOG_ALIAS_CORE1, with
QOG_SPANS_CORE1). Each vertex is computed by id's statements from the
model's data and the model's transform and lighting, which nothing changes
while the vertices are made, and is written only to its own finalvert, so
the result is the same as id's loop. Triangles are drawn afterwards by
core 0 alone, in id's order.
*/
#ifndef QOG_SPANS_CORE1
#error "QOG_ALIAS_CORE1 needs the core 1 job of QOG_SPANS_CORE1"
#endif
#define QOG_ALIAS_CORE1_MIN	24	// fewer vertices are not worth waking core 1

typedef struct
{
	finalvert_t	*fv;
	trivertx_t	*pverts;
	stvert_t	*pstverts;
	int			first, count;
} r_qogaliasjob_t;

static void R_QogAliasPrepareRange (r_qogaliasjob_t *job)
{
	int			i;
	finalvert_t	*fv;
	auxvert_t	*av;
#ifdef QOG_ALIAS_NO_AUXVERTS
	auxvert_t	avone;

	av = &avone;
#endif

	for (i = job->first ; i < job->count ; i += 2)
	{
		fv = job->fv + i;
#ifndef QOG_ALIAS_NO_AUXVERTS
		av = pauxverts + i;
#endif
		R_AliasTransformFinalVert (fv, av, job->pverts + i, job->pstverts + i);
		if (av->fv[2] < ALIAS_Z_CLIP_PLANE)
			fv->flags |= ALIAS_Z_CLIP;
		else
		{
			 R_AliasProjectFinalVert (fv, av);

			if (fv->v[0] < r_refdef.aliasvrect.x)
				fv->flags |= ALIAS_LEFT_CLIP;
			if (fv->v[1] < r_refdef.aliasvrect.y)
				fv->flags |= ALIAS_TOP_CLIP;
			if (fv->v[0] > r_refdef.aliasvrectright)
				fv->flags |= ALIAS_RIGHT_CLIP;
			if (fv->v[1] > r_refdef.aliasvrectbottom)
				fv->flags |= ALIAS_BOTTOM_CLIP;	
		}
	}
}

static void R_QogAliasPrepareCore1 (void *job)
{
	R_QogAliasPrepareRange ((r_qogaliasjob_t *)job);
}
#endif

#ifdef QOG_ALIAS_FAST
/*
================
Fast mode alias vertices

QuakeOG (Agent AI): with r_fast 1 (QOG_ALIAS_FAST) the vertices of an alias
model are made in integers. The model's transform (R_QogAliasXform, the
model to view space without the screen scaling of the unclipped case) in
16.16 fixed point makes a vertex nine integer multiplies, 1/z is one unsigned divide (the RP2040's
hardware divider) and the screen position two divides in 28.4, the light
three multiplies with id's normals in 2.14 (anorms14.h). The screen
position is within a sixteenth of a pixel of id's float projection, 1/z
keeps 23 bits. A point too far off the screen for 32 bits takes a float
divide. The triangle clipper's z clip recomputes view space in float from
the same transform made float (R_QogAliasFloatXform, r_aclip.c).
================
*/
static const short	r_qoganorms[NUMVERTEXNORMALS][3] = {
#include "anorms14.h"
};

typedef struct
{
	int		m[3][4];		// model to view transform, 16.16
	int		fwd[3], rt[3], up[3];	// the entity's AngleVectors, 2.14
	int		lv[3];			// r_plightvec, 2.14
	int		shade;			// r_shadelight
	int		xc, yc;			// aliasxcenter and aliasycenter, 28.4
	int		xs, ys;			// aliasxscale and aliasyscale, 28.4
	int		xlim, ylim;		// x and y in 24.8 below these keep 32 bits
	int		zimul;			// ziscale / 2^31, 3 for the view model
} r_qogxf_t;

static r_qogxf_t	*r_qogxfp;	// the model being drawn (R_AliasDrawModel)

static const short	r_qogsin[1025] = {
#include "qogsin.h"
};

// sine and cosine of an angle in degrees, 2.14, from a table of 1024 steps
// of a turn with linear steps between them (within 2^-13 of id's sin)
static void R_QogSinCos (float deg, int *s, int *c)
{
	int		a, i, f;

	a = (int)(deg * (65536.0f / 360.0f)) & 0xffff;
	i = a >> 6;
	f = a & 63;
	*s = r_qogsin[i] + (((r_qogsin[i + 1] - r_qogsin[i]) * f) >> 6);
	a = (a + 0x4000) & 0xffff;
	i = a >> 6;
	f = a & 63;
	*c = r_qogsin[i] + (((r_qogsin[i + 1] - r_qogsin[i]) * f) >> 6);
}

#define QM14(a, b)	(((a) * (b)) >> 14)

/*
================
R_QogAliasXform

QuakeOG (Agent AI): R_AliasSetUpTransform (0) of Fast mode in integers.
id's AngleVectors takes three double sines and cosines and the transform
two float 3x4 products. Here the entity's three vectors come from a sine
table in 2.14, the view vectors and the model's scale, origin and offset
are read in fixed point by their bits (R_QogFix), and the products are
integer: the model to view rotation in 2.14 times the scale in 16.16 (in
two parts, so nothing passes 32 bits), the translation in 16.16.
================
*/
static void R_QogAliasXform (r_qogxf_t *xf)
{
	int		sy, cy, sp, cp, sr, cr, srsp, crsp, i, j, c, t, mo, mo8;
	int		t2[3][3], v[3][3], s16[3], so6[3];

	R_QogSinCos (currententity->angles[YAW], &sy, &cy);
	R_QogSinCos (-currententity->angles[PITCH], &sp, &cp);
	R_QogSinCos (currententity->angles[ROLL], &sr, &cr);
	srsp = QM14 (sr, sp);
	crsp = QM14 (cr, sp);

	xf->fwd[0] = QM14 (cp, cy);
	xf->fwd[1] = QM14 (cp, sy);
	xf->fwd[2] = -sp;
	xf->rt[0] = -QM14 (srsp, cy) + QM14 (cr, sy);
	xf->rt[1] = -QM14 (srsp, sy) - QM14 (cr, cy);
	xf->rt[2] = -QM14 (sr, cp);
	xf->up[0] = QM14 (crsp, cy) + QM14 (sr, sy);
	xf->up[1] = QM14 (crsp, sy) - QM14 (sr, cy);
	xf->up[2] = QM14 (cr, cp);

	for (i=0 ; i<3 ; i++)
	{
		t2[i][0] = xf->fwd[i];
		t2[i][1] = -xf->rt[i];
		t2[i][2] = xf->up[i];
		v[0][i] = R_QogFix (vright[i], 14);
		v[1][i] = -R_QogFix (vup[i], 14);
		v[2][i] = R_QogFix (vpn[i], 14);
		s16[i] = R_QogFix (pmdl->scale[i], 16);
		so6[i] = R_QogFix (pmdl->scale_origin[i], 6);
	}

	for (i=0 ; i<3 ; i++)
	{
		t = 0;
		for (j=0 ; j<3 ; j++)
		{
			c = (v[i][0] * t2[0][j] + v[i][1] * t2[1][j] + v[i][2] * t2[2][j]) >> 14;
			xf->m[i][j] = ((c * (s16[j] >> 8)) >> 6) + ((c * (s16[j] & 255)) >> 14);
			t += c * so6[j];
		}
	// the entity's origin in view space, V . modelorg in two parts
		mo = 0;
		for (j=0 ; j<3 ; j++)
		{
			mo8 = R_QogFix (modelorg[j], 8);
			mo += v[i][j] * (mo8 >> 8) * 4 + ((v[i][j] * (mo8 & 255)) >> 6);
		}
		xf->m[i][3] = (t >> 4) - mo;
	}

	xf->xc = R_QogFix (aliasxcenter, 4);
	xf->yc = R_QogFix (aliasycenter, 4);
	xf->xs = R_QogFix (aliasxscale, 4);
	xf->ys = R_QogFix (aliasyscale, 4);
	xf->xlim = 0x7fffffff / (xf->xs > 0 ? xf->xs : 1);
	xf->ylim = 0x7fffffff / (xf->ys > 0 ? xf->ys : 1);
	xf->zimul = currententity == &cl.viewent ? 3 : 1;
}

// id's float aliastransform (unscaled) from the integer one, for the
// triangle clipper's z clip (r_aclip.c), only in a clipped model
static void R_QogAliasFloatXform (const r_qogxf_t *xf)
{
	int		i, j;

	for (i=0 ; i<3 ; i++)
		for (j=0 ; j<4 ; j++)
			aliastransform[i][j] = (float)xf->m[i][j] * (1.0f / 65536.0f);
}

// x in 24.8 view space and z8 the depth in 24.8 (at least
// ALIAS_Z_CLIP_PLANE): the screen column as id's
// (int)(x * aliasxscale / z + aliasxcenter), s and c in 28.4
#define R_QOGPROJECT(x, z8, s, c, lim, fs, fc)	\
	((x) > -(lim) && (x) < (lim) ? ((x) * (s) / (z8) + (c)) >> 4 \
	: (int)((float)(x) * (fs) / (float)(z8) + (fc)))

static void R_QogAliasFastVert (const r_qogxf_t *xf, finalvert_t *fv,
	trivertx_t *pverts, stvert_t *pstverts, int clip)
{
	int			x, y, z, z8, lc, temp, v0, v1, v2;
	unsigned	q;
	const short	*n;

	v0 = pverts->v[0];
	v1 = pverts->v[1];
	v2 = pverts->v[2];

	fv->v[2] = pstverts->s;
	fv->v[3] = pstverts->t;
	fv->flags = pstverts->onseam;

// lighting, id's with the cosine in 4.28
	n = r_qoganorms[pverts->lightnormalindex];
	lc = n[0] * xf->lv[0] + n[1] * xf->lv[1] + n[2] * xf->lv[2];
	temp = r_ambientlight;
	if (lc < 0)
	{
		temp += (xf->shade * (lc >> 14)) / 16384;
		if (temp < 0)
			temp = 0;
	}
	fv->v[4] = temp;

	z = xf->m[2][0] * v0 + xf->m[2][1] * v1 + xf->m[2][2] * v2 + xf->m[2][3];
	if (clip && z < ALIAS_Z_CLIP_PLANE * 0x10000)
	{
		fv->flags |= ALIAS_Z_CLIP;
		return;
	}
	x = (xf->m[0][0] * v0 + xf->m[0][1] * v1 + xf->m[0][2] * v2 + xf->m[0][3]) >> 8;
	y = (xf->m[1][0] * v0 + xf->m[1][1] * v1 + xf->m[1][2] * v2 + xf->m[1][3]) >> 8;
	z8 = z >> 8;
	if (z8 < ALIAS_Z_CLIP_PLANE * 0x100)
		z8 = ALIAS_Z_CLIP_PLANE * 0x100;

// 1/z scaled up by 2^31 (3 * 2^31 for the view model) as id's v[5]
	q = 0x80000000u / (unsigned)z8;
	fv->v[5] = (int)((q * xf->zimul) << 8);

	fv->v[0] = R_QOGPROJECT (x, z8, xf->xs, xf->xc, xf->xlim, aliasxscale, aliasxcenter);
	fv->v[1] = R_QOGPROJECT (y, z8, xf->ys, xf->yc, xf->ylim, aliasyscale, aliasycenter);

	if (clip)
	{
		if (fv->v[0] < r_refdef.aliasvrect.x)
			fv->flags |= ALIAS_LEFT_CLIP;
		if (fv->v[1] < r_refdef.aliasvrect.y)
			fv->flags |= ALIAS_TOP_CLIP;
		if (fv->v[0] > r_refdef.aliasvrectright)
			fv->flags |= ALIAS_RIGHT_CLIP;
		if (fv->v[1] > r_refdef.aliasvrectbottom)
			fv->flags |= ALIAS_BOTTOM_CLIP;
	}
}

typedef struct
{
	const r_qogxf_t	*xf;
	finalvert_t		*fv;
	trivertx_t		*pverts;
	stvert_t		*pstverts;
	int				first, count, step, clip;
} r_qogfastjob_t;

static void R_QogAliasFastRange (r_qogfastjob_t *job)
{
	int		i;

	for (i = job->first ; i < job->count ; i += job->step)
		R_QogAliasFastVert (job->xf, job->fv + i, job->pverts + i,
				job->pstverts + i, job->clip);
}

#ifdef QOG_ALIAS_CORE1
static void R_QogAliasFastCore1 (void *job)
{
	R_QogAliasFastRange ((r_qogfastjob_t *)job);
}
#endif

// all r_anumverts vertices of r_apverts, shared by both cores as Agent Z's
// exact vertices are (QOG_ALIAS_CORE1)
static void R_QogAliasFastVerts (finalvert_t *fv, stvert_t *pstverts, int clip)
{
	r_qogfastjob_t	job0;

	job0.xf = r_qogxfp;
	job0.fv = fv;
	job0.pverts = r_apverts;
	job0.pstverts = pstverts;
	job0.first = 0;
	job0.count = r_anumverts;
	job0.step = 1;
	job0.clip = clip;
#ifdef QOG_ALIAS_CORE1
	if (r_anumverts >= QOG_ALIAS_CORE1_MIN)
	{
		r_qogfastjob_t	job1;

		job1 = job0;
		job1.first = 1;
		job0.step = job1.step = 2;
		Sys_QogCore1Start (R_QogAliasFastCore1, &job1);
		R_QogAliasFastRange (&job0);
		Sys_QogCore1Join ();
		return;
	}
#endif
	R_QogAliasFastRange (&job0);
}

/*
================
R_QogAliasSphereOut

QuakeOG (Agent AI): Fast mode. True when the model's bounding sphere
(boundingradius about the entity's origin, which holds every frame, with a
quarter more and 8 units of margin) is wholly behind the eye or wholly
outside a side of the view, so its transform is never built. The side
planes' lengths are taken a little long, which only keeps more models.
================
*/
static QOG_ANOINLINE qboolean R_QogAliasSphereOut (void)
{
	float	x, y, z, r, hw, hh;

	r = pmdl->boundingradius * 1.25f + 8.0f;
	z = -DotProduct (modelorg, vpn);
	if (z < -r)
		return true;
	x = -DotProduct (modelorg, vright);
	y = -DotProduct (modelorg, vup);
	hw = r_refdef.vrect.width * 0.5f + 2.0f;
	hh = r_refdef.vrect.height * 0.5f + 2.0f;
	r *= 1.0f + (hw > hh ? hw : hh) / (xscale < yscale ? xscale : yscale);
	if (x * xscale - z * hw > r * xscale || -x * xscale - z * hw > r * xscale)
		return true;
	if (y * yscale - z * hh > r * yscale || -y * yscale - z * hh > r * yscale)
		return true;
	return false;
}

/*
================
R_QogAliasCheckBBoxFast

QuakeOG (Agent AI): R_AliasCheckBBox in Fast mode. The frame's box corners
go through the same integer transform as the vertices. A box with a corner
behind the near plane is drawn clipped, without id's rejection test. The
box is rejected only when every corner is more than a pixel off one side,
and accepted unclipped only when every corner is a pixel inside, which
covers the rounding of the integer projection.
================
*/
static QOG_ANOINLINE qboolean R_QogAliasCheckBBoxFast (maliasframedesc_t *pframedesc)
{
	r_qogxf_t	xf;
	int			i, x, y, z, z8, u, v, flags, anyclip, allclip, minz;
	int			b[2][3];

	R_QogAliasXform (&xf);
	for (i=0 ; i<3 ; i++)
	{
		b[0][i] = pframedesc->bboxmin.v[i];
		b[1][i] = pframedesc->bboxmax.v[i];
	}

	anyclip = 0;
	allclip = ALIAS_XY_CLIP_MASK;
	minz = 0x7fffffff;
	for (i=0 ; i<8 ; i++)
	{
		int		bx = b[i & 1][0], by = b[(i >> 1) & 1][1], bz = b[i >> 2][2];

		z = xf.m[2][0] * bx + xf.m[2][1] * by + xf.m[2][2] * bz + xf.m[2][3];
		if (z < ALIAS_Z_CLIP_PLANE * 0x10000)
			return true;	// near the eye: drawn clipped, never rejected here
		if (z < minz)
			minz = z;
		x = (xf.m[0][0] * bx + xf.m[0][1] * by + xf.m[0][2] * bz + xf.m[0][3]) >> 8;
		y = (xf.m[1][0] * bx + xf.m[1][1] * by + xf.m[1][2] * bz + xf.m[1][3]) >> 8;
		z8 = z >> 8;
		u = R_QOGPROJECT (x, z8, xf.xs, xf.xc, xf.xlim, aliasxscale, aliasxcenter);
		v = R_QOGPROJECT (y, z8, xf.ys, xf.yc, xf.ylim, aliasyscale, aliasycenter);

		flags = 0;
		if (u < r_refdef.aliasvrect.x - 1)
			flags |= ALIAS_LEFT_CLIP;
		if (v < r_refdef.aliasvrect.y - 1)
			flags |= ALIAS_TOP_CLIP;
		if (u > r_refdef.aliasvrectright + 1)
			flags |= ALIAS_RIGHT_CLIP;
		if (v > r_refdef.aliasvrectbottom + 1)
			flags |= ALIAS_BOTTOM_CLIP;
		allclip &= flags;

		if (u < r_refdef.aliasvrect.x + 1 || v < r_refdef.aliasvrect.y + 1 ||
			u > r_refdef.aliasvrectright - 1 || v > r_refdef.aliasvrectbottom - 1)
			anyclip = 1;
	}

	if (allclip)
		return false;	// trivial reject off one side

	currententity->trivial_accept = !anyclip;
	if (currententity->trivial_accept &&
		(float)(minz >> 16) > (r_aliastransition + (pmdl->size * r_resfudge)))
		currententity->trivial_accept |= 2;

	return true;
}
#endif

void R_AliasPreparePoints (void)
{
	int			i;
	stvert_t	*pstverts;
	finalvert_t	*fv;
	auxvert_t	*av;
	mtriangle_t	*ptri;
	finalvert_t	*pfv[3];
#ifdef QOG_ALIAS_NO_AUXVERTS
	auxvert_t	avone;	// QuakeOG: one vertex at a time, see r_local.h
#endif

	pstverts = (stvert_t *)((byte *)paliashdr + paliashdr->stverts);
	r_anumverts = pmdl->numverts;
 	fv = pfinalverts;
	QOG_AST(QAS_VERTS, r_anumverts);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
	{
		// QuakeOG (Agent AI): Fast mode vertices, then id's triangles
#ifdef QOG_ALIAS_NO_AUXVERTS
		r_qog_apverts = r_apverts;
#endif
		R_QogAliasFastVerts (pfinalverts, pstverts, 1);
		r_apverts += r_anumverts;
		goto qog_drawtris;
	}
#endif
#ifdef QOG_ALIAS_CORE1
#ifdef QOG_ALIAS_NO_AUXVERTS
	r_qog_apverts = r_apverts;
#endif
	if (r_anumverts >= QOG_ALIAS_CORE1_MIN)
	{
		// QuakeOG (Agent Z): odd vertices on core 1, even ones here
		r_qogaliasjob_t	job0, job1;

		job0.fv = job1.fv = pfinalverts;
		job0.pverts = job1.pverts = r_apverts;
		job0.pstverts = job1.pstverts = pstverts;
		job0.count = job1.count = r_anumverts;
		job0.first = 0;
		job1.first = 1;
		Sys_QogCore1Start (R_QogAliasPrepareCore1, &job1);
		R_QogAliasPrepareRange (&job0);
		Sys_QogCore1Join ();
		r_apverts += r_anumverts;	// where id's loop leaves it
		(void)av;
	}
	else
#endif
#ifdef QOG_ALIAS_NO_AUXVERTS
	{
	av = &avone;
#ifndef QOG_ALIAS_CORE1
	r_qog_apverts = r_apverts;
#endif

	for (i=0 ; i<r_anumverts ; i++, fv++, r_apverts++, pstverts++)
#else
	{
	av = pauxverts;

	for (i=0 ; i<r_anumverts ; i++, fv++, av++, r_apverts++, pstverts++)
#endif
	{
		R_AliasTransformFinalVert (fv, av, r_apverts, pstverts);
		if (av->fv[2] < ALIAS_Z_CLIP_PLANE)
			fv->flags |= ALIAS_Z_CLIP;
		else
		{
			 R_AliasProjectFinalVert (fv, av);

			if (fv->v[0] < r_refdef.aliasvrect.x)
				fv->flags |= ALIAS_LEFT_CLIP;
			if (fv->v[1] < r_refdef.aliasvrect.y)
				fv->flags |= ALIAS_TOP_CLIP;
			if (fv->v[0] > r_refdef.aliasvrectright)
				fv->flags |= ALIAS_RIGHT_CLIP;
			if (fv->v[1] > r_refdef.aliasvrectbottom)
				fv->flags |= ALIAS_BOTTOM_CLIP;	
		}
	}
	}

#ifdef QOG_ALIAS_FAST
qog_drawtris:
#endif
//
// clip and draw all triangles
//
	r_affinetridesc.numtriangles = 1;

	ptri = (mtriangle_t *)ALIAS_BULK (paliashdr, paliashdr->triangles);
	for (i=0 ; i<pmdl->numtris ; i++, ptri++)
	{
		pfv[0] = &pfinalverts[ptri->vertindex[0]];
		pfv[1] = &pfinalverts[ptri->vertindex[1]];
		pfv[2] = &pfinalverts[ptri->vertindex[2]];

		if ( pfv[0]->flags & pfv[1]->flags & pfv[2]->flags & (ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP) )
			continue;		// completely clipped
		
		if ( ! ( (pfv[0]->flags | pfv[1]->flags | pfv[2]->flags) &
			(ALIAS_XY_CLIP_MASK | ALIAS_Z_CLIP) ) )
		{	// totally unclipped
			r_affinetridesc.pfinalverts = pfinalverts;
			r_affinetridesc.ptriangles = ptri;
			D_PolysetDraw ();
		}
		else		
		{	// partially clipped
			QOG_AST(QAS_CLIPTRI, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS
			R_AliasClipTriangle (ptri);
		}
	}
}


/*
================
R_AliasSetUpTransform
================
*/
void R_AliasSetUpTransform (int trivial_accept)
{
	int				i;
	float			rotationmatrix[3][4], t2matrix[3][4];
	// QuakeOG (Agent AI): id's tmatrix and viewmatrix were static only for
	// their zeros, locals with the same zeros take no static RAM
	float			tmatrix[3][4] = {{0}};
	float			viewmatrix[3][4] = {{0}};
	vec3_t			angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity

	angles[ROLL] = currententity->angles[ROLL];
	angles[PITCH] = -currententity->angles[PITCH];
	angles[YAW] = currententity->angles[YAW];
	AngleVectors (angles, alias_forward, alias_right, alias_up);

	tmatrix[0][0] = pmdl->scale[0];
	tmatrix[1][1] = pmdl->scale[1];
	tmatrix[2][2] = pmdl->scale[2];

	tmatrix[0][3] = pmdl->scale_origin[0];
	tmatrix[1][3] = pmdl->scale_origin[1];
	tmatrix[2][3] = pmdl->scale_origin[2];

// TODO: can do this with simple matrix rearrangement

	for (i=0 ; i<3 ; i++)
	{
		t2matrix[i][0] = alias_forward[i];
		t2matrix[i][1] = -alias_right[i];
		t2matrix[i][2] = alias_up[i];
	}

	t2matrix[0][3] = -modelorg[0];
	t2matrix[1][3] = -modelorg[1];
	t2matrix[2][3] = -modelorg[2];

// FIXME: can do more efficiently than full concatenation
	R_ConcatTransforms (t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
	VectorCopy (vright, viewmatrix[0]);
	VectorCopy (vup, viewmatrix[1]);
	VectorInverse (viewmatrix[1]);
	VectorCopy (vpn, viewmatrix[2]);

//	viewmatrix[0][3] = 0;
//	viewmatrix[1][3] = 0;
//	viewmatrix[2][3] = 0;

	R_ConcatTransforms (viewmatrix, rotationmatrix, aliastransform);

// do the scaling up of x and y to screen coordinates as part of the transform
// for the unclipped case (it would mess up clipping in the clipped case).
// Also scale down z, so 1/z is scaled 31 bits for free, and scale down x and y
// correspondingly so the projected x and y come out right
// FIXME: make this work for clipped case too?
	if (trivial_accept)
	{
		for (i=0 ; i<4 ; i++)
		{
			aliastransform[0][i] *= aliasxscale *
					(1.0 / ((float)0x8000 * 0x10000));
			aliastransform[1][i] *= aliasyscale *
					(1.0 / ((float)0x8000 * 0x10000));
			aliastransform[2][i] *= 1.0 / ((float)0x8000 * 0x10000);

		}
	}
}


/*
================
R_AliasTransformFinalVert
================
*/
#ifdef QOG_ALIAS_NO_AUXVERTS
trivertx_t	*r_qog_apverts;

// QuakeOG: id's view-space transform from R_AliasTransformFinalVert, shared
// with the triangle clipper (r_local.h)
void R_AliasTransformAuxVert (auxvert_t *av, trivertx_t *pverts)
{
	av->fv[0] = DotProduct(pverts->v, aliastransform[0]) +
			aliastransform[0][3];
	av->fv[1] = DotProduct(pverts->v, aliastransform[1]) +
			aliastransform[1][3];
	av->fv[2] = DotProduct(pverts->v, aliastransform[2]) +
			aliastransform[2][3];
}
#endif

void R_AliasTransformFinalVert (finalvert_t *fv, auxvert_t *av,
	trivertx_t *pverts, stvert_t *pstverts)
{
	int		temp;
	float	lightcos;
	QOG_RTABLE_CONST float	*plightnormal;

#ifdef QOG_ALIAS_NO_AUXVERTS
	R_AliasTransformAuxVert (av, pverts);
#else
	av->fv[0] = DotProduct(pverts->v, aliastransform[0]) +
			aliastransform[0][3];
	av->fv[1] = DotProduct(pverts->v, aliastransform[1]) +
			aliastransform[1][3];
	av->fv[2] = DotProduct(pverts->v, aliastransform[2]) +
			aliastransform[2][3];
#endif

	fv->v[2] = pstverts->s;
	fv->v[3] = pstverts->t;

	fv->flags = pstverts->onseam;

// lighting
	plightnormal = r_avertexnormals[pverts->lightnormalindex];
	lightcos = DotProduct (plightnormal, r_plightvec);
	temp = r_ambientlight;

	if (lightcos < 0)
	{
		temp += (int)(r_shadelight * lightcos);

	// clamp; because we limited the minimum ambient and shading light, we
	// don't have to clamp low light, just bright
		if (temp < 0)
			temp = 0;
	}

	fv->v[4] = temp;
}


#if	!id386

/*
================
R_AliasTransformAndProjectFinalVerts
================
*/
#ifdef QOG_ALIAS_CORE1
// QuakeOG (Agent Z): id's loop below for every second vertex (see
// R_QogAliasPrepareRange), the same statements
static void R_QogAliasProjectRange (r_qogaliasjob_t *job)
{
	finalvert_t	*fv;
	stvert_t	*pstverts;
	int			i, temp;
	float		lightcos, zi;
	QOG_RTABLE_CONST float	*plightnormal;
	trivertx_t	*pverts;

	for (i = job->first ; i < job->count ; i += 2)
	{
		fv = job->fv + i;
		pverts = job->pverts + i;
		pstverts = job->pstverts + i;

	// transform and project
		zi = 1.0 / (DotProduct(pverts->v, aliastransform[2]) +
				aliastransform[2][3]);

	// x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
	// scaled up by 1/2**31, and the scaling cancels out for x and y in the
	// projection
		fv->v[5] = zi;

		fv->v[0] = ((DotProduct(pverts->v, aliastransform[0]) +
				aliastransform[0][3]) * zi) + aliasxcenter;
		fv->v[1] = ((DotProduct(pverts->v, aliastransform[1]) +
				aliastransform[1][3]) * zi) + aliasycenter;

		fv->v[2] = pstverts->s;
		fv->v[3] = pstverts->t;
		fv->flags = pstverts->onseam;

	// lighting
		plightnormal = r_avertexnormals[pverts->lightnormalindex];
		lightcos = DotProduct (plightnormal, r_plightvec);
		temp = r_ambientlight;

		if (lightcos < 0)
		{
			temp += (int)(r_shadelight * lightcos);

		// clamp; because we limited the minimum ambient and shading light, we
		// don't have to clamp low light, just bright
			if (temp < 0)
				temp = 0;
		}

		fv->v[4] = temp;
	}
}


static void R_QogAliasProjectCore1 (void *job)
{
	R_QogAliasProjectRange ((r_qogaliasjob_t *)job);
}
#endif

void R_AliasTransformAndProjectFinalVerts (finalvert_t *fv, stvert_t *pstverts)
{
	int			i, temp;
	float		lightcos, zi;
	QOG_RTABLE_CONST float	*plightnormal;
	trivertx_t	*pverts;

#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
	{
		R_QogAliasFastVerts (fv, pstverts, 0);	// QuakeOG (Agent AI): Fast mode
		return;
	}
#endif
#ifdef QOG_ALIAS_CORE1
	if (r_anumverts >= QOG_ALIAS_CORE1_MIN)
	{
		// QuakeOG (Agent Z): odd vertices on core 1, even ones here
		r_qogaliasjob_t	job0, job1;

		job0.fv = job1.fv = fv;
		job0.pverts = job1.pverts = r_apverts;
		job0.pstverts = job1.pstverts = pstverts;
		job0.count = job1.count = r_anumverts;
		job0.first = 0;
		job1.first = 1;
		Sys_QogCore1Start (R_QogAliasProjectCore1, &job1);
		R_QogAliasProjectRange (&job0);
		Sys_QogCore1Join ();
		return;
	}
#endif

	pverts = r_apverts;

	for (i=0 ; i<r_anumverts ; i++, fv++, pverts++, pstverts++)
	{
	// transform and project
		zi = QOG_FRECIP (DotProduct(pverts->v, aliastransform[2]) +
				aliastransform[2][3]);	// QuakeOG: QOG_FLOAT_EXACT

	// x, y, and z are scaled down by 1/2**31 in the transform, so 1/z is
	// scaled up by 1/2**31, and the scaling cancels out for x and y in the
	// projection
		fv->v[5] = zi;

		fv->v[0] = ((DotProduct(pverts->v, aliastransform[0]) +
				aliastransform[0][3]) * zi) + aliasxcenter;
		fv->v[1] = ((DotProduct(pverts->v, aliastransform[1]) +
				aliastransform[1][3]) * zi) + aliasycenter;

		fv->v[2] = pstverts->s;
		fv->v[3] = pstverts->t;
		fv->flags = pstverts->onseam;

	// lighting
		plightnormal = r_avertexnormals[pverts->lightnormalindex];
		lightcos = DotProduct (plightnormal, r_plightvec);
		temp = r_ambientlight;

		if (lightcos < 0)
		{
			temp += (int)(r_shadelight * lightcos);

		// clamp; because we limited the minimum ambient and shading light, we
		// don't have to clamp low light, just bright
			if (temp < 0)
				temp = 0;
		}

		fv->v[4] = temp;
	}
}

#endif


/*
================
R_AliasProjectFinalVert
================
*/
void R_AliasProjectFinalVert (finalvert_t *fv, auxvert_t *av)
{
	float	zi;

// project points
	zi = QOG_FRECIP (av->fv[2]);	// QuakeOG: QOG_FLOAT_EXACT

	fv->v[5] = zi * ziscale;

	fv->v[0] = (av->fv[0] * aliasxscale * zi) + aliasxcenter;
	fv->v[1] = (av->fv[1] * aliasyscale * zi) + aliasycenter;
}


/*
================
R_AliasPrepareUnclippedPoints
================
*/
void R_AliasPrepareUnclippedPoints (void)
{
	stvert_t	*pstverts;
	finalvert_t	*fv;

	pstverts = (stvert_t *)((byte *)paliashdr + paliashdr->stverts);
	r_anumverts = pmdl->numverts;
// FIXME: just use pfinalverts directly?
	fv = pfinalverts;
	QOG_AST(QAS_VERTS, r_anumverts);	// QuakeOG (Agent AI): QOG_ALIAS_STATS

	R_AliasTransformAndProjectFinalVerts (fv, pstverts);

	if (r_affinetridesc.drawtype)
		D_PolysetDrawFinalVerts (fv, r_anumverts);

	r_affinetridesc.pfinalverts = pfinalverts;
	r_affinetridesc.ptriangles = (mtriangle_t *)
			ALIAS_BULK (paliashdr, paliashdr->triangles);
	r_affinetridesc.numtriangles = pmdl->numtris;

	D_PolysetDraw ();
}

/*
===============
R_AliasSetupSkin
===============
*/
void R_AliasSetupSkin (void)
{
	int					skinnum;
	int					i, numskins;
	maliasskingroup_t	*paliasskingroup;
	float				*pskinintervals, fullskininterval;
	float				skintargettime, skintime;

	skinnum = currententity->skinnum;
	if ((skinnum >= pmdl->numskins) || (skinnum < 0))
	{
		Con_DPrintf ("R_AliasSetupSkin: no such skin # %d\n", skinnum);
		skinnum = 0;
	}

	pskindesc = ((maliasskindesc_t *)
			((byte *)paliashdr + paliashdr->skindesc)) + skinnum;
	a_skinwidth = pmdl->skinwidth;

	if (pskindesc->type == ALIAS_SKIN_GROUP)
	{
		paliasskingroup = (maliasskingroup_t *)((byte *)paliashdr +
				pskindesc->skin);
		pskinintervals = (float *)
				((byte *)paliashdr + paliasskingroup->intervals);
		numskins = paliasskingroup->numskins;
		fullskininterval = pskinintervals[numskins-1];
	
		skintime = cl.time + ENT_SYNCBASE (currententity);	// QuakeOG: render.h
	
	// when loading in Mod_LoadAliasSkinGroup, we guaranteed all interval
	// values are positive, so we don't have to worry about division by 0
		skintargettime = skintime -
				((int)(skintime / fullskininterval)) * fullskininterval;
	
		for (i=0 ; i<(numskins-1) ; i++)
		{
			if (pskinintervals[i] > skintargettime)
				break;
		}
	
		pskindesc = &paliasskingroup->skindescs[i];
	}

	r_affinetridesc.pskindesc = pskindesc;
	r_affinetridesc.pskin = (void *)ALIAS_BULK (paliashdr, pskindesc->skin);
	r_affinetridesc.skinwidth = a_skinwidth;
	r_affinetridesc.seamfixupX16 =  (a_skinwidth >> 1) << 16;
	r_affinetridesc.skinheight = pmdl->skinheight;
}

/*
================
R_AliasSetupLighting
================
*/
void R_AliasSetupLighting (alight_t *plighting)
{

// guarantee that no vertex will ever be lit below LIGHT_MIN, so we don't have
// to clamp off the bottom
	r_ambientlight = plighting->ambientlight;

	if (r_ambientlight < LIGHT_MIN)
		r_ambientlight = LIGHT_MIN;

	r_ambientlight = (255 - r_ambientlight) << VID_CBITS;

	if (r_ambientlight < LIGHT_MIN)
		r_ambientlight = LIGHT_MIN;

	r_shadelight = plighting->shadelight;

	if (r_shadelight < 0)
		r_shadelight = 0;

	r_shadelight *= VID_GRADES;

#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
	{
		// QuakeOG (Agent AI): Fast mode, the same rotation in 2.14
		r_qogxf_t	*xf = r_qogxfp;
		int			l0, l1, l2;

		l0 = R_QogFix (plighting->plightvec[0], 14);
		l1 = R_QogFix (plighting->plightvec[1], 14);
		l2 = R_QogFix (plighting->plightvec[2], 14);
		xf->lv[0] = (l0 * xf->fwd[0] + l1 * xf->fwd[1] + l2 * xf->fwd[2]) >> 14;
		xf->lv[1] = -((l0 * xf->rt[0] + l1 * xf->rt[1] + l2 * xf->rt[2]) >> 14);
		xf->lv[2] = (l0 * xf->up[0] + l1 * xf->up[1] + l2 * xf->up[2]) >> 14;
		xf->shade = (int)r_shadelight;
		return;
	}
#endif
// rotate the lighting vector into the model's frame of reference
	r_plightvec[0] = DotProduct (plighting->plightvec, alias_forward);
	r_plightvec[1] = -DotProduct (plighting->plightvec, alias_right);
	r_plightvec[2] = DotProduct (plighting->plightvec, alias_up);
}

/*
=================
R_AliasSetupFrame

set r_apverts
=================
*/
void R_AliasSetupFrame (void)
{
	int				frame;
	int				i, numframes;
	maliasgroup_t	*paliasgroup;
	float			*pintervals, fullinterval, targettime, time;

	frame = currententity->frame;
	if ((frame >= pmdl->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d\n", frame);
		frame = 0;
	}

	if (paliashdr->frames[frame].type == ALIAS_SINGLE)
	{
		r_apverts = (trivertx_t *)
				ALIAS_BULK (paliashdr, paliashdr->frames[frame].frame);
		return;
	}
	
	paliasgroup = (maliasgroup_t *)
				((byte *)paliashdr + paliashdr->frames[frame].frame);
	pintervals = (float *)((byte *)paliashdr + paliasgroup->intervals);
	numframes = paliasgroup->numframes;
	fullinterval = pintervals[numframes-1];

	time = cl.time + ENT_SYNCBASE (currententity);	// QuakeOG: render.h

//
// when loading in Mod_LoadAliasGroup, we guaranteed all interval values
// are positive, so we don't have to worry about division by 0
//
	targettime = time - ((int)(time / fullinterval)) * fullinterval;

	for (i=0 ; i<(numframes-1) ; i++)
	{
		if (pintervals[i] > targettime)
			break;
	}

	r_apverts = (trivertx_t *)
				ALIAS_BULK (paliashdr, paliasgroup->frames[i].frame);
}


#ifdef QOG_TRANSLATE_ONDRAW
#if defined(__GNUC__)
#define QOG_NOINLINE	__attribute__((noinline))
#elif defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE
#endif
/*
================
R_AliasDrawTranslated

QuakeOG: an entity in player colours (client.h). The colour table id kept
per player is built here for this draw, in a stack frame of its own that
sits beside R_EdgeDrawing's larger one, not under it.
================
*/
#ifdef QOG_TRANSLATE_INLINE
/*
QuakeOG (Agent S): id's translated colour map is the colour map with the
two player colour ranges of each row taken from other columns
(CL_BuildTranslation), so table[row*256 + texel] is
colormap[row*256 + map[texel]] for a 256-byte map, which the polyset
drawers apply to each texel (d_atranslate, d_polyse.c). Before the first
colour update the table was all zeros: every texel then maps to a column
that is 0 in every row (colour 0, checked).
*/
extern byte	*d_atranslate;

static QOG_NOINLINE void R_AliasDrawTranslated (int slot)
{
	byte	map[256];
	int		i, j, top, bottom;

	if (!cl.scores[slot].translated)
	{
		for (j=0 ; j<VID_GRADES ; j++)
			if (vid.colormap[j*256])
				Sys_Error ("R_AliasDrawTranslated: colour map column 0 is not black");
		memset (map, 0, sizeof(map));
	}
	else
	{
		for (i=0 ; i<256 ; i++)
			map[i] = i;
		top = cl.scores[slot].colors & 0xf0;
		bottom = (cl.scores[slot].colors &15)<<4;
		for (j=0 ; j<16 ; j++)
		{
			map[TOP_RANGE+j] = top < 128 ? top+j : top+15-j;
			map[BOTTOM_RANGE+j] = bottom < 128 ? bottom+j : bottom+15-j;
		}
	}
	acolormap = vid.colormap;
	d_atranslate = map;
	if (currententity->trivial_accept)
		R_AliasPrepareUnclippedPoints ();
	else
		R_AliasPreparePoints ();
	d_atranslate = NULL;
}
#else
static QOG_NOINLINE void R_AliasDrawTranslated (int slot)
{
	byte	table[VID_GRADES*256];

	CL_BuildTranslation (table, slot);
	acolormap = table;
	if (currententity->trivial_accept)
		R_AliasPrepareUnclippedPoints ();
	else
		R_AliasPreparePoints ();
}
#endif
#endif

/*
================
R_AliasDrawModel
================
*/
void R_AliasDrawModel (alight_t *plighting)
{
	finalvert_t		finalverts[MAXALIASVERTS +
						((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 1];
#ifndef QOG_ALIAS_NO_AUXVERTS
	auxvert_t		auxverts[MAXALIASVERTS];
#endif
#ifdef QOG_ALIAS_FAST
	r_qogxf_t		qogxf;	// QuakeOG (Agent AI): Fast mode transform
#endif

	r_amodels_drawn++;
#ifdef QOG_ALIAS_STATS
	qog_astat_vm = currententity == &cl.viewent;
	QOG_AST(QAS_DRAWN, 1);
	QOG_AST(QAS_TRIVIAL, currententity->trivial_accept != 0);
	QOG_AST(QAS_SUBDIVM, (currententity->trivial_accept == 3) && r_recursiveaffinetriangles);
#endif

// cache align
	pfinalverts = (finalvert_t *)
			(((long)&finalverts[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));
#ifdef QOG_ALIAS_NO_AUXVERTS
	pauxverts = NULL;	// QuakeOG: recomputed when clipping, see r_local.h
#else
	pauxverts = &auxverts[0];
#endif

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);
	pmdl = (mdl_t *)((byte *)paliashdr + paliashdr->model);

	R_AliasSetupSkin ();
#ifdef QOG_ALIAS_FAST
	if (r_qogfast)
	{
		// QuakeOG (Agent AI): Fast mode, the integer transform
		R_QogAliasXform (&qogxf);
		r_qogxfp = &qogxf;
		if (!currententity->trivial_accept)
			R_QogAliasFloatXform (&qogxf);
	}
	else
#endif
	R_AliasSetUpTransform (currententity->trivial_accept);
	R_AliasSetupLighting (plighting);
	R_AliasSetupFrame ();

	if (!currententity->colormap)
		Sys_Error ("R_AliasDrawModel: !currententity->colormap");

	r_affinetridesc.drawtype = (currententity->trivial_accept == 3) &&
			r_recursiveaffinetriangles;

	if (r_affinetridesc.drawtype)
	{
		D_PolysetUpdateTables ();		// FIXME: precalc...
	}
	else
	{
#if	id386
		D_Aff8Patch (currententity->colormap);
#endif
	}

	acolormap = currententity->colormap;

	if (currententity != &cl.viewent)
		ziscale = (float)0x8000 * (float)0x10000;
	else
		ziscale = (float)0x8000 * (float)0x10000 * 3.0;

#ifdef QOG_TRANSLATE_ONDRAW
	if (CL_TranslationSlot (currententity->colormap) >= 0)
	{
		R_AliasDrawTranslated (CL_TranslationSlot (currententity->colormap));
		return;
	}
#endif
	if (currententity->trivial_accept)
		R_AliasPrepareUnclippedPoints ();
	else
		R_AliasPreparePoints ();
}

