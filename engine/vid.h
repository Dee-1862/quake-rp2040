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
// vid.h -- video driver defs

#define VID_CBITS	6
#define VID_GRADES	(1 << VID_CBITS)

// a pixel can be one, two, or four bytes
typedef byte pixel_t;

typedef struct vrect_s
{
	int				x,y,width,height;
	struct vrect_s	*pnext;
} vrect_t;

typedef struct
{
	pixel_t			*buffer;		// invisible buffer
	pixel_t			*colormap;		// 256 * VID_GRADES size
	unsigned short	*colormap16;	// 256 * VID_GRADES size
	int				fullbright;		// index of first fullbright color
	unsigned		rowbytes;	// may be > width if displayed in a window
	unsigned		width;		
	unsigned		height;
	float			aspect;		// width / height -- < 0 is taller than wide
	int				numpages;
	int				recalc_refdef;	// if true, recalc vid-based stuff
	pixel_t			*conbuffer;
	int				conrowbytes;
	unsigned		conwidth;
	unsigned		conheight;
	int				maxwarpwidth;
	int				maxwarpheight;
	pixel_t			*direct;		// direct drawing to framebuffer, if not
									//  NULL
} viddef_t;

extern	viddef_t	vid;				// global video state
#ifdef QOG_VID_NO_8TO16
// QuakeOG (Agent S): the board never fills or needs it (r_pixbytes 1), so
// its zeros stay in flash (port/rp2040/vid_rp2040.c)
extern	const unsigned short	d_8to16table[256];
#else
extern	unsigned short	d_8to16table[256];
#endif
extern	unsigned	d_8to24table[256];
extern void (*vid_menudrawfn)(void);
extern void (*vid_menukeyfn)(int key);

void	VID_SetPalette (unsigned char *palette);
// called at startup and after any gamma correction

void	VID_ShiftPalette (unsigned char *palette);
// called for bonus and pain flashes, and for underwater color changes

void	VID_Init (unsigned char *palette);
// Called at startup to set up translation tables, takes 256 8 bit RGB values
// the palette data will go away after the call, so it must be copied off if
// the video driver will need it again

void	VID_Shutdown (void);
// Called at shutdown

void	VID_Update (vrect_t *rects);
// flushes the given rectangles from the view buffer to the screen

int VID_SetMode (int modenum, unsigned char *palette);
// sets the mode; only used by the Quake engine for resetting to mode 0 (the
// base mode) on memory allocation failures

void VID_HandlePause (qboolean pause);
// called only on Win32, when pause happens, so the mouse can be released

#if defined(QOG_VID_SCANRACE) && !defined(QOG_VID_LOCKWAIT)
#error QOG_VID_SCANRACE needs QOG_VID_LOCKWAIT
#endif
#ifdef QOG_VID_SCANRACE
// QuakeOG (Agent AK): with QOG_VID_LOCKWAIT, SCR_UpdateScreen lets the 3D
// view start while the last frame's scanout still runs. VID_LockBuffer does
// not wait then, and each write into the frame or the z-buffer (whose start
// holds the status bar rows) before VID_QogScanDone first waits until the
// scanout has read every row of [lo, hi) it sends (VID_QogScanFree).
// VID_QogScanDone waits for its end, as VID_LockBuffer did, and ends that.
void VID_QogRaceBegin (void);
void VID_QogScanFree (const void *lo, const void *hi);
void VID_QogScanDone (void);
// QuakeOG (Agent AO): how many rows of the scanout in flight read a byte of
// [lo, hi), 0 for none (a Turbo frame's packed rows are half as wide)
int VID_QogScanNeed (const void *lo, const void *hi);
#endif

#ifdef QOG_RTURBO
// QuakeOG (Agent AO): the Turbo render mode (r_fast 2, engine/r_qogturbo.c).
// The 3D view is drawn half as wide, its rows packed at the start of the
// frame buffer (row r at vid.buffer + r * vid.width / 2), and the scanout
// doubles each pixel sideways. r_qog_turbopacked is the number of such rows
// the frame holds now, 0 when it holds none. Any 2D drawing into those rows
// first spreads them to full width (R_QogTurboUnpack), so menus, the console
// and text stay sharp.
extern int	r_qog_turboview;	// a Turbo view is being drawn (vid is the half width one)
extern int	r_qog_turbopacked;
void R_QogTurboUnpack (void);
void R_QogTurboChar (int x, int y, int num);	// the crosshair into a packed view
#define QOG_TURBO_2D(y)	do { if (r_qog_turbopacked && (int)(y) < r_qog_turbopacked) R_QogTurboUnpack (); } while (0)
#else
#define QOG_TURBO_2D(y)	((void)0)
#endif

