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
// r_surf.c: surface-related refresh code

#ifndef QOG_DC_NAMES	// QuakeOG, d_core1.h includes them before its names
#include "quakedef.h"
#include "r_local.h"
#include "qog_bmodel.h"	// QuakeOG, board cost model counts (QOG_BOARDMODEL)
#endif

#ifndef QOG_DC_NAMES
drawsurf_t	r_drawsurf;
#endif

int				lightleft, sourcesstep, blocksize, sourcetstep;
int				lightdelta, lightdeltastep;
int				lightright, lightleftstep, lightrightstep, blockdivshift;
unsigned		blockdivmask;
void			*prowdestbase;
unsigned char	*pbasesource;
int				surfrowbytes;	// used by ASM files
unsigned		*r_lightptr;
int				r_stepback;
int				r_lightwidth;
int				r_numhblocks, r_numvblocks;
unsigned char	*r_source, *r_sourcemax;

void R_DrawSurfaceBlock8_mip0 (void);
void R_DrawSurfaceBlock8_mip1 (void);
void R_DrawSurfaceBlock8_mip2 (void);
void R_DrawSurfaceBlock8_mip3 (void);

static void	(*surfmiptable[4])(void) = {
	R_DrawSurfaceBlock8_mip0,
	R_DrawSurfaceBlock8_mip1,
	R_DrawSurfaceBlock8_mip2,
	R_DrawSurfaceBlock8_mip3
};



#ifndef QOG_DC_NAMES	// QuakeOG, the shared copy takes the context's grid
unsigned		blocklights[18*18];
#endif

#ifdef QOG_LMAP_STATS
/*
QuakeOG, PC measurement only. Work counts of the light grid
builder per view, printed every 100 views as sums since the start.
qog_lmstats views, dlights alive, lightmap builds, distinct surfaces,
builds of the surface built just before, luxels, luxel styles, builds with
dlights, light and surface pairs, pairs past the minlight test, luxels of
those pairs, luxels lit, rows lit, R_MarkLights nodes and surfaces marked.
*/
unsigned	qog_lms[16];
enum { LMS_VIEWS, LMS_DLIGHTS, LMS_BUILDS, LMS_DISTINCT, LMS_REPEAT, LMS_LUX, LMS_STYLELUX,
	LMS_DLBUILDS, LMS_PAIRS, LMS_PAIRSIN, LMS_PAIRLUX, LMS_LIT, LMS_ROWSLIT, LMS_NODES, LMS_MARKS };
static int			lms_frame = -1;
static msurface_t	*lms_last;
static msurface_t	*lms_seen[4096];
static int			lms_nseen;

static void R_QogLmStatsBuild (msurface_t *surf, int size, int styles)
{
	int		i;

	if (r_framecount != lms_frame)
	{
		lms_frame = r_framecount;
		lms_nseen = 0;
		lms_last = NULL;
		qog_lms[LMS_VIEWS]++;
		for (i=0 ; i<MAX_DLIGHTS ; i++)
			if (cl_dlights[i].die >= cl.time && cl_dlights[i].radius)
				qog_lms[LMS_DLIGHTS]++;
		if (qog_lms[LMS_VIEWS] % 100 == 0)
			Sys_Printf ("qog_lmstats %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
				qog_lms[0], qog_lms[1], qog_lms[2], qog_lms[3], qog_lms[4], qog_lms[5], qog_lms[6],
				qog_lms[7], qog_lms[8], qog_lms[9], qog_lms[10], qog_lms[11], qog_lms[12],
				qog_lms[13], qog_lms[14], qog_lms[15]);
	}
	qog_lms[LMS_BUILDS]++;
	if (surf == lms_last)
		qog_lms[LMS_REPEAT]++;
	lms_last = surf;
	for (i=0 ; i<lms_nseen ; i++)
		if (lms_seen[i] == surf)
			break;
	if (i == lms_nseen)
	{
		qog_lms[LMS_DISTINCT]++;
		if (lms_nseen < 4096)
			lms_seen[lms_nseen++] = surf;
	}
	qog_lms[LMS_LUX] += size;
	qog_lms[LMS_STYLELUX] += size * styles;
}
#define LMS(c, n)	(qog_lms[c] += (unsigned)(n))
#else
#define LMS(c, n)
#endif

#if defined(QOG_DLIGHT_INT) && !defined(QUAKE2)
#ifndef QOG_NOINLINE
#if defined(__GNUC__)
#define QOG_NOINLINE	__attribute__((noinline))
#elif defined(_MSC_VER)
#define QOG_NOINLINE	__declspec(noinline)
#else
#define QOG_NOINLINE
#endif
#endif
/*
===============
R_QogDlightInt

QuakeOG, one light's cells of R_AddDynamicLights in integers,
with id's result to the bit. Returns 0, having changed nothing, when the
values are outside the range the proof below covers, and id's float loop
runs instead.

id's cell loop, per cell, all in float, sd = (int)(local[0] - s*16)
(truncated), the same for td, dist = sd + (td>>1) or td + (sd>>1) as a
float, the test dist < minlight, and blocklights += (rad - dist)*256,
which adds in float and truncates back to unsigned.

- sd, with A = floor (local) and c = 1 when local is not a whole number,
  local - 16s is A - 16s + frac. When |local - 16s| < 4096 its float
  rounding error is at most 2^-13, and when the fraction is 0 or at least
  2^-11 away from 0 and 1 the rounded value stays in the same open unit
  interval as the exact one, so the truncation is A - 16s when that is
  0 or more, else A - 16s + c, and sd is its magnitude.
- The test, dist is a whole number, so dist < minlight is
  dist < ceil (minlight).
- The add, with 1 <= rad < 32768 the float rad - dist (0 <= dist < rad)
  is exact, and so is its product with 256. With X = rad * 2^24 (a whole
  number), 256 rad is (X >> 16) + F / 65536, F = X & 0xFFFF. The float sum
  S = b + 256 rad - 256 dist with b the cell's value has the whole part
  Si = b + (X >> 16) - 256 dist and the fraction F / 65536. Below 2^23 the
  sum's float spacing u is at most 1/2, so the rounded sum truncates to
  Si + 1 exactly when S is within u/2 of Si + 1 (a tie goes up, Si + 1 is
  an even multiple of u), that is when 65536 - F <= 2^(k-8) with 2^k <= S,
  and as F is the same for every cell of this light, when Si is at least
  2^(8 + ceil (log2 (65536 - F))). A cell whose sum reaches 2^23 - 1 takes
  id's float expression.
===============
*/
static QOG_NOINLINE int R_QogDlightInt (const float *local, float rad, float minlight, int smax, int tmax)
{
	int			A[2], c[2], k, M, RI, thr, s, t, d, sd, td, dist, rowd;
	float		f;
	long long	X;
	unsigned	*bl, Si, F, G;

	if (!(rad >= 1.0f && rad < 32768.0f && minlight >= 0.0f))
		return 0;
	for (k=0 ; k<2 ; k++)
	{
		if (!(local[k] > -3800.0f && local[k] < 3800.0f))
			return 0;
		A[k] = (int)local[k];
		if ((float)A[k] > local[k])
			A[k]--;
		f = local[k] - (float)A[k];		// exact, below 1
		c[k] = f > 0.0f;
		if (c[k] && (f < 1.0f / 2048 || f > 1.0f - 1.0f / 2048))
			return 0;
	}
	M = (int)minlight;
	if ((float)M < minlight)
		M++;
	X = (long long)(rad * 16777216.0f);	// exact, rad is a multiple of 2^-23
	RI = (int)(X >> 16);
	F = (unsigned)X & 0xFFFF;
	G = 65536 - F;
	for (k=0 ; (1u << k) < G ; k++)
		;
	thr = 1 << (8 + k);					// 2^24 when F is 0, never

	bl = blocklights;
	for (t = 0 ; t<tmax ; t++, bl += smax)
	{
		rowd = A[1] - t*16;
		td = rowd >= 0 ? rowd : -rowd - c[1];
		if (td >= M)
			continue;		// dist >= td, no cell of this row
		d = A[0];
		for (s=0 ; s<smax ; s++, d -= 16)
		{
			sd = d >= 0 ? d : -d - c[0];
			if (sd > td)
				dist = sd + (td>>1);
			else
				dist = td + (sd>>1);
			if (dist >= M)
				continue;
			Si = bl[s] + (unsigned)(RI - 256*dist);
			if (bl[s] < (1u << 23) && Si < (1u << 23) - 1)
				bl[s] = Si + (Si >= (unsigned)thr);
			else
				bl[s] += (rad - (float)dist)*256;	// id's float expression
		}
	}
	return 1;
}
#endif

#ifdef QOG_DLIGHT_CHECK
/* QuakeOG, id's cell loop of one light, for the check build */
static void R_QogDlightFloat (const float *local, float rad, float minlight, int smax, int tmax)
{
	int		s, t, sd, td;
	float	dist;

	for (t = 0 ; t<tmax ; t++)
	{
		td = local[1] - t*16;
		if (td < 0)
			td = -td;
		for (s=0 ; s<smax ; s++)
		{
			sd = local[0] - s*16;
			if (sd < 0)
				sd = -sd;
			if (sd > td)
				dist = sd + (td>>1);
			else
				dist = td + (sd>>1);
			if (dist < minlight)
				blocklights[t*smax + s] += (rad - dist)*256;
		}
	}
}
#endif

/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (void)
{
	msurface_t *surf;
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
#ifdef QOG_DLIGHT_SPARSE
	int			dlbits;
#endif

	surf = r_drawsurf.surf;
	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = SURF_TEXINFO (r_drawsurf.model, surf);	// QuakeOG
#ifdef QOG_DLIGHT_SPARSE
	dlbits = R_DlightBits (r_drawsurf.model, surf);	// QuakeOG, kept only for lit surfaces
#endif

	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
#ifdef QOG_DLIGHT_SPARSE
		if ( !(dlbits & (1<<lnum) ) )
#else
		if ( !(SURF_DLIGHTBITS (r_drawsurf.model, surf) & (1<<lnum) ) )
#endif
			continue;		// not lit by this light

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, SURF_PLANE (r_drawsurf.model, surf)->normal) -	// QuakeOG
				SURF_PLANE (r_drawsurf.model, surf)->dist;
		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;
		LMS (LMS_PAIRS, 1);
		if (rad < minlight)
			continue;
		minlight = rad - minlight;
		LMS (LMS_PAIRSIN, 1);
		LMS (LMS_PAIRLUX, smax * tmax);

		for (i=0 ; i<3 ; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] -
					SURF_PLANE (r_drawsurf.model, surf)->normal[i]*dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
#if defined(QOG_DLIGHT_INT) && !defined(QUAKE2)
#ifdef QOG_DLIGHT_CHECK
		// QuakeOG, PC check build, both ways on every pair
		{
			static unsigned	qog_dlc_pairs, qog_dlc_bad, qog_dlc_int;
			unsigned		save[18*18], intres[18*18];
			int				ok, n = smax * tmax;

			memcpy (save, blocklights, n * sizeof(unsigned));
			ok = R_QogDlightInt (local, rad, minlight, smax, tmax);
			memcpy (intres, blocklights, n * sizeof(unsigned));
			memcpy (blocklights, save, n * sizeof(unsigned));
			R_QogDlightFloat (local, rad, minlight, smax, tmax);
			qog_dlc_pairs++;
			if (ok)
			{
				qog_dlc_int++;
				if (memcmp (intres, blocklights, n * sizeof(unsigned)))
					qog_dlc_bad++;
			}
			if ((qog_dlc_pairs & 4095) == 0 || (ok && memcmp (intres, blocklights, n * sizeof(unsigned))))
				Sys_Printf ("qog_dlcheck pairs %u int %u differ %u\n", qog_dlc_pairs, qog_dlc_int, qog_dlc_bad);
			continue;
		}
#else
		if (R_QogDlightInt (local, rad, minlight, smax, tmax))	// QuakeOG
		{
			QOG_BM (QOG_BM_DLPAIR_INT, 1);	// QuakeOG
			QOG_BM (QOG_BM_DLCELL_INT, smax * tmax);
			continue;
		}
#endif
#endif
		
		QOG_BM (QOG_BM_DLPAIR_FLT, 1);	// QuakeOG
		QOG_BM (QOG_BM_DLCELL_FLT, smax * tmax);
		for (t = 0 ; t<tmax ; t++)
		{
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			LMS (LMS_ROWSLIT, td < minlight);
			for (s=0 ; s<smax ; s++)
			{
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
#ifdef QOG_LMAP_STATS
				LMS (LMS_LIT, dist < minlight);
#endif
				QOG_BM (QOG_BM_DLLIT_FLT, dist < minlight);	// QuakeOG
				if (dist < minlight)
#ifdef QUAKE2
				{
					unsigned temp;
					temp = (rad - dist)*256;
					i = t*smax + s;
					if (!cl_dlights[lnum].dark)
						blocklights[i] += temp;
					else
					{
						if (blocklights[i] > temp)
							blocklights[i] -= temp;
						else
							blocklights[i] = 0;
					}
				}
#else
					blocklights[t*smax + s] += (rad - dist)*256;
#endif
			}
		}
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
#if defined(QOG_LMAP_FUSE) && defined(QOG_LMAP_FUSE_CHECK)
static unsigned	*r_qog_lmcheck;
static int		r_qog_lmchecksize;

static void R_QogLmCheck (const unsigned *bl)
{
	static unsigned	n, bad;

	n++;
	if (memcmp (bl, r_qog_lmcheck, r_qog_lmchecksize * sizeof(unsigned)))
		bad++;
	if ((n & 0x3FFF) == 0)
		Sys_Printf ("qog_lmfusecheck %u grids, %u differ\n", n, bad);
}
#endif

void R_BuildLightMap (void)
{
	int			smax, tmax;
	int			t;
	int			i, size;
	byte		*lightmap;
	unsigned	scale;
	int			maps;
	msurface_t	*surf;

	surf = r_drawsurf.surf;

	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	size = smax*tmax;
	lightmap = surf->samples;

	if (r_fullbright.value || !cl.worldmodel->lightdata)
	{
		for (i=0 ; i<size ; i++)
			blocklights[i] = 0;
		return;
	}

#if defined(QOG_LMAP_FUSE) && defined(QOG_LMAP_FUSE_CHECK)
	// PC check build, id's passes into a copy, compared with the fused grid
	if (!SURF_DLIGHTFRAME_IS (r_drawsurf.model, surf, r_framecount))
	{
		static unsigned	ref[18*18];
		byte			*lm = lightmap;
		int				k;

		for (i=0 ; i<size ; i++)
			ref[i] = r_refdef.ambientlight<<8;
		if (lm)
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++)
			{
				scale = r_drawsurf.lightadj[maps];
				for (i=0 ; i<size ; i++)
					ref[i] += lm[i] * scale;
				lm += size;
			}
		for (i=0 ; i<size ; i++)
		{
			k = (255*256 - (int)ref[i]) >> (8 - VID_CBITS);
			ref[i] = k < (1 << 6) ? (1 << 6) : k;
		}
		r_qog_lmcheck = ref;
		r_qog_lmchecksize = size;
	}
	else
		r_qog_lmcheck = NULL;
#endif
#ifdef QOG_LMAP_FUSE
	/*
	QuakeOG, id's passes over the grid (clear to ambient, one
	per light style, bound and invert after the dynamic lights) in one or
	two. The cell is an unsigned sum, which wraps the same in any order, so
	ambient plus each style's sample times its scale in one statement is
	id's value, and without dynamic lights the bound and invert follow in
	the same loop. The grid is read and written once instead of three or
	four times. No new names, so core 1's copy (d_core1.h) needs none.
	*/
	{
		unsigned		amb = r_refdef.ambientlight<<8, s0, s1, v;
		const byte		*l0, *l1;
		int				nmaps = 0, dl;
		unsigned		*bl = blocklights;

		l0 = l1 = lightmap;
		s0 = s1 = 0;
		if (lightmap)
		{
			for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++)
				;
			nmaps = maps;
		}
		dl = SURF_DLIGHTFRAME_IS (r_drawsurf.model, surf, r_framecount);	// QuakeOG
		if (nmaps <= 2)
		{
			if (nmaps >= 1)
			{
				s0 = r_drawsurf.lightadj[0];	// 8.8 fraction
				l1 = l0 + size;
			}
			if (nmaps == 2)
				s1 = r_drawsurf.lightadj[1];
			// with fewer styles the scale is 0 and the product adds nothing
			if (nmaps == 0)
				l0 = l1 = (const byte *)bl;	// any readable bytes, times 0
			else if (nmaps == 1)
				l1 = l0;
			if (!dl && nmaps == 1)
			{
				// one style, the common case, one product a cell
				unsigned	*end = bl + size;
				unsigned	*p = bl;
				int			top = 255*256 - (int)amb;

				while (p < end)
				{
					t = (top - (int)(*l0++ * s0)) >> (8 - VID_CBITS);
					if (t < (1 << 6))
						t = (1 << 6);
					*p++ = t;
				}
			}
			else if (!dl)
			{
				for (i=0 ; i<size ; i++)
				{
					v = amb + l0[i] * s0 + l1[i] * s1;
					t = (255*256 - (int)v) >> (8 - VID_CBITS);
					if (t < (1 << 6))
						t = (1 << 6);
					bl[i] = t;
				}
			}
			else
			{
				for (i=0 ; i<size ; i++)
					bl[i] = amb + l0[i] * s0 + l1[i] * s1;
			}
		}
		else
		{
			// three or four styles, id's passes
			for (i=0 ; i<size ; i++)
				bl[i] = amb;
			for (maps = 0 ; maps < nmaps ; maps++)
			{
				scale = r_drawsurf.lightadj[maps];	// 8.8 fraction
				for (i=0 ; i<size ; i++)
					bl[i] += lightmap[i] * scale;
				lightmap += size;	// skip to next lightmap
			}
			lightmap = surf->samples;
		}
		if (lightmap)
			lightmap += size * nmaps;	// as id's loop leaves it

#ifdef QOG_XIPSIM
		{	// QuakeOG, PC measurement, the samples read from flash (d_xipsim.c)
			void XipSim_Range (int kind, const unsigned char *p, int bytes);
			if (surf->samples)
				XipSim_Range (2, surf->samples, (int)(lightmap - surf->samples));
		}
#endif

#ifdef QOG_LMAP_STATS
		R_QogLmStatsBuild (surf, size, lightmap ? nmaps : 0);
#endif
#ifdef QOG_LMAP_FUSE_CHECK
		if (r_qog_lmcheck && nmaps <= 2)
			R_QogLmCheck (bl);
#endif
		if (!dl && nmaps <= 2)
			return;		// bound and inverted above

		if (dl)
		{
			LMS (LMS_DLBUILDS, 1);
			R_AddDynamicLights ();
		}

	// bound, invert, and shift
		for (i=0 ; i<size ; i++)
		{
			t = (255*256 - (int)bl[i]) >> (8 - VID_CBITS);

			if (t < (1 << 6))
				t = (1 << 6);

			bl[i] = t;
		}
#ifdef QOG_LMAP_FUSE_CHECK
		if (r_qog_lmcheck)
			R_QogLmCheck (bl);
#endif
		return;
	}
#endif

// clear to ambient
	for (i=0 ; i<size ; i++)
		blocklights[i] = r_refdef.ambientlight<<8;


// add all the lightmaps
	if (lightmap)
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ;
			 maps++)
		{
			scale = r_drawsurf.lightadj[maps];	// 8.8 fraction		
			for (i=0 ; i<size ; i++)
				blocklights[i] += lightmap[i] * scale;
			lightmap += size;	// skip to next lightmap
		}

#ifdef QOG_XIPSIM
	{	// QuakeOG, PC measurement, the samples read from flash (d_xipsim.c)
		void XipSim_Range (int kind, const unsigned char *p, int bytes);
		if (surf->samples)
			XipSim_Range (2, surf->samples, (int)(lightmap - surf->samples));
	}
#endif

#ifdef QOG_LMAP_STATS
	for (t=0 ; t<MAXLIGHTMAPS && surf->styles[t] != 255 ; t++)
		;
	R_QogLmStatsBuild (surf, size, lightmap ? t : 0);
#endif

// add all the dynamic lights
	if (SURF_DLIGHTFRAME_IS (r_drawsurf.model, surf, r_framecount))	// QuakeOG
	{
		LMS (LMS_DLBUILDS, 1);
		R_AddDynamicLights ();
	}

// bound, invert, and shift
	for (i=0 ; i<size ; i++)
	{
		t = (255*256 - (int)blocklights[i]) >> (8 - VID_CBITS);

		if (t < (1 << 6))
			t = (1 << 6);

		blocklights[i] = t;
	}
}


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base)
{
	int		reletive;
	int		count;

	if (currententity->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}
	
	if (!base->anim_total)
		return base;

	reletive = (int)(cl.time*10) % base->anim_total;

	count = 0;	
	while (base->anim_min > reletive || base->anim_max <= reletive)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100)
			Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}


/*
===============
R_DrawSurface
===============
*/
void R_DrawSurface (void)
{
	unsigned char	*basetptr;
	int				smax, tmax, twidth;
	int				u;
	int				soffset, basetoffset, texwidth;
	int				horzblockstep;
	unsigned char	*pcolumndest;
	void			(*pblockdrawer)(void);
	texture_t		*mt;

// calculate the lightings
	R_BuildLightMap ();
	
	surfrowbytes = r_drawsurf.rowbytes;

	mt = r_drawsurf.texture;
	
	r_source = mt->mips[r_drawsurf.surfmip];
	
// the fractional light values should range from 0 to (VID_GRADES - 1) << 16
// from a source range of 0 - 255
	
	texwidth = mt->width >> r_drawsurf.surfmip;

	blocksize = 16 >> r_drawsurf.surfmip;
	blockdivshift = 4 - r_drawsurf.surfmip;
	blockdivmask = (1 << blockdivshift) - 1;
	
	r_lightwidth = (r_drawsurf.surf->extents[0]>>4)+1;

	r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
	r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

//==============================

	if (r_pixbytes == 1)
	{
		pblockdrawer = surfmiptable[r_drawsurf.surfmip];
	// TODO: only needs to be set when there is a display settings change
		horzblockstep = blocksize;
	}
	else
	{
		pblockdrawer = R_DrawSurfaceBlock16;
	// TODO: only needs to be set when there is a display settings change
		horzblockstep = blocksize << 1;
	}

	smax = mt->width >> r_drawsurf.surfmip;
	twidth = texwidth;
	tmax = mt->height >> r_drawsurf.surfmip;
	sourcetstep = texwidth;
	r_stepback = tmax * twidth;

	r_sourcemax = r_source + (tmax * smax);

	soffset = r_drawsurf.surf->texturemins[0];
	basetoffset = r_drawsurf.surf->texturemins[1];

// << 16 components are to guarantee positive values for %
	soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
	basetptr = &r_source[((((basetoffset >> r_drawsurf.surfmip) 
		+ (tmax << 16)) % tmax) * twidth)];

	pcolumndest = r_drawsurf.surfdat;

	for (u=0 ; u<r_numhblocks; u++)
	{
		r_lightptr = blocklights + u;

		prowdestbase = pcolumndest;

		pbasesource = basetptr + soffset;

		(*pblockdrawer)();

		soffset = soffset + blocksize;
		if (soffset >= smax)
			soffset = 0;

		pcolumndest += horzblockstep;
	}
}


//=============================================================================

#if	!id386

/*
================
R_DrawSurfaceBlock8_mip0
================
*/
void R_DrawSurfaceBlock8_mip0 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 4;
		lightrightstep = (r_lightptr[1] - lightright) >> 4;

		for (i=0 ; i<16 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 4;

			light = lightright;

			for (b=15; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip1
================
*/
void R_DrawSurfaceBlock8_mip1 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 3;
		lightrightstep = (r_lightptr[1] - lightright) >> 3;

		for (i=0 ; i<8 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 3;

			light = lightright;

			for (b=7; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip2
================
*/
void R_DrawSurfaceBlock8_mip2 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 2;
		lightrightstep = (r_lightptr[1] - lightright) >> 2;

		for (i=0 ; i<4 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 2;

			light = lightright;

			for (b=3; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock8_mip3
================
*/
void R_DrawSurfaceBlock8_mip3 (void)
{
	int				v, i, b, lightstep, lighttemp, light;
	unsigned char	pix, *psource, *prowdest;

	psource = pbasesource;
	prowdest = prowdestbase;

	for (v=0 ; v<r_numvblocks ; v++)
	{
	// FIXME: make these locals?
	// FIXME: use delta rather than both right and left, like ASM?
		lightleft = r_lightptr[0];
		lightright = r_lightptr[1];
		r_lightptr += r_lightwidth;
		lightleftstep = (r_lightptr[0] - lightleft) >> 1;
		lightrightstep = (r_lightptr[1] - lightright) >> 1;

		for (i=0 ; i<2 ; i++)
		{
			lighttemp = lightleft - lightright;
			lightstep = lighttemp >> 1;

			light = lightright;

			for (b=1; b>=0; b--)
			{
				pix = psource[b];
				prowdest[b] = ((unsigned char *)vid.colormap)
						[(light & 0xFF00) + pix];
				light += lightstep;
			}
	
			psource += sourcetstep;
			lightright += lightrightstep;
			lightleft += lightleftstep;
			prowdest += surfrowbytes;
		}

		if (psource >= r_sourcemax)
			psource -= r_stepback;
	}
}


/*
================
R_DrawSurfaceBlock16

FIXME: make this work
================
*/
void R_DrawSurfaceBlock16 (void)
{
	int				k;
	unsigned char	*psource;
	int				lighttemp, lightstep, light;
	unsigned short	*prowdest;

	prowdest = (unsigned short *)prowdestbase;

	for (k=0 ; k<blocksize ; k++)
	{
		unsigned short	*pdest;
		unsigned char	pix;
		int				b;

		psource = pbasesource;
		lighttemp = lightright - lightleft;
		lightstep = lighttemp >> blockdivshift;

		light = lightleft;
		pdest = prowdest;

		for (b=0; b<blocksize; b++)
		{
			pix = *psource;
			*pdest = vid.colormap16[(light & 0xFF00) + pix];
			psource += sourcesstep;
			pdest++;
			light += lightstep;
		}

		pbasesource += sourcetstep;
		lightright += lightrightstep;
		lightleft += lightleftstep;
		prowdest = (unsigned short *)((long)prowdest + surfrowbytes);
	}

	prowdestbase = prowdest;
}

#endif


//============================================================================

/*
================
R_GenTurbTile
================
*/
void R_GenTurbTile (pixel_t *pbasetex, void *pdest)
{
	QOG_RTABLE_CONST int	*turb;
	int		i, j, s, t;
	byte	*pd;
	
	turb = sintable + ((int)(cl.time*SPEED)&(CYCLE-1));
	pd = (byte *)pdest;

	for (i=0 ; i<TILE_SIZE ; i++)
	{
		for (j=0 ; j<TILE_SIZE ; j++)
		{	
			s = (((j << 16) + turb[i & (CYCLE-1)]) >> 16) & 63;
			t = (((i << 16) + turb[j & (CYCLE-1)]) >> 16) & 63;
			*pd++ = *(pbasetex + (t<<6) + s);
		}
	}
}


/*
================
R_GenTurbTile16
================
*/
void R_GenTurbTile16 (pixel_t *pbasetex, void *pdest)
{
	QOG_RTABLE_CONST int	*turb;
	int				i, j, s, t;
	unsigned short	*pd;

	turb = sintable + ((int)(cl.time*SPEED)&(CYCLE-1));
	pd = (unsigned short *)pdest;

	for (i=0 ; i<TILE_SIZE ; i++)
	{
		for (j=0 ; j<TILE_SIZE ; j++)
		{	
			s = (((j << 16) + turb[i & (CYCLE-1)]) >> 16) & 63;
			t = (((i << 16) + turb[j & (CYCLE-1)]) >> 16) & 63;
			*pd++ = d_8to16table[*(pbasetex + (t<<6) + s)];
		}
	}
}


/*
================
R_GenTile
================
*/
void R_GenTile (msurface_t *psurf, void *pdest)
{
	if (psurf->flags & SURF_DRAWTURB)
	{
		if (r_pixbytes == 1)
		{
			R_GenTurbTile ((pixel_t *)
				SURF_TEXINFO (currententity->model, psurf)->texture->mips[0], pdest);	// QuakeOG
		}
		else
		{
			R_GenTurbTile16 ((pixel_t *)
				SURF_TEXINFO (currententity->model, psurf)->texture->mips[0], pdest);	// QuakeOG
		}
	}
	else if (psurf->flags & SURF_DRAWSKY)
	{
		if (r_pixbytes == 1)
		{
			R_GenSkyTile (pdest);
		}
		else
		{
			R_GenSkyTile16 (pdest);
		}
	}
	else
	{
		Sys_Error ("Unknown tile type");
	}
}

