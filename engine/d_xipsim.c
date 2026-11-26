/*
Copyright (C) 2026 QuakeOG contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
// d_xipsim.c -- QuakeOG, PC measurement only (QOG_XIPSIM). A
// model of the RP2040's XIP cache (16 KB, two ways, 8-byte lines, LRU)
// fed with the flash reads of the world span drawer, texels, colour map
// entries and the lightmap samples R_BuildLightMap reads. Code fetches and
// the rest of the frame are not modelled, so the misses are a lower bound
// of what the board sees, good for comparing ways of drawing the spans.
//
// Four caches run side by side, each seeing a subset of the reads.
//   0 texels + colour map + lightmaps (as the board reads them today)
//   1 texels + lightmaps (colour map in RAM)
//   2 colour map + lightmaps (texture in RAM)
//   3 lightmaps (both in RAM)
// qog_xipsim prints the misses per view of each kind in each cache since
// the last call and clears them.

#include "quakedef.h"

#ifdef QOG_XIPSIM

#define XS_SETS		1024
#define XS_CACHES	4

static unsigned		xs_tag[XS_CACHES][XS_SETS][2];
static unsigned char	xs_old[XS_CACHES][XS_SETS];	// way to replace next
static unsigned		xs_acc[XS_CACHES][3], xs_miss[XS_CACHES][3];
static unsigned		xs_ramtex;		// texel reads served from a RAM copy
static int			xs_frame0;
static const unsigned char	xs_mask[XS_CACHES] = { 7, 5, 6, 4 };

static void XipSim_Line (int kind, unsigned line)
{
	int		c, set = line & (XS_SETS - 1);
	unsigned	tag = line | 0x80000000u;	// 0 means empty

	for (c = 0 ; c < XS_CACHES ; c++)
	{
		unsigned	*w = xs_tag[c][set];

		if (!(xs_mask[c] & (1 << kind)))
			continue;
		xs_acc[c][kind]++;
		if (w[0] == tag)
			xs_old[c][set] = 1;
		else if (w[1] == tag)
			xs_old[c][set] = 0;
		else
		{
			xs_miss[c][kind]++;
			w[xs_old[c][set]] = tag;
			xs_old[c][set] ^= 1;
		}
	}
}

// kind 0 texel, 1 colour map, 2 lightmap
const unsigned char *XipSim_Rd (int kind, const unsigned char *p)
{
	extern int	xipsim_ramtex;

	if (kind == 0 && xipsim_ramtex)
		xs_ramtex++;
	else
		XipSim_Line (kind, (unsigned)(size_t)p >> 3);
	return p;
}

void XipSim_Range (int kind, const unsigned char *p, int bytes)
{
	unsigned	a = (unsigned)(size_t)p >> 3, e;

	if (bytes <= 0)
		return;
	e = ((unsigned)(size_t)p + bytes - 1) >> 3;
	for ( ; a <= e ; a++)
		XipSim_Line (kind, a);
}

int		xipsim_ramtex;		// set by the drawer while it reads a RAM copy

void XipSim_f (void)
{
	int		c, views = r_framecount - xs_frame0;
	static const char	*names[XS_CACHES] = { "all", "cmap_in_ram", "tex_in_ram", "both_in_ram" };

	if (views < 1)
		views = 1;
	Con_Printf ("qog_xipsim: views %d, misses per view (tex cmap lmap) of 8-byte lines, accesses per view in ()\n", views);
	for (c = 0 ; c < XS_CACHES ; c++)
		Con_Printf ("qog_xipsim: %-12s miss %7u %7u %7u total %7u  (acc %8u %8u %8u)\n", names[c],
			xs_miss[c][0] / views, xs_miss[c][1] / views, xs_miss[c][2] / views,
			(xs_miss[c][0] + xs_miss[c][1] + xs_miss[c][2]) / views,
			xs_acc[c][0] / views, xs_acc[c][1] / views, xs_acc[c][2] / views);
	Con_Printf ("qog_xipsim: texel reads from RAM copies per view %u\n", xs_ramtex / views);
	memset (xs_acc, 0, sizeof(xs_acc));
	memset (xs_miss, 0, sizeof(xs_miss));
	xs_ramtex = 0;
	xs_frame0 = r_framecount;
}

#endif	// QOG_XIPSIM
