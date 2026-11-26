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
// r_sky.c

#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"


int		iskyspeed = 8;
int		iskyspeed2 = 2;
float	skyspeed, skyspeed2;

float		skytime;

byte		*r_skysource;

int r_skymade;
int r_skydirect;		// not used?


// QuakeOG, the sky is composited straight from the texture (read in place
// from flash). id kept two pre-masked 128x131 copies of the front layer
// (bottomsky, bottommask) and a back-layer copy in the right half of a
// 256-wide newsky. The result is the same pixel rule, front if non-zero
// else back, so only the 128x128 output remains in RAM (49 KB less).

static byte	*r_skytex;			// 256x128, left half front layer (0 = clear), right half back layer
#ifdef QOG_SKY_DIRECT
// QuakeOG, no composited copy at all (16 KB less). R_MakeSky only records
// what it would have composited (the texture and the layer shift), and
// D_DrawSkyScans8 applies SkyPixel's rule to each pixel it draws. id kept
// the old composite while the shift did not change, even across a map
// change, so the recorded texture is kept the same way.
byte	*r_skycomposed;			// texture of the last composite
int		r_skyxshift, r_skyyshift;	// front layer shift of the last composite
#else
byte	newsky[128*128];		// composited sky, 128 wide (d_sky.c reads it with that stride)
#endif

static byte SkyPixel (int x, int y, int xshift, int yshift)
{
	byte	front = r_skytex[((y + yshift) & SKYMASK) * 256 + ((x + xshift) & SKYMASK)];
	return front ? front : r_skytex[y * 256 + 128 + x];
}


/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void R_InitSky (texture_t *mt)
{
	r_skytex = mt->mips[0];
#ifndef QOG_SKY_DIRECT
	r_skysource = newsky;
#endif
}


/*
=================
R_MakeSky
=================
*/
void R_MakeSky (void)
{
	int			x, y;
	int			xshift, yshift;
	byte		*out;
	static int	xlast = -1, ylast = -1;

	xshift = skytime*skyspeed;
	yshift = skytime*skyspeed;

	if ((xshift == xlast) && (yshift == ylast))
		return;

	xlast = xshift;
	ylast = yshift;

#ifdef QOG_SKY_DIRECT
	(void)x; (void)y; (void)out;
	r_skycomposed = r_skytex;
	r_skyxshift = xshift;
	r_skyyshift = yshift;
#else
	out = newsky;
	for (y=0 ; y<SKYSIZE ; y++)
		for (x=0 ; x<SKYSIZE ; x++)
			*out++ = SkyPixel (x, y, xshift, yshift);
#endif

	r_skymade = 1;
}


/*
=================
R_GenSkyTile
=================
*/
void R_GenSkyTile (void *pdest)
{
	int			x, y;
	int			xshift, yshift;
	byte		*pd;

	xshift = skytime*skyspeed;
	yshift = skytime*skyspeed;

	pd = (byte *)pdest;
	for (y=0 ; y<SKYSIZE ; y++)
		for (x=0 ; x<SKYSIZE ; x++)
			*pd++ = SkyPixel (x, y, xshift, yshift);
}


/*
=================
R_GenSkyTile16
=================
*/
void R_GenSkyTile16 (void *pdest)
{
	int				x, y;
	int				xshift, yshift;
	unsigned short	*pd;

	xshift = skytime * skyspeed;
	yshift = skytime * skyspeed;

	pd = (unsigned short *)pdest;
	for (y=0 ; y<SKYSIZE ; y++)
		for (x=0 ; x<SKYSIZE ; x++)
			*pd++ = d_8to16table[SkyPixel (x, y, xshift, yshift)];
}


/*
=============
R_SetSkyFrame
==============
*/
void R_SetSkyFrame (void)
{
	int		g, s1, s2;
	float	temp;

	skyspeed = iskyspeed;
	skyspeed2 = iskyspeed2;

	g = GreatestCommonDivisor (iskyspeed, iskyspeed2);
	s1 = iskyspeed / g;
	s2 = iskyspeed2 / g;
	temp = SKYSIZE * s1 * s2;

	skytime = cl.time - ((int)(cl.time / temp) * temp);
	

	r_skymade = 0;
}


