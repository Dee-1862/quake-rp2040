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
// d_part.c: software driver module for drawing particles

#include "quakedef.h"
#include "d_local.h"
#include "r_qogalias.h"	// QuakeOG (Agent AI)


/*
==============
D_EndParticles
==============
*/
void D_EndParticles (void)
{
// not used by software driver
}


/*
==============
D_StartParticles
==============
*/
void D_StartParticles (void)
{
// not used by software driver
}


#if	!id386

/*
==============
D_ParticlePixels

QuakeOG (Agent AI): id's pixel loops of D_DrawParticle, shared with the
Fast mode drawer. izi is the depth as the z-buffer holds it.
==============
*/
static void D_ParticlePixels (byte *pdest, d_zval_t *pz, int izi, int pix,
	int color)
{
	int		i, count;

	switch (pix)
	{
	case 1:
		count = 1 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = color;
			}
		}
		break;

	case 2:
		count = 2 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = color;
			}

			if (pz[1] <= izi)
			{
				pz[1] = izi;
				pdest[1] = color;
			}
		}
		break;

	case 3:
		count = 3 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = color;
			}

			if (pz[1] <= izi)
			{
				pz[1] = izi;
				pdest[1] = color;
			}

			if (pz[2] <= izi)
			{
				pz[2] = izi;
				pdest[2] = color;
			}
		}
		break;

	case 4:
		count = 4 << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			if (pz[0] <= izi)
			{
				pz[0] = izi;
				pdest[0] = color;
			}

			if (pz[1] <= izi)
			{
				pz[1] = izi;
				pdest[1] = color;
			}

			if (pz[2] <= izi)
			{
				pz[2] = izi;
				pdest[2] = color;
			}

			if (pz[3] <= izi)
			{
				pz[3] = izi;
				pdest[3] = color;
			}
		}
		break;

	default:
		count = pix << d_y_aspect_shift;

		for ( ; count ; count--, pz += d_zwidth, pdest += screenwidth)
		{
			for (i=0 ; i<pix ; i++)
			{
				if (pz[i] <= izi)
				{
					pz[i] = izi;
					pdest[i] = color;
				}
			}
		}
		break;
	}
}

#ifdef QOG_ALIAS_FAST
/*
==============
D_QogParticleView, D_QogDrawParticleFast

QuakeOG (Agent AI): particles in Fast mode (r_fast 1, QOG_ALIAS_FAST) with
no float arithmetic. The position is read into 24.8 fixed point by its bits
(D_QogFix8), the eye's subtracted, and all three shifted right together
until they are below 2^15, so the products with the view vectors in 2.14
fit 32 bits. The screen position is two divides and 1/z one, all on the
hardware divider, within about a sixteenth of a pixel of id's float
statements. R_DrawParticles makes the view's constants once.
==============
*/
static int D_QogFix8 (float f)
{
	union
	{
		float	f;
		int		i;
	} b;
	int		e, m;

	b.f = f;
	e = ((b.i >> 23) & 0xff) - (127 + 23 - 8);
	if (e <= -24)
		return 0;
	if (e > 6)
		return b.i < 0 ? -0x3fffffff : 0x3fffffff;	// beyond any map
	m = (b.i & 0x7fffff) | 0x800000;
	m = e >= 0 ? m << e : m >> -e;
	return b.i < 0 ? -m : m;
}

void D_QogParticleView (d_qogpview_t *pv)
{
	int		i;

	for (i=0 ; i<3 ; i++)
	{
		pv->org[i] = D_QogFix8 (r_origin[i]);
		pv->right[i] = (int)(vright[i] * 16384.0f);
		pv->up[i] = (int)(vup[i] * 16384.0f);
		pv->fwd[i] = (int)(vpn[i] * 16384.0f);
	}
	pv->xs = (int)(xscaleshrink * 16.0f);
	pv->ys = (int)(yscaleshrink * 16.0f);
	pv->xc = (int)((xcenter + 0.5f) * 16.0f);
	pv->yc = (int)((ycenter + 0.5f) * 16.0f);
}

void D_QogDrawParticleFast (particle_t *pparticle, const d_qogpview_t *pv)
{
	int		lx, ly, lz, ax, sh, x, y, z, zd, u, v, izi, pix;

#ifdef QOG_ALIAS_STATS
	qog_astat_vm = 0;
	QOG_AST(QAS_PARTS, 1);
#endif
	lx = D_QogFix8 (pparticle->org[0]) - pv->org[0];
	ly = D_QogFix8 (pparticle->org[1]) - pv->org[1];
	lz = D_QogFix8 (pparticle->org[2]) - pv->org[2];

	ax = (lx < 0 ? -lx : lx) | (ly < 0 ? -ly : ly) | (lz < 0 ? -lz : lz);
	sh = 0;
	while (ax >= 0x8000)
	{
		ax >>= 1;
		sh++;
	}
	lx >>= sh;
	ly >>= sh;
	lz >>= sh;

// depth in world units times 2^(22 - sh)
	z = lx * pv->fwd[0] + ly * pv->fwd[1] + lz * pv->fwd[2];
	if (z < ((int)PARTICLE_Z_CLIP << (22 - sh)))
		return;

	x = (lx * pv->right[0] + ly * pv->right[1] + lz * pv->right[2]) >> 12;
	y = (lx * pv->up[0] + ly * pv->up[1] + lz * pv->up[2]) >> 12;
	zd = z >> 12;

	u = (x * pv->xs / zd + pv->xc) >> 4;
	v = (pv->yc - y * pv->ys / zd) >> 4;

	if ((v > d_vrectbottom_particle) ||
		(u > d_vrectright_particle) ||
		(v < d_vrecty) ||
		(u < d_vrectx))
	{
		return;
	}

// id's (int)(0x8000 / depth)
	izi = (int)(0x80000000u / (unsigned)(z >> 6)) >> sh;

	pix = izi >> d_pix_shift;
	QOG_AST(QAS_PARTVIS, 1);

	if (pix < d_pix_min)
		pix = d_pix_min;
	else if (pix > d_pix_max)
		pix = d_pix_max;

	izi = D_ZQ(izi);
	QOG_AST(QAS_PARTPIX, (pix * pix) << d_y_aspect_shift);

	D_ParticlePixels (d_viewbuffer + D_SCANTABLE(v) + u,
			d_pzbuffer + (d_zwidth * v) + u, izi, pix, pparticle->color);
}
#endif

/*
==============
D_DrawParticle
==============
*/
void D_DrawParticle (particle_t *pparticle)
{
	vec3_t	local, transformed;
	float	zi;
	byte	*pdest;
	d_zval_t	*pz;
	int		izi, pix, u, v;

#ifdef QOG_ALIAS_STATS
	qog_astat_vm = 0;	// QuakeOG (Agent AI)
	QOG_AST(QAS_PARTS, 1);
#endif
// transform point
	VectorSubtract (pparticle->org, r_origin, local);

// QuakeOG (Agent AI): depth first, so a particle behind the near plane
// costs no more products. The compare in float is the same, 8 is exact.
	transformed[2] = DotProduct(local, r_ppn);

	if (transformed[2] < (float)PARTICLE_Z_CLIP)
		return;

	transformed[0] = DotProduct(local, r_pright);
	transformed[1] = DotProduct(local, r_pup);

// project the point
// FIXME: preadjust xcenter and ycenter
	zi = QOG_FRECIP (transformed[2]);	// QuakeOG: QOG_FLOAT_EXACT
	u = (int)(xcenter + zi * transformed[0] + 0.5);
	v = (int)(ycenter - zi * transformed[1] + 0.5);

	if ((v > d_vrectbottom_particle) || 
		(u > d_vrectright_particle) ||
		(v < d_vrecty) ||
		(u < d_vrectx))
	{
		return;
	}

	pz = d_pzbuffer + (d_zwidth * v) + u;
	pdest = d_viewbuffer + D_SCANTABLE(v) + u;	// QuakeOG: QOG_NO_ROWTABLES
	izi = (int)(zi * 0x8000);

	pix = izi >> d_pix_shift;
	QOG_AST(QAS_PARTVIS, 1);	// QuakeOG (Agent AI): QOG_ALIAS_STATS

	if (pix < d_pix_min)
		pix = d_pix_min;
	else if (pix > d_pix_max)
		pix = d_pix_max;

	izi = D_ZQ(izi);	// QuakeOG: QOG_ZBUFFER8 (after the size, which uses id's value)
	QOG_AST(QAS_PARTPIX, (pix * pix) << d_y_aspect_shift);	// QuakeOG (Agent AI)

	D_ParticlePixels (pdest, pz, izi, pix, pparticle->color);	// QuakeOG (Agent AI)
}

#endif	// !id386

