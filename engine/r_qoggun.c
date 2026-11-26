/*
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
// r_qoggun.c -- QuakeOG, the view weapon from pre-rendered images

/*
QuakeOG, QOG_GUN_IMAGES (approximate, Turbo only).

gfx/qoggun.dat in the display's pak holds every frame of every view weapon,
drawn once on the PC by id's own renderer (QOG_GUN_BAKE, tools/mkboard.py)
in a Turbo view, at its half width. In place of the model, the image is copied from
flash through one colour map row for the current light, transparent where
the model drew nothing, and moved by whole pixels for bobbing and view
kicks. Its pixels take the nearest z, so particles stay behind it as they
did behind the model. r_fast 0 and 1, a missing file, weapon or frame, and
any other view size or field of view draw id's model.

The file, all little endian

    u32 magic "QGUN", u16 view width, view height, yscale, weapons
    per weapon  char name[24], u32 frames, u32 table offset
    table       u32 image offset[frames]
    image       s16 x, y (in the view), w, h, then h rows of
                u8 runs, runs of (u8 skip, u8 length, length pixels)

What is not kept from the model, the light's direction and its per vertex
shading (one fixed direction, yaw 90, is in the image), the change of size
as the weapon moves nearer or farther, and roll.
*/

#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"

#ifdef QOG_GUN_IMAGES

#ifndef QOG_RTURBO
#error "QOG_GUN_IMAGES needs QOG_RTURBO"
#endif

#define GUN_FILE		"gfx/qoggun.dat"
#define GUN_MAGIC		0x4E554751		// "QGUN"
#define GUN_LIGHT		80				// the images' ambient and shade light
#define GUN_REFBRIGHT	(GUN_LIGHT + GUN_LIGHT / 2)
#define GUN_DEPTH		16.0f			// a typical depth of the visible weapon
#define GUN_RAISE		2.0f			// view.c, the weapon above the eye at viewsize 100

typedef struct
{
	unsigned		magic;
	unsigned short	width, height, yscale, nummodels;
} qgunhdr_t;

typedef struct
{
	char		name[24];
	unsigned	numframes, table;
} qgunmodel_t;

typedef struct
{
	short	x, y, w, h;
} qgunimage_t;

extern int	r_qog_turboview;

// the one pointer this keeps in RAM, 0 not looked for yet, 1 no file
static const byte	*r_qoggun;

#ifdef QOG_GUN_BAKE
static cvar_t	r_gunimage = {"r_gunimage", "1"};	// PC, 0 draws the model
static unsigned	gun_stat_views, gun_stat_pixels, gun_stat_runs;
#endif

static const byte *R_QogGunRow (const byte *p, byte *dest, d_zval_t *z, int x,
	int left, int right, const byte *cmap)
{
	int		runs, len, a, b;

	for (runs = *p++ ; runs ; runs--)
	{
		x += *p++;
		len = *p++;
		a = x < left ? left : x;
		b = x + len > right ? right : x + len;
		if (dest && a < b)
		{
			const byte	*src = p + (a - x);
			byte		*d = dest + a, *end = dest + b;
			d_zval_t	*dz = z + a;

#ifdef QOG_GUN_BAKE
			gun_stat_runs++;
			gun_stat_pixels += b - a;
#endif
			do
			{
				*d++ = cmap[*src++];
				*dz++ = (d_zval_t)0x7fff;	// the nearest, 255 in a byte z-buffer
			} while (d < end);
		}
		x += len;
		p += len;
	}
	return p;
}

static int Gun_Round (float a)
{
	return (int)(a < 0 ? a - 0.5f : a + 0.5f);
}

/*
==============
R_QogGunImage

R_DrawViewModel calls it with the weapon's light, currententity is
cl.viewent. Returns false to have the model drawn.
==============
*/
int R_QogGunImage (int ambient, int shade)
{
	const qgunhdr_t		*hdr;
	const qgunmodel_t	*m;
	const qgunimage_t	*im;
	const byte			*p, *next, *cmap;
	vec3_t				rel;
	float				f, s, turn;
	int					i, x, y, du, dv, row, rows, sy, level;
	char				*name;

	if (!r_qog_turboview)
		return 0;
#ifdef QOG_GUN_BAKE
	if (!r_gunimage.value)
		return 0;
#endif
	if (!r_qoggun)
	{
		r_qoggun = COM_FlashFile (GUN_FILE, &i);
		if (!r_qoggun || i < (int)sizeof(qgunhdr_t) || ((const qgunhdr_t *)r_qoggun)->magic != GUN_MAGIC)
			r_qoggun = (const byte *)1;
	}
	if (r_qoggun == (const byte *)1)
		return 0;

	hdr = (const qgunhdr_t *)r_qoggun;
	if (r_refdef.vrect.width != hdr->width || r_refdef.vrect.height != hdr->height
		|| (int)(yscale + 0.5f) != hdr->yscale)
		return 0;

	name = currententity->model->name;
	m = (const qgunmodel_t *)(hdr + 1);
	for (i=0 ; i<hdr->nummodels ; i++, m++)
		if (!strcmp (m->name, name))
			break;
	if (i == hdr->nummodels || (unsigned)currententity->frame >= m->numframes)
		return 0;
	im = (const qgunimage_t *)(r_qoggun
		+ ((const unsigned *)(r_qoggun + m->table))[currententity->frame]);

// one colour map row for the light, row 31 leaves the image as it is
	level = 63 - (32 * (ambient + shade / 2) + GUN_REFBRIGHT / 2) / GUN_REFBRIGHT;
	if (level < 0)
		level = 0;
	cmap = (const byte *)vid.colormap + level * 256;

// where the weapon is now against where it was drawn, as a point at the
// weapon's depth, then the view's kick against the weapon's angles
	VectorSubtract (currententity->origin, r_origin, rel);
	f = DotProduct (rel, vpn);
	s = 1.0f / (GUN_DEPTH + f);
	x = r_refdef.vrect.x + im->x;
	y = r_refdef.vrect.y + im->y;
	turn = r_refdef.viewangles[YAW] - currententity->angles[YAW];
	if (turn > 180)
		turn -= 360;
	else if (turn < -180)
		turn += 360;
	du = Gun_Round ((xscale * DotProduct (rel, vright) - (x + im->w * 0.5f - xcenter) * f) * s
		+ turn * (float)(M_PI / 180) * xscale);
	dv = Gun_Round ((-(y + im->h * 0.5f - ycenter) * f - yscale * (DotProduct (rel, vup) - GUN_RAISE)) * s
		- (r_refdef.viewangles[PITCH] + currententity->angles[PITCH]) * (float)(M_PI / 180) * yscale);

// rows, the last one again below an image cut off by the view's bottom
// when it moves up
	rows = im->h;
	if (dv < 0 && im->y + im->h == hdr->height)
		rows -= dv;
	p = next = (const byte *)(im + 1);
	for (row=0 ; row<rows ; row++)
	{
		sy = y + dv + row;
		if (sy >= r_refdef.vrectbottom)
			break;
		if (row < im->h)
			p = next;
		next = R_QogGunRow (p, sy < r_refdef.vrect.y ? NULL : (byte *)d_viewbuffer + sy * screenwidth,
			d_pzbuffer + sy * (int)d_zwidth, x + du, r_refdef.vrect.x, r_refdef.vrectright, cmap);
	}
#ifdef QOG_GUN_BAKE
	gun_stat_views++;
#endif
	return 1;
}

#ifdef QOG_GUN_BAKE
/*
===============================================================================

PC only, the writer (tools/mkboard.py) and the work counts

===============================================================================
*/

int		r_qog_gunbake;	// 0 off, else the writer's stage

static const char *gun_names[] =
{
	"progs/v_axe.mdl", "progs/v_shot.mdl", "progs/v_shot2.mdl", "progs/v_nail.mdl",
	"progs/v_nail2.mdl", "progs/v_rock.mdl", "progs/v_rock2.mdl", "progs/v_light.mdl"
};
#define GUN_NAMES	(int)(sizeof(gun_names) / sizeof(gun_names[0]))
#define GUN_FRAMES	16

static byte		gun_out[2 * 1024 * 1024];
static int		gun_size;
static unsigned	gun_ofs[GUN_NAMES][GUN_FRAMES];
static int		gun_frames[GUN_NAMES];
static model_t	*gun_models[GUN_NAMES];

static void Gun_Put (const void *data, int len)
{
	if (gun_size + len > (int)sizeof(gun_out))
		Sys_Error ("qog_gunbake: the images are too big");
	memcpy (gun_out + gun_size, data, len);
	gun_size += len;
}

// draws the entity over a view of one colour with an empty z-buffer
static void Gun_Draw (alight_t *light, int background, byte *keep)
{
	int		v, w = r_refdef.vrect.width;

	for (v=0 ; v<r_refdef.vrect.height ; v++)
	{
		int		sy = r_refdef.vrect.y + v;
		byte	*dest = (byte *)d_viewbuffer + sy * screenwidth + r_refdef.vrect.x;

		memset (dest, background, w);
		memset (d_pzbuffer + sy * (int)d_zwidth + r_refdef.vrect.x, 0, w * sizeof(d_zval_t));
	}
	R_AliasDrawModel (light);
	for (v=0 ; v<r_refdef.vrect.height ; v++)
		memcpy (keep + v * w, (byte *)d_viewbuffer + (r_refdef.vrect.y + v) * screenwidth + r_refdef.vrect.x, w);
}

static void Gun_Image (const byte *a, const byte *b)	// opaque where a == b
{
	int			w = r_refdef.vrect.width, h = r_refdef.vrect.height;
	int			u, v, x0 = w, y0 = h, x1 = 0, y1 = 0;
	qgunimage_t	im;

	for (v=0 ; v<h ; v++)
		for (u=0 ; u<w ; u++)
			if (a[v*w + u] == b[v*w + u])
			{
				if (u < x0) x0 = u;
				if (u >= x1) x1 = u + 1;
				if (v < y0) y0 = v;
				y1 = v + 1;
			}
	if (x1 <= x0)
		x0 = x1 = y0 = y1 = 0;
	im.x = x0;
	im.y = y0;
	im.w = x1 - x0;
	im.h = y1 - y0;
	Gun_Put (&im, sizeof(im));
	for (v=y0 ; v<y1 ; v++)
	{
		int		count = gun_size, last = x0;
		byte	pair[2] = {0, 0};

		Gun_Put (pair, 1);
		for (u=x0 ; u<x1 ; )
		{
			int		start, len, skip;

			while (u < x1 && a[v*w + u] != b[v*w + u])
				u++;
			if (u == x1)
				break;
			start = u;
			while (u < x1 && a[v*w + u] == b[v*w + u] && u - start < 255)
				u++;
			len = u - start;
			for (skip = start - last ; skip > 255 ; skip -= 255)
			{
				pair[0] = 255;
				pair[1] = 0;
				Gun_Put (pair, 2);
				gun_out[count]++;
			}
			pair[0] = skip;
			pair[1] = len;
			Gun_Put (pair, 2);
			Gun_Put (a + v*w + start, len);
			gun_out[count]++;
			last = u;
		}
	}
	if (gun_size & 1)
	{
		byte	zero = 0;
		Gun_Put (&zero, 1);
	}
}

static void Gun_BakeAll (void)
{
	static byte	a[320 * 200], b[320 * 200];
	float		lightvec[3] = {-1, 0, 0};
	alight_t	light;
	int			i, frame;
	entity_t	*e = &cl.viewent;

	if (r_refdef.vrect.width > 320 || r_refdef.vrect.height > 200)
		Sys_Error ("qog_gunbake: the view is larger than 320 x 200");

// a level view looking along yaw 90, the weapon where view.c puts it
// with no bob and no kick
	r_refdef.viewangles[PITCH] = r_refdef.viewangles[ROLL] = 0;
	r_refdef.viewangles[YAW] = 90;
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);
	currententity = e;
	VectorCopy (r_refdef.viewangles, e->angles);
	VectorCopy (r_origin, e->origin);
	e->origin[2] += GUN_RAISE;
	e->colormap = vid.colormap;
	VectorCopy (e->origin, r_entorigin);
	VectorSubtract (r_origin, r_entorigin, modelorg);

	for (i=0 ; i<GUN_NAMES ; i++)
	{
		if (!gun_models[i])
			continue;
		e->model = gun_models[i];
		for (frame=0 ; frame<gun_frames[i] ; frame++)
		{
			light.ambientlight = light.shadelight = GUN_LIGHT;
			light.plightvec = lightvec;
			e->frame = frame;
			Gun_Draw (&light, 0, a);
			light.ambientlight = light.shadelight = GUN_LIGHT;
			Gun_Draw (&light, 255, b);
			gun_ofs[i][frame] = gun_size;
			Gun_Image (a, b);
		}
	}
}

static void Gun_Write (void)
{
	qgunhdr_t	*hdr = (qgunhdr_t *)gun_out;
	qgunmodel_t	*m = (qgunmodel_t *)(hdr + 1);
	int			i, n = 0, images = 0;
	char		path[MAX_OSPATH];

	for (i=0 ; i<GUN_NAMES ; i++)
	{
		if (!gun_models[i])
			continue;
		memset (m->name, 0, sizeof(m->name));
		strcpy (m->name, gun_names[i]);
		m->numframes = gun_frames[i];
		while (gun_size & 3)
			gun_out[gun_size++] = 0;
		m->table = gun_size;
		Gun_Put (gun_ofs[i], gun_frames[i] * sizeof(gun_ofs[i][0]));
		images += gun_frames[i];
		m++;
		n++;
	}
	hdr->nummodels = n;
	sprintf (path, "%s/qoggun.out", com_gamedir);
	COM_CreatePath (path);
	{
		FILE	*f = fopen (path, "wb");

		if (!f || fwrite (gun_out, 1, gun_size, f) != (size_t)gun_size)
			Sys_Error ("qog_gunbake: cannot write %s", path);
		fclose (f);
	}
	Con_Printf ("qog_gunbake: %d weapons, %d images, %d bytes\n", n, images, gun_size);
}

/*
==============
R_QogGunBakeView

R_DrawViewModel calls it while qog_gunbake runs, the first call sets Turbo,
a Turbo view then draws and writes every image.
==============
*/
void R_QogGunBakeView (void)
{
	qgunhdr_t	*hdr = (qgunhdr_t *)gun_out;
	int			i, n;

	if (r_qog_gunbake == 1)
	{
		Cvar_Set ("r_fast", "2");
		r_qog_gunbake = 2;
		return;
	}
	if (!r_qog_turboview)
		return;

	for (i=n=0 ; i<GUN_NAMES ; i++)
	{
		gun_models[i] = COM_FlashFile ((char *)gun_names[i], NULL) ? Mod_ForName ((char *)gun_names[i], false) : NULL;
		if (!gun_models[i])
			continue;
		gun_frames[i] = gun_models[i]->numframes;
		if (gun_frames[i] > GUN_FRAMES)
			Sys_Error ("qog_gunbake: %s has %d frames", gun_names[i], gun_frames[i]);
		n++;
	}
	hdr->magic = GUN_MAGIC;
	hdr->width = r_refdef.vrect.width;
	hdr->height = r_refdef.vrect.height;
	hdr->yscale = (int)(yscale + 0.5f);
	gun_size = sizeof(*hdr) + n * sizeof(qgunmodel_t);
	Gun_BakeAll ();
	Gun_Write ();
	r_qog_gunbake = 0;
	Cbuf_AddText ("qog_quit\n");
}

static void R_QogGunBake_f (void)
{
	r_qog_gunbake = 1;
}

static void R_QogGunStats_f (void)
{
	if (gun_stat_views)
		Con_Printf ("qog_gunstats: %u images drawn, %u pixels and %u runs each\n", gun_stat_views,
			gun_stat_pixels / gun_stat_views, gun_stat_runs / gun_stat_views);
	else
		Con_Printf ("qog_gunstats: no image drawn\n");
}

void R_QogGunInit (void)
{
	Cvar_RegisterVariable (&r_gunimage);
	Cmd_AddCommand ("qog_gunbake", R_QogGunBake_f);
	Cmd_AddCommand ("qog_gunstats", R_QogGunStats_f);
}
#endif	// QOG_GUN_BAKE

#endif	// QOG_GUN_IMAGES
