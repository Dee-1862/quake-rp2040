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
// r_bsp.c

#include "quakedef.h"
#include "r_local.h"
#include "qog_phase.h"	// QuakeOG (Agent Z): world work counts (QOG_PHASE_XIP)
#include "qog_bmodel.h"	// QuakeOG (Agent AS): board cost model counts (QOG_BOARDMODEL)
#include "qog_aqstats.h"	// QuakeOG (Agent AQ): front end work counts (QOG_AQ_STATS)

//
// current entity info
//
qboolean		insubmodel;
entity_t		*currententity;
vec3_t			modelorg, base_modelorg;
								// modelorg is the viewpoint reletive to
								// the currently rendering entity
vec3_t			r_entorigin;	// the currently rendering entity in world
								// coordinates

float			entity_rotation[3][3];

vec3_t			r_worldmodelorg;

int				r_currentbkey;

typedef enum {touchessolid, drawnode, nodrawnode} solidstate_t;

// QuakeOG: these hold one brush entity polygon after clipping against the
// world, and are reset for every polygon. Shareware peaks are 22 vertices
// and 74 edges over all three demos (qog_mem prints them). Overflow only
// drops that fragment with a console message, as in id's code.
// (MAX_BMODEL_VERTS and MAX_BMODEL_EDGES are in r_local.h, QuakeOG)
static mvertex_t	*pbverts;
static bedge_t		*pbedges;
static int			numbverts, numbedges;
int					r_qog_peakbverts, r_qog_peakbedges;	// QuakeOG: high-water marks, printed by qog_mem

static mvertex_t	*pfrontenter, *pfrontexit;

static qboolean		makeclippededge;


//===========================================================================

/*
================
R_EntityRotate
================
*/
void R_EntityRotate (vec3_t vec)
{
	vec3_t	tvec;

	VectorCopy (vec, tvec);
	vec[0] = DotProduct (entity_rotation[0], tvec);
	vec[1] = DotProduct (entity_rotation[1], tvec);
	vec[2] = DotProduct (entity_rotation[2], tvec);
}


/*
================
R_RotateBmodel
================
*/
#ifdef QOG_BMODEL_ZEROANGLE
/*
QuakeOG (Agent AJ): a brush entity that does not turn (doors, plats,
buttons, nearly all of them) has zero angles, and sin (+-0) is +-0 and
cos (+-0) is 1 exactly, so the angle conversion and the double sine and
cosine are left out for a zero angle. The board pays about 5,000 cycles
for each axis otherwise, twice for every brush entity drawn and twice more
for each of its surfaces in D_DrawSurfaces.
*/
#define QOG_ROT_SINCOS(a)												\
	if ((a) == 0)														\
	{																	\
		s = (a);														\
		c = 1;															\
	}																	\
	else																\
	{																	\
		QOG_WC (QOG_WC_ROTTRIG);										\
		angle = (a);													\
		angle = angle * M_PI*2 / 360;									\
		s = sin(angle);													\
		c = cos(angle);													\
	}

/*
QuakeOG (Agent AJ): with all three angles +0 the code below builds
entity_rotation as the identity with every zero +0 (worked through by
hand: each entry is a sum of products of 1, +0 and -0 that rounds to 1 or
+0). R_EntityRotate by that matrix computes, for each component,
(t0 * r0 + t1 * r1) + t2 * r2 with one r 1 and two r +0: a nonzero t
comes out unchanged, a zero comes out -0 only if all three products are
-0, that is if all three components have the sign bit set. Infinities or
NaNs would make NaNs from 0 * t, so such a vector takes id's dot products.
*/
static const float	r_qog_identity[3][3] = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };
#if defined(QOG_BPOLY_AXIAL) || defined(QOG_DSURF_NOFRUSTUM)
static byte			r_qog_entident;	// QuakeOG (Agent AQ): entity_rotation is r_qog_identity
#endif
#if defined(QOG_DSURF_NOFRUSTUM) || defined(QOG_BMODEL_FRUSTUM)
static byte			r_qog_nofrustum;	// QuakeOG (Agent AQ): R_QogRotateBmodelVecs
#endif

static void R_QogIdentityRotate (vec3_t vec)
{
	unsigned	b[3];
	int			i;

	memcpy (b, vec, sizeof b);
	for (i=0 ; i<3 ; i++)
	{
		unsigned	e = b[i] & 0x7F800000;

		if (e == 0x7F800000 || (e == 0 && (b[i] & 0x007FFFFF)))
		{
			// not finite, or denormal (which the RP2040's float routines
			// take as zero): id's arithmetic
			R_EntityRotate (vec);
			return;
		}
	}
	for (i=0 ; i<3 ; i++)
	{
		if (b[i] == 0x80000000 && !(b[0] & b[1] & b[2] & 0x80000000))
			vec[i] = 0;	// -0 plus a +0 product is +0
	}
}
#endif

void R_RotateBmodel (void)
{
	float	angle, s, c, temp1[3][3], temp2[3][3], temp3[3][3];

// TODO: should use a look-up table
// TODO: should really be stored with the entity instead of being reconstructed
// TODO: could cache lazily, stored in the entity
// TODO: share work with R_SetUpAliasTransform

	QOG_WC (QOG_WC_ROTATE);	// QuakeOG (Agent AJ)
#ifdef QOG_BMODEL_ZEROANGLE
	{
		unsigned	ab[3];

#if defined(QOG_BPOLY_AXIAL) || defined(QOG_DSURF_NOFRUSTUM)
		r_qog_entident = false;
#endif
		memcpy (ab, currententity->angles, sizeof ab);
		if (!(ab[0] | ab[1] | ab[2]))
		{
			// QuakeOG (Agent AJ): no turn, the same values without the
			// matrix products
			memcpy (entity_rotation, r_qog_identity, sizeof entity_rotation);
#if defined(QOG_BPOLY_AXIAL) || defined(QOG_DSURF_NOFRUSTUM)
			r_qog_entident = true;
#endif
			R_QogIdentityRotate (modelorg);
			R_QogIdentityRotate (vpn);
			R_QogIdentityRotate (vright);
			R_QogIdentityRotate (vup);
#if defined(QOG_DSURF_NOFRUSTUM) || defined(QOG_BMODEL_FRUSTUM)
			if (!r_qog_nofrustum)
#endif
			R_TransformFrustum ();
			return;
		}
	}
#endif
// yaw
#ifdef QOG_BMODEL_ZEROANGLE
	QOG_ROT_SINCOS (currententity->angles[YAW])
#else
	angle = currententity->angles[YAW];
	angle = angle * M_PI*2 / 360;
	s = sin(angle);
	c = cos(angle);
#endif

	temp1[0][0] = c;
	temp1[0][1] = s;
	temp1[0][2] = 0;
	temp1[1][0] = -s;
	temp1[1][1] = c;
	temp1[1][2] = 0;
	temp1[2][0] = 0;
	temp1[2][1] = 0;
	temp1[2][2] = 1;


// pitch
#ifdef QOG_BMODEL_ZEROANGLE
	QOG_ROT_SINCOS (currententity->angles[PITCH])
#else
	angle = currententity->angles[PITCH];
	angle = angle * M_PI*2 / 360;
	s = sin(angle);
	c = cos(angle);
#endif

	temp2[0][0] = c;
	temp2[0][1] = 0;
	temp2[0][2] = -s;
	temp2[1][0] = 0;
	temp2[1][1] = 1;
	temp2[1][2] = 0;
	temp2[2][0] = s;
	temp2[2][1] = 0;
	temp2[2][2] = c;

	R_ConcatRotations (temp2, temp1, temp3);

// roll
#ifdef QOG_BMODEL_ZEROANGLE
	QOG_ROT_SINCOS (currententity->angles[ROLL])
#else
	angle = currententity->angles[ROLL];
	angle = angle * M_PI*2 / 360;
	s = sin(angle);
	c = cos(angle);
#endif

	temp1[0][0] = 1;
	temp1[0][1] = 0;
	temp1[0][2] = 0;
	temp1[1][0] = 0;
	temp1[1][1] = c;
	temp1[1][2] = s;
	temp1[2][0] = 0;
	temp1[2][1] = -s;
	temp1[2][2] = c;

	R_ConcatRotations (temp1, temp3, entity_rotation);

//
// rotate modelorg and the transformation matrix
//
	R_EntityRotate (modelorg);
	R_EntityRotate (vpn);
	R_EntityRotate (vright);
	R_EntityRotate (vup);

#if defined(QOG_DSURF_NOFRUSTUM) || defined(QOG_BMODEL_FRUSTUM)
	if (!r_qog_nofrustum)
#endif
	R_TransformFrustum ();
}

#if defined(QOG_DSURF_NOFRUSTUM) || defined(QOG_BMODEL_FRUSTUM)
/*
QuakeOG (Agent AQ): R_RotateBmodel without the view planes. D_DrawSurfaces
needs the turned vectors and modelorg but never the view planes: nothing
that draws spans reads view_clipplanes, so D_DrawSurfaces leaves them alone
and sets them back once at its end (d_edge.c). R_DrawBEntitiesOnList makes
the planes itself from what it made for the entity before (r_main.c).
*/
void R_QogRotateBmodelVecs (void)
{
	r_qog_nofrustum = true;
	R_RotateBmodel ();
	r_qog_nofrustum = false;
}
#endif


#if defined(QOG_BPOLY_AXIAL) || defined(QOG_BMODEL_FACING)
#ifndef QOG_BMODEL_ZEROANGLE
#error "QOG_BPOLY_AXIAL and QOG_BMODEL_FACING need QOG_BMODEL_ZEROANGLE"
#endif
/*
QuakeOG (Agent AQ): R_RecursiveClipBPoly on an axial plane of a brush
entity that does not turn, without float arithmetic for the sides.

With entity_rotation the identity, tplane's normal is id's identity product
of the node plane's normal (R_QogIdentityRotate), so on an axial plane it
has one component +-1 and two +-0. id's DotProduct of a finite point p with
such a normal n is then exactly p[k] n[k] when p[k] is not zero (the
products with +-0 are zeros, which leave a sum unchanged), else a zero that
is -0 only when all three products are -0 (round to nearest adds zeros that
way). R_QogAxDot builds those bits, so the dot product needs no float call.
A point's dist is the float difference q - tplane.dist, and dist > 0 holds
exactly when q > tplane.dist as real numbers, which an integer compare of
the bits decides (both zeros as 0). Points with a component that is not
finite or denormal, a p[k] below 2^-100 or a tplane.dist below 2^-100 but
not zero take id's floats (no difference can then flush to zero on the
board's float routines). The dists of a split edge are id's values, q -
tplane.dist in one float subtraction.
*/
// a float's bits (may alias a float, so no memcpy call on the board)
#if defined(__GNUC__)
typedef unsigned int __attribute__((may_alias))	r_qogbits_t;
#else
typedef unsigned int							r_qogbits_t;
#endif
#define QOG_FB(f)	(*(r_qogbits_t *)&(f))

/* id's DotProduct (v, n) for an axial n (component k), or 0 when v needs
   id's arithmetic */
static int R_QogAxDot (const float *v, const float *n, int k, float *out)
{
	const r_qogbits_t	*b = (const r_qogbits_t *)v, *nb = (const r_qogbits_t *)n;
	unsigned			e, q;
	int					i;

	for (i=0 ; i<3 ; i++)
	{
		e = b[i] & 0x7F800000;
		if (e == 0x7F800000 || (e == 0 && (b[i] & 0x007FFFFF)))
			return 0;	// not finite, or denormal
	}
	if (b[k] & 0x7FFFFFFF)
	{
		if ((b[k] & 0x7F800000) < (27u << 23))
			return 0;	// below 2^-100
		q = b[k] ^ (nb[k] & 0x80000000);
	}
	else
		q = ((b[0] ^ nb[0]) & (b[1] ^ nb[1]) & (b[2] ^ nb[2])) & 0x80000000;
	*(r_qogbits_t *)out = q;
	return 1;
}

/* a float's bits as an int in the order of the real numbers (+-0 as 0) */
static int R_QogFKey (unsigned b)
{
	return (b & 0x80000000) ? -(int)(b & 0x7FFFFFFF) : (int)b;
}

/* the axis of a normal with one component +-1 and two +-0, else -1 */
static int R_QogAxis (const float *n)
{
	const r_qogbits_t	*nb = (const r_qogbits_t *)n;

	if ((nb[0] & 0x7FFFFFFF) == 0x3F800000 && !((nb[1] | nb[2]) & 0x7FFFFFFF))
		return 0;
	if ((nb[1] & 0x7FFFFFFF) == 0x3F800000 && !((nb[0] | nb[2]) & 0x7FFFFFFF))
		return 1;
	if ((nb[2] & 0x7FFFFFFF) == 0x3F800000 && !((nb[0] | nb[1]) & 0x7FFFFFFF))
		return 2;
	return -1;
}

#ifdef QOG_BMODEL_FACING
#ifdef QOG_BMODEL_FACING_CHECK
/* PC check build: both ways on every brush surface */
static void R_QogFacingCheck (int a, int b)
{
	static unsigned	n, bad;

	n++;
	if (a != b)
		bad++;
	if ((n & 0x3FFF) == 0)
		Sys_Printf ("qog_facingcheck %u surfaces, %u differ\n", n, bad);
}
#endif
/*
QuakeOG (Agent AQ): whether a brush entity surface faces the eye, as id
decides it: dot = DotProduct (modelorg, normal) - dist in float, compared
in double with -BACKFACE_EPSILON or BACKFACE_EPSILON. For a float x, x <
-0.01 and x > 0.01 are x < -0.01f and x > 0.01f (0.01f is the float just
below 0.01), so the compare needs no double. On an axial plane the dot
product is R_QogAxDot's exact value, one float subtraction in all.
*/
static int R_QogFacing (msurface_t *psurf, mplane_t *pplane)
{
	float	dot;
	int		k = R_QogAxis (pplane->normal);

	if (k < 0 || !R_QogAxDot (modelorg, pplane->normal, k, &dot))
		dot = DotProduct (modelorg, pplane->normal);
	dot -= pplane->dist;
	if (psurf->flags & SURF_PLANEBACK)
		return dot < -0.01f;
	return dot > 0.01f;
}
#endif

#ifdef QOG_BPOLY_AXIAL
/*
tplane of an axial node plane for an entity that does not turn, with id's
values and the axis in its type. Returns the axis, or -1 for id's
arithmetic (tplane then unset).
*/
static int R_QogAxPlane (mplane_t *splitplane, mplane_t *tp)
{
	unsigned	db;
	int			k;
	float		dot;

	if (!r_qog_entident)
		return -1;
	VectorCopy (splitplane->normal, tp->normal);
	R_QogIdentityRotate (tp->normal);	// id's DotProduct (entity_rotation[i], normal)
	k = R_QogAxis (tp->normal);
	if (k < 0)
		return -1;
	// the node plane's own normal is the same up to the signs of its zeros
	if (!R_QogAxDot (r_entorigin, splitplane->normal, k, &dot))
		return -1;
	tp->dist = splitplane->dist - dot;
	db = QOG_FB (tp->dist);
	if ((db & 0x7F800000) == 0x7F800000 || ((db & 0x7FFFFFFF) && (db & 0x7F800000) < (27u << 23)))
		return -1;
	tp->type = (byte)k;
	return k;
}

#ifdef QOG_BPOLY_SIDES
/*
QuakeOG (Agent AQ): the sides of one call's edges without a call per point.
R_QogAxSidesBegin keeps the axis, the sign of tplane's normal there and
tplane.dist's key in statics, R_QogAxEdgeSides decides a point as
R_QogAxSide does (the same tests, inline), and a point that ends or starts
the edge before has the side it got then (the same point against the same
plane gives the same side, and no recursion runs inside one call's edge
loop, so the statics belong to that loop).
*/
static mplane_t		*r_qog_axtp;
static mvertex_t	*r_qog_axv0, *r_qog_axv1;	// the edge before
static byte			r_qog_axs0, r_qog_axs1, r_qog_axk;
static unsigned		r_qog_axsign;
static int			r_qog_axtkey;

static int R_QogAxSide (mvertex_t *pv, mplane_t *tp, float *pdist);

static void R_QogAxSidesBegin (mplane_t *tp)
{
	r_qog_axtp = tp;
	r_qog_axk = tp->type;
	r_qog_axsign = QOG_FB (tp->normal[tp->type]) & 0x80000000;
	r_qog_axtkey = R_QogFKey (QOG_FB (tp->dist));
	r_qog_axv0 = r_qog_axv1 = NULL;
}

static int R_QogAxPointSide (mvertex_t *pv)
{
	const r_qogbits_t	*b = (const r_qogbits_t *)pv->position;
	unsigned			e0 = b[0] & 0x7F800000, e1 = b[1] & 0x7F800000, e2 = b[2] & 0x7F800000, bk;

	if (pv == r_qog_axv1)
		return r_qog_axs1;
	if (pv == r_qog_axv0)
		return r_qog_axs0;
	bk = b[r_qog_axk];
	if (e0 == 0x7F800000 || e1 == 0x7F800000 || e2 == 0x7F800000 ||
		(!e0 && (b[0] & 0x007FFFFF)) || (!e1 && (b[1] & 0x007FFFFF)) || (!e2 && (b[2] & 0x007FFFFF)) ||
		((bk & 0x7FFFFFFF) && (bk & 0x7F800000) < (27u << 23)))
		return R_QogAxSide (pv, r_qog_axtp, NULL);	// id's arithmetic
	if (!(bk & 0x7FFFFFFF))
		return 0 > r_qog_axtkey ? 0 : 1;
	return R_QogFKey (bk ^ r_qog_axsign) > r_qog_axtkey ? 0 : 1;
}

/* the sides of an edge: bit 0 its first point's, bit 1 its second's */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int R_QogAxEdgeSides (mvertex_t *pv0, mvertex_t *pv1)
{
	int		s0 = R_QogAxPointSide (pv0);
	int		s1 = R_QogAxPointSide (pv1);

	r_qog_axv0 = pv0;
	r_qog_axs0 = (byte)s0;
	r_qog_axv1 = pv1;
	r_qog_axs1 = (byte)s1;
	return s0 | (s1 << 1);
}
#endif

/* id's side of a point (0 when dist > 0), and its dist when asked */
static int R_QogAxSide (mvertex_t *pv, mplane_t *tp, float *pdist)
{
	float	q, d;

	if (!R_QogAxDot (pv->position, tp->normal, tp->type, &q))
	{
		QOG_AQ (AQ_BPOLYFB, 1);
		d = DotProduct (pv->position, tp->normal) - tp->dist;	// id's arithmetic
		if (pdist)
			*pdist = d;
		return d > 0 ? 0 : 1;
	}
	if (pdist)
		*pdist = q - tp->dist;
	return R_QogFKey (QOG_FB (q)) > R_QogFKey (QOG_FB (tp->dist)) ? 0 : 1;
}
#endif
#endif

#ifdef QOG_BPOLY_CHECK
/* PC check build: id's tplane, sides and dists beside the axial ones */
static unsigned	r_qog_bpn, r_qog_bpbad;

static void R_QogBpolyCount (int bad)
{
	r_qog_bpn++;
	if (bad)
		r_qog_bpbad++;
	if ((r_qog_bpn & 0xFFFF) == 0)
		Sys_Printf ("qog_bpolycheck %u tests, %u differ\n", r_qog_bpn, r_qog_bpbad);
}

static void R_QogBpolyCheckPlane (mplane_t *splitplane, mplane_t *tp, int axk)
{
	mplane_t	t;

	if (axk < 0)
		return;
	t.dist = splitplane->dist - DotProduct(r_entorigin, splitplane->normal);
	t.normal[0] = DotProduct (entity_rotation[0], splitplane->normal);
	t.normal[1] = DotProduct (entity_rotation[1], splitplane->normal);
	t.normal[2] = DotProduct (entity_rotation[2], splitplane->normal);
	R_QogBpolyCount (memcmp (&t.dist, &tp->dist, sizeof t.dist) || memcmp (t.normal, tp->normal, sizeof t.normal));
}

static void R_QogBpolyCheckSide (mvertex_t *pv, mplane_t *tp, int side)
{
	float	d = DotProduct (pv->position, tp->normal) - tp->dist;

	R_QogBpolyCount ((d > 0 ? 0 : 1) != side);
}

static void R_QogBpolyCheckDist (mvertex_t *pv, mplane_t *tp, float dist)
{
	float	d = DotProduct (pv->position, tp->normal) - tp->dist;

	R_QogBpolyCount (memcmp (&d, &dist, sizeof d) != 0);
}
#endif

/*
================
R_RecursiveClipBPoly
================
*/
void R_RecursiveClipBPoly (bedge_t *pedges, mnode_t *pnode, msurface_t *psurf)
{
	bedge_t		*psideedges[2], *pnextedge, *ptedge;
	int			i, side, lastside;
	float		dist, frac, lastdist;
	mplane_t	*splitplane, tplane;
	mvertex_t	*pvert, *plastvert, *ptvert;
	mnode_t		*pn;

	psideedges[0] = psideedges[1] = NULL;
	QOG_BM (QOG_BM_BPOLY, 1);	// QuakeOG (Agent AS)
	QOG_AQ (AQ_BPOLY, 1);

	makeclippededge = false;

// transform the BSP plane into model space
// FIXME: cache these?
	splitplane = pnode->plane;
	QOG_AQ (AQ_BPOLYAX, splitplane->type < 3);
#ifdef QOG_BPOLY_AXIAL
	// QuakeOG (Agent AQ): tplane.type holds the axis, or 0xFF for id's
	// arithmetic (no more locals in this recursion's frame)
	tplane.type = 0xFF;
	if (R_QogAxPlane (splitplane, &tplane) < 0)
#endif
	{
	tplane.dist = splitplane->dist -
			DotProduct(r_entorigin, splitplane->normal);
	tplane.normal[0] = DotProduct (entity_rotation[0], splitplane->normal);
	tplane.normal[1] = DotProduct (entity_rotation[1], splitplane->normal);
	tplane.normal[2] = DotProduct (entity_rotation[2], splitplane->normal);
	}
#ifdef QOG_BPOLY_CHECK
	R_QogBpolyCheckPlane (splitplane, &tplane, tplane.type == 0xFF ? -1 : 0);
#endif
#ifdef QOG_BPOLY_SIDES
	if (tplane.type != 0xFF)
		R_QogAxSidesBegin (&tplane);
#endif

// clip edges to BSP plane
	for ( ; pedges ; pedges = pnextedge)
	{
		pnextedge = pedges->pnext;
		QOG_BM (QOG_BM_BPOLYEDGE, 1);	// QuakeOG (Agent AS)
		QOG_AQ (AQ_BPOLYEDGE, 1);

	// set the status for the last point as the previous point
	// FIXME: cache this stuff somehow?
		plastvert = pedges->v[0];
		pvert = pedges->v[1];
#ifdef QOG_BPOLY_AXIAL
		if (tplane.type != 0xFF)
		{
			// QuakeOG (Agent AQ): the sides by integer compares, the dists
			// only for a split edge
#ifdef QOG_BPOLY_SIDES
			side = R_QogAxEdgeSides (plastvert, pvert);
			lastside = side & 1;
			side >>= 1;
#else
			lastside = R_QogAxSide (plastvert, &tplane, NULL);
			side = R_QogAxSide (pvert, &tplane, NULL);
#endif
#ifdef QOG_BPOLY_CHECK
			R_QogBpolyCheckSide (plastvert, &tplane, lastside);
			R_QogBpolyCheckSide (pvert, &tplane, side);
#endif
		}
		else
#endif
		{
		lastdist = DotProduct (plastvert->position, tplane.normal) -
				   tplane.dist;

		if (lastdist > 0)
			lastside = 0;
		else
			lastside = 1;

		dist = DotProduct (pvert->position, tplane.normal) - tplane.dist;

		if (dist > 0)
			side = 0;
		else
			side = 1;
		}

		if (side != lastside)
		{
		// clipped
			QOG_AQ (AQ_BPOLYSPLIT, 1);
			if (numbverts >= MAX_BMODEL_VERTS)
				return;
#ifdef QOG_BPOLY_AXIAL
			if (tplane.type != 0xFF)
			{
				R_QogAxSide (plastvert, &tplane, &lastdist);
				R_QogAxSide (pvert, &tplane, &dist);
#ifdef QOG_BPOLY_CHECK
				R_QogBpolyCheckDist (plastvert, &tplane, lastdist);
				R_QogBpolyCheckDist (pvert, &tplane, dist);
#endif
			}
#endif

		// generate the clipped vertex
			frac = lastdist / (lastdist - dist);
			ptvert = &pbverts[numbverts++];
			ptvert->position[0] = plastvert->position[0] +
					frac * (pvert->position[0] -
					plastvert->position[0]);
			ptvert->position[1] = plastvert->position[1] +
					frac * (pvert->position[1] -
					plastvert->position[1]);
			ptvert->position[2] = plastvert->position[2] +
					frac * (pvert->position[2] -
					plastvert->position[2]);

		// split into two edges, one on each side, and remember entering
		// and exiting points
		// FIXME: share the clip edge by having a winding direction flag?
			if (numbedges >= (MAX_BMODEL_EDGES - 1))
			{
				CON_PRINTTEXT ("Out of edges for bmodel\n");	// QuakeOG: no 4 KB buffer this deep
				return;
			}

			ptedge = &pbedges[numbedges];
			ptedge->pnext = psideedges[lastside];
			psideedges[lastside] = ptedge;
			ptedge->v[0] = plastvert;
			ptedge->v[1] = ptvert;

			ptedge = &pbedges[numbedges + 1];
			ptedge->pnext = psideedges[side];
			psideedges[side] = ptedge;
			ptedge->v[0] = ptvert;
			ptedge->v[1] = pvert;

			numbedges += 2;

			if (side == 0)
			{
			// entering for front, exiting for back
				pfrontenter = ptvert;
				makeclippededge = true;
			}
			else
			{
				pfrontexit = ptvert;
				makeclippededge = true;
			}
		}
		else
		{
		// add the edge to the appropriate side
			pedges->pnext = psideedges[side];
			psideedges[side] = pedges;
		}
	}

// if anything was clipped, reconstitute and add the edges along the clip
// plane to both sides (but in opposite directions)
	if (makeclippededge)
	{
		if (numbedges >= (MAX_BMODEL_EDGES - 2))
		{
			CON_PRINTTEXT ("Out of edges for bmodel\n");	// QuakeOG: no 4 KB buffer this deep
			return;
		}

		ptedge = &pbedges[numbedges];
		ptedge->pnext = psideedges[0];
		psideedges[0] = ptedge;
		ptedge->v[0] = pfrontexit;
		ptedge->v[1] = pfrontenter;

		ptedge = &pbedges[numbedges + 1];
		ptedge->pnext = psideedges[1];
		psideedges[1] = ptedge;
		ptedge->v[0] = pfrontenter;
		ptedge->v[1] = pfrontexit;

		numbedges += 2;
	}

// draw or recurse further
	for (i=0 ; i<2 ; i++)
	{
		if (psideedges[i])
		{
		// draw if we've reached a non-solid leaf, done if all that's left is a
		// solid leaf, and continue down the tree if it's not a leaf
			pn = pnode->children[i];

		// we're done with this branch if the node or leaf isn't in the PVS
			if (NODE_VISFRAME_IS (cl.worldmodel, pn, r_visframecount))	// QuakeOG
			{
				if (pn->contents < 0)
				{
					if (pn->contents != CONTENTS_SOLID)
					{
						r_currentbkey = LEAF_KEY (cl.worldmodel, (mleaf_t *)pn);
						R_RenderBmodelFace (psideedges[i], psurf);
					}
				}
				else
				{
					R_RecursiveClipBPoly (psideedges[i], pnode->children[i],
									  psurf);
				}
			}
		}
	}
}


/*
================
R_DrawSolidClippedSubmodelPolygons
================
*/
void R_DrawSolidClippedSubmodelPolygons (model_t *pmodel)
{
	int			i, j, lindex;
	vec_t		dot;
	msurface_t	*psurf;
	int			numsurfaces;
	mplane_t	*pplane;
#if defined(QOG_EDGES_IN_ZBUFFER)
	// QuakeOG: the clip buffers sit at the start of the z-buffer, which
	// holds no z yet. With QOG_EDGECACHE_ZBUFFER the world's edge cache was
	// there, which nothing reads after R_RenderWorld (R_EdgeDrawing, R_NewMap
	// checks the room).
	mvertex_t	*bverts = (mvertex_t *)r_ztemp_base;
	bedge_t		*bedges = (bedge_t *)(bverts + MAX_BMODEL_VERTS), *pbedge;
#elif defined(QOG_SPANS_IN_EDGES)
	// QuakeOG: the clip buffers sit in the edge table past the edge limit,
	// which only R_ScanEdges uses later (r_local.h)
	mvertex_t	*bverts = (mvertex_t *)(r_spanpool_end - SPAN_RESERVE_EDGES * sizeof(edge_t));
	bedge_t		*bedges = (bedge_t *)(bverts + MAX_BMODEL_VERTS), *pbedge;
#else
	mvertex_t	bverts[MAX_BMODEL_VERTS];
	bedge_t		bedges[MAX_BMODEL_EDGES], *pbedge;
#endif
	medge_t		*pedge, *pedges;

// FIXME: use bounding-box-based frustum clipping info?

	psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
	numsurfaces = pmodel->nummodelsurfaces;
	pedges = pmodel->edges;

	for (i=0 ; i<numsurfaces ; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = SURF_PLANE (pmodel, psurf);	// QuakeOG

		QOG_AQ (AQ_BSURFS, 1);
#ifdef QOG_BMODEL_FACING
#ifdef QOG_BMODEL_FACING_CHECK
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		R_QogFacingCheck (R_QogFacing (psurf, pplane), ((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)));
#endif
		if (R_QogFacing (psurf, pplane))	// QuakeOG (Agent AQ)
#else
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
#endif
		{
			QOG_AQ (AQ_BFRONT, 1);
		// FIXME: use bounding-box-based frustum clipping info?

		// copy the edges to bedges, flipping if necessary so always
		// clockwise winding
		// FIXME: if edges and vertices get caches, these assignments must move
		// outside the loop, and overflow checking must be done here
			pbverts = bverts;
			pbedges = bedges;
			numbverts = numbedges = 0;

			if (psurf->numedges > 0)
			{
				pbedge = &bedges[numbedges];
				numbedges += psurf->numedges;

				for (j=0 ; j<psurf->numedges ; j++)
				{
				   lindex = pmodel->surfedges[psurf->firstedge+j];

					if (lindex > 0)
					{
						pedge = &pedges[lindex];
						pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[0]];
						pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[1]];
					}
					else
					{
						lindex = -lindex;
						pedge = &pedges[lindex];
						pbedge[j].v[0] = &r_pcurrentvertbase[pedge->v[1]];
						pbedge[j].v[1] = &r_pcurrentvertbase[pedge->v[0]];
					}

					pbedge[j].pnext = &pbedge[j+1];
				}

				pbedge[j-1].pnext = NULL;	// mark end of edges

				R_RecursiveClipBPoly (pbedge, ENT_TOPNODE (currententity), psurf);

				// QuakeOG: high-water marks, to size bverts and bedges from measurements
				if (numbverts > r_qog_peakbverts)
					r_qog_peakbverts = numbverts;
				if (numbedges > r_qog_peakbedges)
					r_qog_peakbedges = numbedges;
			}
			else
			{
				Sys_Error ("no edges in bmodel");
			}
		}
	}
}


/*
================
R_DrawSubmodelPolygons
================
*/
void R_DrawSubmodelPolygons (model_t *pmodel, int clipflags)
{
	int			i;
	vec_t		dot;
	msurface_t	*psurf;
	int			numsurfaces;
	mplane_t	*pplane;

// FIXME: use bounding-box-based frustum clipping info?

	psurf = &pmodel->surfaces[pmodel->firstmodelsurface];
	numsurfaces = pmodel->nummodelsurfaces;

	for (i=0 ; i<numsurfaces ; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = SURF_PLANE (pmodel, psurf);	// QuakeOG

		QOG_AQ (AQ_BSURFS, 1);
#ifdef QOG_BMODEL_FACING
#ifdef QOG_BMODEL_FACING_CHECK
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;
		R_QogFacingCheck (R_QogFacing (psurf, pplane), ((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)));
#endif
		if (R_QogFacing (psurf, pplane))	// QuakeOG (Agent AQ)
#else
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
#endif
		{
			QOG_AQ (AQ_BFRONT, 1);
			r_currentkey = LEAF_KEY (cl.worldmodel, (mleaf_t *)ENT_TOPNODE (currententity));

		// FIXME: use bounding-box-based frustum clipping info?
			R_RenderFace (psurf, clipflags);
		}
	}
}


#ifdef QOG_WORLDNODE_FAST
/*
================
R_QogCullNode

QuakeOG (Agent Z): R_RecursiveWorldNode's frustum tests without double
arithmetic. id's d holds the float dot product, and (double)dot -
(double)dist is exact in sign and zero only when the two are equal, so
d <= 0 and d >= 0 are the float comparisons dot <= dist and dot >= dist.
The box corners are converted to float once per node, not once per plane.
Returns the new clip flags, or -1 when the node is outside a plane. Not
inlined, so its locals are not in every level of the recursion.
================
*/
#if defined(__GNUC__)
#define QOG_NOINLINE	__attribute__((noinline))
#elif defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE
#endif
#ifdef QOG_WORLDNODE_INT
#ifndef QOG_FRONT_FAST
#error "QOG_WORLDNODE_INT needs QOG_FRONT_FAST (the planes live in its header)"
#endif
/*
QuakeOG (Agent AJ): the same tests in integers first. With the corners c
(shorts), the plane's normal N and dist D in 1/16384 (rounded, R_QogFxSetup)
the integer d = c.N - D is within 0.5 (|c0| + |c1| + |c2|) + 0.5 of
16384 (c.n - dist), and id's float dot product within 0.003 (|c0| + |c1| +
|c2|) of the real one, so where |d| reaches e = |c0| + |c1| + |c2| + 512 its
sign is the sign of id's float test. Only boxes closer than that to a plane
(a fraction of a unit near the view, about e / 16384 units) take id's floats.
*/
static int R_QogCullFloat (mnode_t *node, int i, int *pindex)
{
	vec3_t	pt;

	QOG_AQ (AQ_CULLFLT, 1);
	pt[0] = (float)node->minmaxs[pindex[0]];
	pt[1] = (float)node->minmaxs[pindex[1]];
	pt[2] = (float)node->minmaxs[pindex[2]];
	return DotProduct (pt, view_clipplanes[i].normal) <= view_clipplanes[i].dist;
}

static QOG_NOINLINE int R_QogCullNodeInt (mnode_t *node, int clipflags, const int *pl)
{
	int		i, *pindex, c0, c1, c2, d, e;
	float	dot;
	vec3_t	pt;

	for (i=0 ; i<4 ; i++, pl += 4)
	{
		if (! (clipflags & (1<<i)) )
			continue;	// don't need to clip against it

		pindex = pfrustum_indexes[i];

		// reject: id's dot (rejectpt) <= dist
		c0 = node->minmaxs[pindex[0]];
		c1 = node->minmaxs[pindex[1]];
		c2 = node->minmaxs[pindex[2]];
		d = pl[0] * c0 + pl[1] * c1 + pl[2] * c2 - pl[3];
		e = (c0 < 0 ? -c0 : c0) + (c1 < 0 ? -c1 : c1) + (c2 < 0 ? -c2 : c2) + 512;
		if (d <= -e)
			return -1;
		if (d < e && R_QogCullFloat (node, i, pindex))
			return -1;

		// accept: id's dot (acceptpt) >= dist
		c0 = node->minmaxs[pindex[3+0]];
		c1 = node->minmaxs[pindex[3+1]];
		c2 = node->minmaxs[pindex[3+2]];
		d = pl[0] * c0 + pl[1] * c1 + pl[2] * c2 - pl[3];
		e = (c0 < 0 ? -c0 : c0) + (c1 < 0 ? -c1 : c1) + (c2 < 0 ? -c2 : c2) + 512;
		if (d >= e)
			clipflags &= ~(1<<i);	// node is entirely on screen
		else if (d > -e)
		{
			QOG_AQ (AQ_CULLFLT, 1);
			pt[0] = (float)c0;
			pt[1] = (float)c1;
			pt[2] = (float)c2;
			dot = DotProduct (pt, view_clipplanes[i].normal);
			if (dot >= view_clipplanes[i].dist)
				clipflags &= ~(1<<i);
		}
	}
	return clipflags;
}
#endif

static QOG_NOINLINE int R_QogCullNode (mnode_t *node, int clipflags)
{
	int		i, *pindex;
	float	mm[6];
	vec3_t	acceptpt, rejectpt;

#ifdef QOG_WORLDNODE_INT
	const int	*pl = R_QogFxPlanes ();

	if (pl)
		return R_QogCullNodeInt (node, clipflags, pl);
#endif
	for (i=0 ; i<6 ; i++)
		mm[i] = (float)node->minmaxs[i];

	for (i=0 ; i<4 ; i++)
	{
		if (! (clipflags & (1<<i)) )
			continue;	// don't need to clip against it

		pindex = pfrustum_indexes[i];

		rejectpt[0] = mm[pindex[0]];
		rejectpt[1] = mm[pindex[1]];
		rejectpt[2] = mm[pindex[2]];

		if (DotProduct (rejectpt, view_clipplanes[i].normal) <= view_clipplanes[i].dist)
			return -1;

		acceptpt[0] = mm[pindex[3+0]];
		acceptpt[1] = mm[pindex[3+1]];
		acceptpt[2] = mm[pindex[3+2]];

		if (DotProduct (acceptpt, view_clipplanes[i].normal) >= view_clipplanes[i].dist)
			clipflags &= ~(1<<i);	// node is entirely on screen
	}
	return clipflags;
}
#endif

#if defined(QOG_NODESIDE_BITS) && defined(QOG_WORLDNODE_FAST)
/*
QuakeOG (Agent AQ): R_RecursiveWorldNode's three float compares of fdot (a
finite float difference, computed here as there) by its bits: fdot >= 0 for +-0 and every positive
value, and |fdot| > 0.01f exactly when its magnitude's bits are above
0.01f's. No float call. Not inlined, so the recursion's frame keeps its size.
Returns side + 2 (facing + 1).
*/
static QOG_NOINLINE int R_QogNodeSide (mplane_t *plane)
{
	float		fdot;
	unsigned	fb, mag;
	float		eps = 0.01f;
	unsigned	eb = *(unsigned *)&eps;
	int			side, facing;

	// id's fdot, as R_RecursiveWorldNode computes it
	QOG_AQ (AQ_NODEAX, plane->type < 3);
	QOG_AQ (AQ_NODEDOT, plane->type >= 3);
	switch (plane->type)
	{
	case PLANE_X:
		fdot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		fdot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		fdot = modelorg[2] - plane->dist;
		break;
	default:
		fdot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}
	fb = *(unsigned *)&fdot;
	mag = fb & 0x7FFFFFFF;
	side = (fb & 0x80000000) && mag ? 1 : 0;
	facing = mag > eb ? ((fb & 0x80000000) ? -1 : 1) : 0;
#ifdef QOG_NODESIDE_CHECK
	{
		static unsigned	n, bad;

		n++;
		if (side != (fdot >= 0 ? 0 : 1) || facing != (fdot < -0.01f ? -1 : (fdot > 0.01f ? 1 : 0)))
			bad++;
		if ((n & 0xFFFF) == 0)
			Sys_Printf ("qog_nodesidecheck %u nodes, %u differ%c", n, bad, 10);
	}
#endif
	return side + 2 * (facing + 1);
}
#endif

/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node, int clipflags)
{
	int			i, c, side, *pindex;
	vec3_t		acceptpt, rejectpt;
	mplane_t	*plane;
	msurface_t	*surf;
	marksurf_t	*mark;	// QuakeOG: a pointer or a 16-bit surface number
	mleaf_t		*pleaf;
#ifdef QOG_FLOAT_EXACT
	// QuakeOG (Agent AA): id's doubles here only ever hold a float expression
	// or the difference of two floats, and are only compared with 0 or with
	// BACKFACE_EPSILON. A float difference rounds to the same sign and zero,
	// and no float lies between 0.01 and (float)0.01, so every test is the same.
	float		d, dot;
#define QOG_BFEPS	((float)BACKFACE_EPSILON)
#else
	double		d, dot;
#define QOG_BFEPS	BACKFACE_EPSILON
#endif
#ifdef QOG_WORLDNODE_FAST
	int			facing;	// QuakeOG (Agent Z): -1 back, 1 front, 0 neither
#endif

	QOG_WC (QOG_WC_NODES);	// QuakeOG (Agent Z)
	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (!NODE_VISFRAME_IS (cl.worldmodel, node, r_visframecount))	// QuakeOG
		return;

// cull the clipping planes if not trivial accept
// FIXME: the compiler is doing a lousy job of optimizing here; it could be
//  twice as fast in ASM
#ifdef QOG_WORLDNODE_FAST
	if (clipflags)
	{
		clipflags = R_QogCullNode (node, clipflags);	// QuakeOG (Agent Z)
		if (clipflags < 0)
			return;
	}
#else
	if (clipflags)
	{
		for (i=0 ; i<4 ; i++)
		{
			if (! (clipflags & (1<<i)) )
				continue;	// don't need to clip against it

		// generate accept and reject points
		// FIXME: do with fast look-ups or integer tests based on the sign bit
		// of the floating point values

			pindex = pfrustum_indexes[i];

			rejectpt[0] = (float)node->minmaxs[pindex[0]];
			rejectpt[1] = (float)node->minmaxs[pindex[1]];
			rejectpt[2] = (float)node->minmaxs[pindex[2]];
			
			d = DotProduct (rejectpt, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d <= 0)
				return;

			acceptpt[0] = (float)node->minmaxs[pindex[3+0]];
			acceptpt[1] = (float)node->minmaxs[pindex[3+1]];
			acceptpt[2] = (float)node->minmaxs[pindex[3+2]];

			d = DotProduct (acceptpt, view_clipplanes[i].normal);
			d -= view_clipplanes[i].dist;

			if (d >= 0)
				clipflags &= ~(1<<i);	// node is entirely on screen
		}
	}
#endif
	
// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;
		QOG_WC (QOG_WC_LEAFS);	// QuakeOG (Agent Z)

		mark = LEAF_MARKSURFACES (cl.worldmodel, pleaf);	// QuakeOG
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				SURF_VISFRAME_SETNUM (cl.worldmodel, MARKSURF_NUM (cl.worldmodel, mark), r_framecount);	// QuakeOG: MODFRAME, QOG_MOD_FLASH, QOG_FRAMEBITS
				mark++;
			} while (--c);
		}

	// deal with model fragments in this leaf
		if (LEAF_EFRAGS (cl.worldmodel, pleaf))
		{
#ifdef QOG_LEAF16
			R_StoreLeafEfrags (pleaf);	// QuakeOG: the leaf's head may be shared
#else
			R_StoreEfrags (&LEAF_EFRAGS (cl.worldmodel, pleaf));
#endif
		}

		LEAF_KEY_SET (cl.worldmodel, pleaf, r_currentkey);	// QuakeOG: QOG_LEAF16
		r_currentkey++;		// all bmodels in a leaf share the same key
	}
	else
	{
	// node is just a decision point, so go down the apropriate sides

	// find which side of the node we are on
		plane = node->plane;

#ifdef QOG_WORLDNODE_FAST
	// QuakeOG (Agent Z): id's dot is a double holding a float difference,
	// compared with 0 and with the double 0.01. For a float x, x < -0.01 and
	// x > 0.01 are x < -0.01f and x > 0.01f (0.01f is the float just below
	// 0.01), so the side and the face tests need no double arithmetic.
		{
#ifdef QOG_NODESIDE_BITS
		side = R_QogNodeSide (plane);	// QuakeOG (Agent AQ)
		facing = (side >> 1) - 1;
		side &= 1;
#else
		float	fdot;

		QOG_AQ (AQ_NODEAX, plane->type < 3);
		QOG_AQ (AQ_NODEDOT, plane->type >= 3);
		switch (plane->type)
		{
		case PLANE_X:
			fdot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			fdot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			fdot = modelorg[2] - plane->dist;
			break;
		default:
			fdot = DotProduct (modelorg, plane->normal) - plane->dist;
			break;
		}

		side = fdot >= 0 ? 0 : 1;
		facing = fdot < -0.01f ? -1 : (fdot > 0.01f ? 1 : 0);	// id's dot < -BACKFACE_EPSILON and dot > BACKFACE_EPSILON
#endif
		}
#else
		switch (plane->type)
		{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct (modelorg, plane->normal) - plane->dist;
			break;
		}
	
		if (dot >= 0)
			side = 0;
		else
			side = 1;
#endif

	// recurse down the children, front side first
		R_RecursiveWorldNode (node->children[side], clipflags);

	// draw stuff
		c = node->numsurfaces;

		if (c)
		{
			surf = cl.worldmodel->surfaces + node->firstsurface;

#ifdef QOG_WORLDNODE_FAST
			if (facing < 0)
#else
			if (dot < -QOG_BFEPS)
#endif
			{
				do
				{
					if ((surf->flags & SURF_PLANEBACK) &&
						SURF_VISFRAME_IS (cl.worldmodel, surf, r_framecount))	// QuakeOG
					{
						if (r_drawpolys)
						{
							if (r_worldpolysbacktofront)
							{
								if (numbtofpolys < MAX_BTOFPOLYS)
								{
									pbtofpolys[numbtofpolys].clipflags =
											clipflags;
									pbtofpolys[numbtofpolys].psurf = surf;
									numbtofpolys++;
								}
							}
							else
							{
								R_RenderPoly (surf, clipflags);
							}
						}
						else
						{
							R_RenderFace (surf, clipflags);
						}
					}

					surf++;
				} while (--c);
			}
#ifdef QOG_WORLDNODE_FAST
			else if (facing > 0)
#else
			else if (dot > QOG_BFEPS)
#endif
			{
				do
				{
					if (!(surf->flags & SURF_PLANEBACK) &&
						SURF_VISFRAME_IS (cl.worldmodel, surf, r_framecount))	// QuakeOG
					{
						if (r_drawpolys)
						{
							if (r_worldpolysbacktofront)
							{
								if (numbtofpolys < MAX_BTOFPOLYS)
								{
									pbtofpolys[numbtofpolys].clipflags =
											clipflags;
									pbtofpolys[numbtofpolys].psurf = surf;
									numbtofpolys++;
								}
							}
							else
							{
								R_RenderPoly (surf, clipflags);
							}
						}
						else
						{
							R_RenderFace (surf, clipflags);
						}
					}

					surf++;
				} while (--c);
			}

		// all surfaces on the same node share the same sequence number
			r_currentkey++;
		}

	// recurse down the back side
		R_RecursiveWorldNode (node->children[!side], clipflags);
	}
}



/*
================
R_RenderWorld
================
*/
void R_RenderWorld (void)
{
	int			i;
	model_t		*clmodel;
#if MAX_BTOFPOLYS > 0
	btofpoly_t	btofpolys[MAX_BTOFPOLYS];

	pbtofpolys = btofpolys;
#else
	// QuakeOG: back to front list compiled out (r_local.h)
	if (r_worldpolysbacktofront)
		Sys_Error ("R_RenderWorld: back to front polygons need QOG_MAX_BTOFPOLYS");
	pbtofpolys = NULL;
#endif

	currententity = &cl_entities[0];
	VectorCopy (r_origin, modelorg);
	clmodel = currententity->model;
	r_pcurrentvertbase = clmodel->vertexes;
#ifdef QOG_FRONT_FAST
	R_QogFxSetup ();	// QuakeOG (Agent AJ): Fast mode's integer front end
#endif

	R_RecursiveWorldNode (clmodel->nodes, 15);

// if the driver wants the polygons back to front, play the visible ones back
// in that order
	if (r_worldpolysbacktofront)
	{
		for (i=numbtofpolys-1 ; i>=0 ; i--)
		{
			R_RenderPoly (pbtofpolys[i].psurf, pbtofpolys[i].clipflags);	// QuakeOG: was btofpolys
		}
	}
}


