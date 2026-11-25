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
// d_modech.c: called when mode has just changed

#include "quakedef.h"
#include "d_local.h"

int	d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;

int	d_y_aspect_shift, d_pix_min, d_pix_max, d_pix_shift;

#ifdef QOG_NO_ROWTABLES
int		d_scanrowbytes;		// QuakeOG: replaces the two row tables (d_local.h)
#else
int		d_scantable[MAXHEIGHT];
d_zval_t	*zspantable[MAXHEIGHT]; 
#endif

/*
================
D_Patch
================
*/
void D_Patch (void)
{
#if id386

	static qboolean protectset8 = false;

	if (!protectset8)
	{
		Sys_MakeCodeWriteable ((int)D_PolysetAff8Start,
						     (int)D_PolysetAff8End - (int)D_PolysetAff8Start);
		protectset8 = true;
	}

#endif	// id386
}


/*
================
D_ViewChanged
================
*/
void D_ViewChanged (void)
{
	int rowbytes;

	// QuakeOG: the scan and span tables are sized for MAXWIDTH x MAXHEIGHT
	if (vid.width > MAXWIDTH || vid.height > MAXHEIGHT)
		Sys_Error ("D_ViewChanged: video mode %dx%d is larger than MAXWIDTH x MAXHEIGHT (%dx%d)",
				   vid.width, vid.height, MAXWIDTH, MAXHEIGHT);

	if (r_dowarp
#ifdef QOG_RTURBO
		&& !r_qog_turboview	// QuakeOG (Agent AO): the in-place warp keeps a Turbo view's half rows
#endif
		)
		rowbytes = WARP_WIDTH;
	else
		rowbytes = vid.rowbytes;

	scale_for_mip = xscale;
	if (yscale > xscale)
		scale_for_mip = yscale;

#ifdef QOG_ZBUFFER_VIEW
	// QuakeOG: z rows as wide as the view, see d_local.h
	d_zwidth = r_refdef.vrect.width;
	d_zrowbytes = d_zwidth * 2;
	d_pzbuffer = d_zbase - (r_refdef.vrect.y * (int)d_zwidth + r_refdef.vrect.x);
#else
	d_zrowbytes = vid.width * 2;
	d_zwidth = vid.width;
#endif

	d_pix_min = r_refdef.vrect.width / 320;
	if (d_pix_min < 1)
		d_pix_min = 1;

	d_pix_max = (int)((float)r_refdef.vrect.width / (320.0 / 4.0) + 0.5);
	d_pix_shift = 8 - (int)((float)r_refdef.vrect.width / 320.0 + 0.5);
	if (d_pix_max < 1)
		d_pix_max = 1;

	if (pixelAspect > 1.4)
		d_y_aspect_shift = 1;
	else
		d_y_aspect_shift = 0;

	d_vrectx = r_refdef.vrect.x;
	d_vrecty = r_refdef.vrect.y;
	d_vrectright_particle = r_refdef.vrectright - d_pix_max;
	d_vrectbottom_particle =
			r_refdef.vrectbottom - (d_pix_max << d_y_aspect_shift);

#ifdef QOG_NO_ROWTABLES
	d_scanrowbytes = rowbytes;
#else
	{
		int		i;

		for (i=0 ; i<vid.height; i++)
		{
			d_scantable[i] = i*rowbytes;
			zspantable[i] = d_pzbuffer + i*d_zwidth;
		}
	}
#endif

	D_Patch ();
}

