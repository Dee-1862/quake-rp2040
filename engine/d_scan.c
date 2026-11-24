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
// d_scan.c
//
// Portable C scan-level rasterization code, all pixel depths.

#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"

unsigned char	*r_turb_pbase, *r_turb_pdest;
fixed16_t		r_turb_s, r_turb_t, r_turb_sstep, r_turb_tstep;
QOG_RTABLE_CONST int	*r_turb_turb;
int				r_turb_spancount;

void D_DrawTurbulent8Span (void);


/*
=============
D_WarpScreen

// this performs a slight compression of the screen at the same time as
// the sine warp, to keep the edges from wrapping
=============
*/
#ifdef QOG_WARP_INPLACE
/*
QuakeOG: the underwater view is drawn straight into the frame buffer (see
R_RenderView_) and distorted in place here, instead of being drawn into a
64 KB warp buffer on the stack and copied out. Destination row v reads
source rows from rowsrc[v] to rowsrc[v + AMP2*2], which are never more than
AMP2*2 rows above v, so each source row is saved to a small ring before it
is overwritten, and rows at or above v are read from that copy. The output
is the same as id's.
*/
#define WARP_RING	8	// power of two, more than AMP2*2 rows

void D_WarpScreen (void)
{
	int		w, h;
	int		u,v,k,r;
	byte	*dest;
	QOG_RTABLE_CONST int	*turb;
	int		*col;
	byte	**row;
	byte	*rowptr[MAXHEIGHT+(AMP2*2)];
	int		rowsrc[MAXHEIGHT+(AMP2*2)];
	int		column[MAXWIDTH+(AMP2*2)];
	byte	ring[WARP_RING][WARP_WIDTH];
	float	wratio, hratio;

	if (d_viewbuffer != vid.buffer || screenwidth != vid.rowbytes ||
		r_refdef.vrect.x != scr_vrect.x || r_refdef.vrect.y != scr_vrect.y ||
		r_refdef.vrect.width != scr_vrect.width ||
		r_refdef.vrect.height != scr_vrect.height ||
		r_refdef.vrect.x + r_refdef.vrect.width > WARP_WIDTH)
		Sys_Error ("D_WarpScreen: in-place warp needs the view drawn full size into the frame buffer");

	w = r_refdef.vrect.width;
	h = r_refdef.vrect.height;

	wratio = w / (float)scr_vrect.width;
	hratio = h / (float)scr_vrect.height;

	for (v=0 ; v<scr_vrect.height+AMP2*2 ; v++)
	{
		rowsrc[v] = (int)((float)v * hratio * h / (h + AMP2 * 2));
	}

	for (u=0 ; u<scr_vrect.width+AMP2*2 ; u++)
	{
		column[u] = r_refdef.vrect.x +
				(int)((float)u * wratio * w / (w + AMP2 * 2));
	}

	turb = intsintable + ((int)(cl.time*SPEED)&(CYCLE-1));
	dest = vid.buffer + scr_vrect.y * vid.rowbytes + scr_vrect.x;

	for (v=0 ; v<scr_vrect.height ; v++, dest += vid.rowbytes)
	{
	// save source row v before destination row v overwrites it
		memcpy (&ring[v & (WARP_RING-1)][r_refdef.vrect.x],
				d_viewbuffer + (r_refdef.vrect.y + v) * screenwidth + r_refdef.vrect.x,
				w);

		for (k=v ; k<=v+AMP2*2 ; k++)
		{
			r = rowsrc[k];
			if (r <= v)
			{
				if (v - r >= WARP_RING)
					Sys_Error ("D_WarpScreen: warp ring too small");
				rowptr[k] = ring[r & (WARP_RING-1)];
			}
			else
				rowptr[k] = d_viewbuffer + (r_refdef.vrect.y + r) * screenwidth;
		}

		col = &column[turb[v]];
		row = &rowptr[v];

		for (u=0 ; u<scr_vrect.width ; u+=4)
		{
			dest[u+0] = row[turb[u+0]][col[u+0]];
			dest[u+1] = row[turb[u+1]][col[u+1]];
			dest[u+2] = row[turb[u+2]][col[u+2]];
			dest[u+3] = row[turb[u+3]][col[u+3]];
		}
	}
}
#else	// id's warp from a separate buffer
void D_WarpScreen (void)
{
	int		w, h;
	int		u,v;
	byte	*dest;
	QOG_RTABLE_CONST int	*turb;
	int		*col;
	byte	**row;
	byte	*rowptr[MAXHEIGHT+(AMP2*2)];
	int		column[MAXWIDTH+(AMP2*2)];
	float	wratio, hratio;

	w = r_refdef.vrect.width;
	h = r_refdef.vrect.height;

	wratio = w / (float)scr_vrect.width;
	hratio = h / (float)scr_vrect.height;

	for (v=0 ; v<scr_vrect.height+AMP2*2 ; v++)
	{
		rowptr[v] = d_viewbuffer + (r_refdef.vrect.y * screenwidth) +
				 (screenwidth * (int)((float)v * hratio * h / (h + AMP2 * 2)));
	}

	for (u=0 ; u<scr_vrect.width+AMP2*2 ; u++)
	{
		column[u] = r_refdef.vrect.x +
				(int)((float)u * wratio * w / (w + AMP2 * 2));
	}

	turb = intsintable + ((int)(cl.time*SPEED)&(CYCLE-1));
	dest = vid.buffer + scr_vrect.y * vid.rowbytes + scr_vrect.x;

	for (v=0 ; v<scr_vrect.height ; v++, dest += vid.rowbytes)
	{
		col = &column[turb[v]];
		row = &rowptr[v];

		for (u=0 ; u<scr_vrect.width ; u+=4)
		{
			dest[u+0] = row[turb[u+0]][col[u+0]];
			dest[u+1] = row[turb[u+1]][col[u+1]];
			dest[u+2] = row[turb[u+2]][col[u+2]];
			dest[u+3] = row[turb[u+3]][col[u+3]];
		}
	}
}
#endif	// QOG_WARP_INPLACE


#if	!id386

/*
=============
D_DrawTurbulent8Span
=============
*/
void D_DrawTurbulent8Span (void)
{
	int		sturb, tturb;

	do
	{
		sturb = ((r_turb_s + r_turb_turb[(r_turb_t>>16)&(CYCLE-1)])>>16)&63;
		tturb = ((r_turb_t + r_turb_turb[(r_turb_s>>16)&(CYCLE-1)])>>16)&63;
		*r_turb_pdest++ = *(r_turb_pbase + (tturb<<6) + sturb);
		r_turb_s += r_turb_sstep;
		r_turb_t += r_turb_tstep;
	} while (--r_turb_spancount > 0);
}

#endif	// !id386


/*
=============
Turbulent8
=============
*/
void Turbulent8 (espan_t *pspan)
{
	int				count;
	fixed16_t		snext, tnext;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz16stepu, tdivz16stepu, zi16stepu;
	
	r_turb_turb = sintable + ((int)(cl.time*SPEED)&(CYCLE-1));

	r_turb_sstep = 0;	// keep compiler happy
	r_turb_tstep = 0;	// ditto

	r_turb_pbase = (unsigned char *)cacheblock;

	sdivz16stepu = d_sdivzstepu * 16;
	tdivz16stepu = d_tdivzstepu * 16;
	zi16stepu = d_zistepu * 16;

	do
	{
		r_turb_pdest = (unsigned char *)((byte *)d_viewbuffer +
				(screenwidth * pspan->v) + pspan->u);

		count = pspan->count;

	// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

		r_turb_s = (int)(sdivz * z) + sadjust;
		if (r_turb_s > bbextents)
			r_turb_s = bbextents;
		else if (r_turb_s < 0)
			r_turb_s = 0;

		r_turb_t = (int)(tdivz * z) + tadjust;
		if (r_turb_t > bbextentt)
			r_turb_t = bbextentt;
		else if (r_turb_t < 0)
			r_turb_t = 0;

		do
		{
		// calculate s and t at the far end of the span
			if (count >= 16)
				r_turb_spancount = 16;
			else
				r_turb_spancount = count;

			count -= r_turb_spancount;

			if (count)
			{
			// calculate s/z, t/z, zi->fixed s and t at far end of span,
			// calculate s and t steps across span by shifting
				sdivz += sdivz16stepu;
				tdivz += tdivz16stepu;
				zi += zi16stepu;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 16)
					snext = 16;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 16)
					tnext = 16;	// guard against round-off error on <0 steps

				r_turb_sstep = (snext - r_turb_s) >> 4;
				r_turb_tstep = (tnext - r_turb_t) >> 4;
			}
			else
			{
			// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
			// can't step off polygon), clamp, calculate s and t steps across
			// span by division, biasing steps low so we don't run off the
			// texture
				spancountminus1 = (float)(r_turb_spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 16)
					snext = 16;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 16)
					tnext = 16;	// guard against round-off error on <0 steps

				if (r_turb_spancount > 1)
				{
					r_turb_sstep = (snext - r_turb_s) / (r_turb_spancount - 1);
					r_turb_tstep = (tnext - r_turb_t) / (r_turb_spancount - 1);
				}
			}

			r_turb_s = r_turb_s & ((CYCLE<<16)-1);
			r_turb_t = r_turb_t & ((CYCLE<<16)-1);

			D_DrawTurbulent8Span ();

			r_turb_s = snext;
			r_turb_t = tnext;

		} while (count > 0);

	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR(pspan->pnext)));	// QuakeOG
}


#if	!id386

/*
=============
D_DrawSpans8
=============
*/
void D_DrawSpans8 (espan_t *pspan)
{
	int				count, spancount;
	unsigned char	*pbase, *pdest;
	fixed16_t		s, t, snext, tnext, sstep, tstep;
	float			sdivz, tdivz, zi, z, du, dv, spancountminus1;
	float			sdivz8stepu, tdivz8stepu, zi8stepu;

	sstep = 0;	// keep compiler happy
	tstep = 0;	// ditto

	pbase = (unsigned char *)cacheblock;

	sdivz8stepu = d_sdivzstepu * 8;
	tdivz8stepu = d_tdivzstepu * 8;
	zi8stepu = d_zistepu * 8;

	do
	{
		pdest = (unsigned char *)((byte *)d_viewbuffer +
				(screenwidth * pspan->v) + pspan->u);

		count = pspan->count;

	// calculate the initial s/z, t/z, 1/z, s, and t and clamp
		du = (float)pspan->u;
		dv = (float)pspan->v;

		sdivz = d_sdivzorigin + dv*d_sdivzstepv + du*d_sdivzstepu;
		tdivz = d_tdivzorigin + dv*d_tdivzstepv + du*d_tdivzstepu;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
		z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

		s = (int)(sdivz * z) + sadjust;
		if (s > bbextents)
			s = bbextents;
		else if (s < 0)
			s = 0;

		t = (int)(tdivz * z) + tadjust;
		if (t > bbextentt)
			t = bbextentt;
		else if (t < 0)
			t = 0;

		do
		{
		// calculate s and t at the far end of the span
			if (count >= 8)
				spancount = 8;
			else
				spancount = count;

			count -= spancount;

			if (count)
			{
			// calculate s/z, t/z, zi->fixed s and t at far end of span,
			// calculate s and t steps across span by shifting
				sdivz += sdivz8stepu;
				tdivz += tdivz8stepu;
				zi += zi8stepu;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point

				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 8)
					snext = 8;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 8)
					tnext = 8;	// guard against round-off error on <0 steps

				sstep = (snext - s) >> 3;
				tstep = (tnext - t) >> 3;
			}
			else
			{
			// calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
			// can't step off polygon), clamp, calculate s and t steps across
			// span by division, biasing steps low so we don't run off the
			// texture
				spancountminus1 = (float)(spancount - 1);
				sdivz += d_sdivzstepu * spancountminus1;
				tdivz += d_tdivzstepu * spancountminus1;
				zi += d_zistepu * spancountminus1;
				z = (float)0x10000 / zi;	// prescale to 16.16 fixed-point
				snext = (int)(sdivz * z) + sadjust;
				if (snext > bbextents)
					snext = bbextents;
				else if (snext < 8)
					snext = 8;	// prevent round-off error on <0 steps from
								//  from causing overstepping & running off the
								//  edge of the texture

				tnext = (int)(tdivz * z) + tadjust;
				if (tnext > bbextentt)
					tnext = bbextentt;
				else if (tnext < 8)
					tnext = 8;	// guard against round-off error on <0 steps

				if (spancount > 1)
				{
					sstep = (snext - s) / (spancount - 1);
					tstep = (tnext - t) / (spancount - 1);
				}
			}

			do
			{
				*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);
				s += sstep;
				t += tstep;
			} while (--spancount > 0);

			s = snext;
			t = tnext;

		} while (count > 0);

	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR(pspan->pnext)));	// QuakeOG
}

#endif


#if	!id386

/*
=============
D_DrawZSpans
=============
*/
#ifdef QOG_ZSPANS_FAST
/*
=============
D_QogZiFixed

QuakeOG: (int)(zi * 0x8000 * 0x10000) as id's D_DrawZSpans computes it,
with zi a double holding a float, but without double arithmetic (a float
to double conversion, two double multiplies and a double to int
conversion are about 250 cycles per span through the M0+'s ROM routines).
Both multiplies are by powers of two, so the product is exact and the
result is the float's mantissa shifted by its exponent, truncated toward
zero. |zi| >= 1 (a product of 2^31 or more), infinities and NaNs take
id's expression.
=============
*/
static int D_QogZiFixed (float zi)
{
	union { float f; unsigned u; } v;
	unsigned	m;
	int			e, r;

	v.f = zi;
	e = (int)((v.u >> 23) & 0xFF);
	if (e >= 127)
		return (int)((double)zi * 0x8000 * 0x10000);
	if (e < 96)		// below 2^-31 (zeros and denormals too): truncates to 0
		return 0;
	m = (v.u & 0x7FFFFF) | 0x800000;	// zi = m * 2^(e - 150), so zi * 2^31 = m * 2^(e - 119)
	if (e >= 119)
		r = (int)(m << (e - 119));		// at most 2^31 - 2^7
	else
		r = (int)(m >> (119 - e));
	return (v.u & 0x80000000) ? -r : r;
}
#endif

void D_DrawZSpans (espan_t *pspan)
{
	int				count, doublecount, izistep;
	int				izi;
	d_zval_t		*pdest;
	unsigned		ltemp;
#ifdef QOG_ZSPANS_FAST
	float			zi;		// QuakeOG: see D_QogZiFixed
#else
	double			zi;
#endif
	float			du, dv;

// FIXME: check for clamping/range problems
// we count on FP exceptions being turned off to avoid range problems
	izistep = (int)(d_zistepu * 0x8000 * 0x10000);
#ifdef QOG_ZBUFFER8
	(void)ltemp; (void)doublecount;
	do
	{
		pdest = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;
		count = pspan->count;
		du = (float)pspan->u;
		dv = (float)pspan->v;
		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
#ifdef QOG_ZSPANS_FAST
		izi = D_QogZiFixed (zi);	// QuakeOG
#else
		izi = (int)(zi * 0x8000 * 0x10000);
#endif
		do
		{
			int	z = izi >> 16;
			*pdest++ = D_ZQ(z);
			izi += izistep;
		} while (--count > 0);
	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR(pspan->pnext)));
	return;
#endif

	do
	{
		pdest = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;

		count = pspan->count;

	// calculate the initial 1/z
		du = (float)pspan->u;
		dv = (float)pspan->v;

		zi = d_ziorigin + dv*d_zistepv + du*d_zistepu;
	// we count on FP exceptions being turned off to avoid range problems
#ifdef QOG_ZSPANS_FAST
		izi = D_QogZiFixed (zi);	// QuakeOG
#else
		izi = (int)(zi * 0x8000 * 0x10000);
#endif

		if ((long)pdest & 0x02)
		{
			*pdest++ = (short)(izi >> 16);
			izi += izistep;
			count--;
		}

		if ((doublecount = count >> 1) > 0)
		{
			do
			{
				ltemp = izi >> 16;
				izi += izistep;
				ltemp |= izi & 0xFFFF0000;
				izi += izistep;
				*(int *)pdest = ltemp;
				pdest += 2;
			} while (--doublecount > 0);
		}

		if (count & 1)
			*pdest = (short)(izi >> 16);

	} while (pspan->pnext != SPAN_NONE && (pspan = SPAN_PTR(pspan->pnext)));	// QuakeOG
}

#ifdef QOG_LITSPAN_ZFUSE
/*
=============
D_QogZSpan

QuakeOG: the z of one span exactly as D_DrawZSpans writes it, for
D_DrawSpans8Lit, which computes the same 1/z at the start of each span
for its own use (d_litspan.h). izistep as D_DrawZSpans computes it.
=============
*/
void D_QogZSpan (espan_t *pspan, float zi, int izistep)
{
	int				count, doublecount;
	int				izi;
	d_zval_t		*pdest;
	unsigned		ltemp;

	pdest = d_pzbuffer + (d_zwidth * pspan->v) + pspan->u;
	count = pspan->count;
#ifdef QOG_ZSPANS_FAST
	izi = D_QogZiFixed (zi);
#else
	izi = (int)((double)zi * 0x8000 * 0x10000);
#endif
#ifdef QOG_ZBUFFER8
	(void)ltemp; (void)doublecount;
	do
	{
		int	z = izi >> 16;
		*pdest++ = D_ZQ(z);
		izi += izistep;
	} while (--count > 0);
#else
	if ((long)pdest & 0x02)
	{
		*pdest++ = (short)(izi >> 16);
		izi += izistep;
		count--;
	}

	if ((doublecount = count >> 1) > 0)
	{
		do
		{
			ltemp = izi >> 16;
			izi += izistep;
			ltemp |= izi & 0xFFFF0000;
			izi += izistep;
			*(int *)pdest = ltemp;
			pdest += 2;
		} while (--doublecount > 0);
	}

	if (count & 1)
		*pdest = (short)(izi >> 16);
#endif
}
#endif

#endif

