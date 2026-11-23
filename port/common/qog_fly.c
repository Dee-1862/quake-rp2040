/* Renderer-only fly-through of a map. See qog_fly.h.
 *
 * Copyright (C) 2026 QuakeOG contributors. GPL v2 or later, like id's code
 * it calls. */
#include <math.h>
#include <string.h>
#include "quakedef.h"
#include "r_local.h"
#include "qog_fly.h"

#define FLY_SPACING		96		/* as port/pc/tour_pc.c TOUR_SPACING */
#define FLY_SPEED		160.0f	/* units per second along the path */
#define FLY_MIN_SEG		0.75f	/* seconds, so close points do not snap the view */
#define FLY_LOS_STEP	16.0f	/* line of sight samples */
#define FLY_MAX_PITCH	20.0f

extern cvar_t	scr_fov;
extern int		r_numparticles;
extern particle_t	*particles;
extern byte		*hunk_base;
extern int		hunk_low_used;

/* The route, on the level's low hunk: feet origins in loop order (a spot
   can appear more than once, the route goes back through spots it has seen
   rather than through walls) and the time at each, [fly_n] = loop length. */
static vec3_t	*fly_pts;
static float	*fly_start;
static byte		*fly_cut;	/* 1: no line of sight to the next point, the camera cuts there */
static int		fly_n;
static entity_t	fly_worldent;	/* cl_entities[0] when standalone */

/* ---- engine start ------------------------------------------------------ */

/* R_Init takes MAX_PARTICLES particles from the hunk (18 KB), which nothing
   here uses. When they are the last low hunk block, give the block back and
   leave the renderer one particle, which R_ClearParticles links and nothing
   ever takes. */
static particle_t	fly_oneparticle[1];

static void fly_release_particles (void)
{
	int		bytes = 16 + ((r_numparticles * (int)sizeof(particle_t) + 15) & ~15);	/* hunk_t + data */
	int		*h = (int *)((byte *)particles - 16);

	if ((byte *)particles + bytes - 16 != hunk_base + hunk_low_used
		|| h[0] != 0x1df001ed || h[1] != bytes)
	{
		Con_Printf ("qog_fly: particles kept (%i bytes)\n", bytes);
		return;
	}
	Hunk_FreeToLowMark (hunk_low_used - bytes);
	particles = fly_oneparticle;
	r_numparticles = 1;
}

void qog_fly_init_host (quakeparms_t *parms)
{
	host_parms = *parms;
	com_argc = parms->argc;
	com_argv = parms->argv;

	Memory_Init (parms->membase, parms->memsize);
	COM_Init (parms->basedir);
	W_LoadWadFile ("gfx.wad");
	Mod_Init ();
	R_InitTextures ();

	// palette and colormap read in place when the pack is mapped
	host_basepal = COM_FlashFile ("gfx/palette.lmp", NULL);
	if (!host_basepal)
		host_basepal = (byte *)COM_LoadHunkFile ("gfx/palette.lmp");
	host_colormap = COM_FlashFile ("gfx/colormap.lmp", NULL);
	if (!host_colormap)
		host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp");
	if (!host_basepal || !host_colormap)
		Sys_Error ("qog_fly: no gfx/palette.lmp or gfx/colormap.lmp");

	VID_Init (host_basepal);
	Draw_Init ();
	Cvar_RegisterVariable (&scr_fov);		// SCR_Init would, with its pictures
	Cvar_RegisterVariable (&scr_viewsize);
	R_Init ();
	fly_release_particles ();
	qog_fly_setup_view ();
}

void qog_fly_setup_view (void)
{
	vrect_t	vrect;

	sb_lines = 24 + 16 + 8;		// SCR_CalcRefdef with viewsize 100
	r_refdef.fov_x = scr_fov.value;
	vrect.x = 0;
	vrect.y = 0;
	vrect.width = vid.width;
	vrect.height = vid.height;
	R_SetVrect (&vrect, &scr_vrect, sb_lines);	// D_WarpScreen checks the view against it
	R_ViewChanged (&vrect, sb_lines, vid.aspect);
}

void qog_fly_lightstyles_normal (void)
{
	int		i;

	for (i = 0 ; i < MAX_LIGHTSTYLES ; i++)
	{
		cl_lightstyle[i].length = 1;
		cl_lightstyle[i].map[0] = 'm';
		cl_lightstyle[i].map[1] = 0;
	}
}

void qog_fly_load (const char *name)
{
	char	path[MAX_QPATH];

	sprintf (path, "maps/%s.bsp", name);
	cl.worldmodel = Mod_ForName (path, true);
	cl_entities = &fly_worldent;
	cl_entities[0].model = cl.worldmodel;
	R_NewMap ();
	qog_fly_use_world ();
}

/* ---- path -------------------------------------------------------------- */

static qboolean fly_solid (const float *p)
{
	return Mod_PointInLeaf ((float *)p, cl.worldmodel)->contents == CONTENTS_SOLID;
}

static qboolean fly_eye_solid (const vec3_t feet)
{
	vec3_t	eye;

	VectorCopy (feet, eye);
	eye[2] += QOG_FLY_EYE;
	return fly_solid (eye);
}

/* Line of sight from eye to eye, kept FLY_WIDE units clear on both sides,
   so the camera does not graze walls. */
#define FLY_WIDE		12.0f

static qboolean fly_clear (const vec3_t a, const vec3_t b)
{
	vec3_t	d, p, side;
	float	len, s;
	int		k;

	VectorSubtract (b, a, d);
	len = Length (d);
	side[0] = -d[1];
	side[1] = d[0];
	side[2] = 0;
	VectorNormalize (side);
	for (s = FLY_LOS_STEP ; s < len ; s += FLY_LOS_STEP)
	{
		for (k = -1 ; k <= 1 ; k++)
		{
			VectorMA (a, s / len, d, p);
			VectorMA (p, k * FLY_WIDE, side, p);
			p[2] += QOG_FLY_EYE;
			if (fly_solid (p))
				return false;
		}
	}
	return true;
}

/* Items and monsters often stand next to a wall. Move the spot away from
   walls within FLY_NEAR of its eye, 8 units at a time (it stays on whole
   units, which the protocol sends exactly, see port/pc/fly_pc.c). */
#define FLY_NEAR		32

static void fly_nudge (vec3_t feet)
{
	int		pass, dx, dy, mx, my;
	vec3_t	eye, p;

	for (pass = 0 ; pass < 4 ; pass++)
	{
		VectorCopy (feet, eye);
		eye[2] += QOG_FLY_EYE;
		mx = my = 0;
		for (dx = -1 ; dx <= 1 ; dx++)
			for (dy = -1 ; dy <= 1 ; dy++)
			{
				if (!dx && !dy)
					continue;
				p[0] = eye[0] + dx * FLY_NEAR;
				p[1] = eye[1] + dy * FLY_NEAR;
				p[2] = eye[2];
				if (fly_solid (p))
				{
					mx -= dx;
					my -= dy;
				}
			}
		if (!mx && !my)
			return;
		p[0] = feet[0] + (mx > 0 ? 8 : mx < 0 ? -8 : 0);
		p[1] = feet[1] + (my > 0 ? 8 : my < 0 ? -8 : 0);
		p[2] = feet[2];
		if (fly_eye_solid (p))
			return;
		VectorCopy (p, feet);
	}
}

/* A spot from the entity lump, kept as qog_tour keeps edict spots: not with
   the eye in solid, not closer than FLY_SPACING to one already kept. */
static void fly_addspot (vec3_t *spots, int *n, const float *p)
{
	int		i;
	vec3_t	d;

	vec3_t	q;

	if (fly_eye_solid (p))
		return;
	VectorCopy (p, q);
	fly_nudge (q);
	p = q;
	for (i = 0 ; i < *n ; i++)
	{
		VectorSubtract (p, spots[i], d);
		if (DotProduct (d, d) < FLY_SPACING * FLY_SPACING)
			return;
	}
	if (*n < QOG_FLY_MAX_POINTS)
	{
		VectorCopy (p, spots[*n]);
		(*n)++;
	}
}

static qboolean fly_wanted (const char *classname)
{
	return !strncmp (classname, "item_", 5) || !strncmp (classname, "weapon_", 7)
		|| !strncmp (classname, "monster_", 8);
}

/* Reads the entity lump twice: the player start first, then the rest in
   lump order. */
static int fly_findspots (vec3_t *spots)
{
	int		pass, n = 0;

	for (pass = 0 ; pass < 2 ; pass++)
	{
		char	*data = cl.worldmodel->entities;

		while (data)
		{
			char	classname[64];
			vec3_t	org;
			qboolean	hasorg = false;

			data = COM_Parse (data);
			if (!data || com_token[0] != '{')
				break;
			classname[0] = 0;
			for (;;)
			{
				char	key[64];

				data = COM_Parse (data);
				if (!data || com_token[0] == '}')
					break;
				Q_strncpy (key, com_token, sizeof(key) - 1);
				key[sizeof(key) - 1] = 0;
				data = COM_Parse (data);
				if (!data)
					break;
				if (!strcmp (key, "classname"))
				{
					Q_strncpy (classname, com_token, sizeof(classname) - 1);
					classname[sizeof(classname) - 1] = 0;
				}
				else if (!strcmp (key, "origin"))
				{
					org[0] = org[1] = org[2] = 0;
					sscanf (com_token, "%f %f %f", &org[0], &org[1], &org[2]);
					hasorg = true;
				}
			}
			if (!hasorg)
				continue;
			if (pass == 0 ? !strcmp (classname, "info_player_start") : fly_wanted (classname))
				fly_addspot (spots, &n, org);
		}
	}
	return n;
}

/* Shortest route from spot a to every spot, along lines of sight. */
static void fly_dijkstra (int n, const vec3_t *spots, const byte *vis, int a, float *dist, int *prev)
{
	qboolean	done[QOG_FLY_MAX_POINTS];
	int			i, k, u;
	vec3_t		d;

	for (i = 0 ; i < n ; i++)
	{
		dist[i] = -1;
		prev[i] = -1;
		done[i] = false;
	}
	dist[a] = 0;
	for (k = 0 ; k < n ; k++)
	{
		u = -1;
		for (i = 0 ; i < n ; i++)
			if (!done[i] && dist[i] >= 0 && (u < 0 || dist[i] < dist[u]))
				u = i;
		if (u < 0)
			break;
		done[u] = true;
		for (i = 0 ; i < n ; i++)
		{
			float	nd;

			if (done[i] || !(vis[u * n + i]))
				continue;
			VectorSubtract (spots[i], spots[u], d);
			nd = dist[u] + Length (d);
			if (dist[i] < 0 || nd < dist[i])
			{
				dist[i] = nd;
				prev[i] = u;
			}
		}
	}
}

#define FLY_MAX_ROUTE	(QOG_FLY_MAX_POINTS * 4)

/* Appends the route from cur to target (target included, cur not). Through
   seen spots when target is reachable, else straight (through a wall). */
static int fly_append (byte *route, int m, const int *prev, int target)
{
	byte	rev[QOG_FLY_MAX_POINTS];
	int		k = 0, i;

	for (i = target ; i >= 0 && prev[i] >= 0 && k < QOG_FLY_MAX_POINTS ; i = prev[i])
		rev[k++] = (byte)i;
	if (!k)
		rev[k++] = (byte)target;
	while (k > 0 && m < FLY_MAX_ROUTE)
		route[m++] = rev[--k];
	return m;
}

int qog_fly_use_world (void)
{
	int		himark = Hunk_HighMark ();
	vec3_t	*spots = Hunk_HighAllocName (QOG_FLY_MAX_POINTS * sizeof(vec3_t), "flyspots");
	byte	*vis, *route;
	qboolean	used[QOG_FLY_MAX_POINTS];
	float	dist[QOG_FLY_MAX_POINTS], t;
	int		prev[QOG_FLY_MAX_POINTS];
	int		n, i, j, m, cur, best, walls = 0;
	vec3_t	d;

	qog_fly_lightstyles_normal ();
	n = fly_findspots (spots);
	if (n < 2)
		Sys_Error ("qog_fly: %s has %i usable spots", cl.worldmodel->name, n);

	// which spots see each other (eye to eye, no solid on the line)
	vis = Hunk_HighAllocName (n * n, "flyvis");
	for (i = 0 ; i < n ; i++)
		for (j = i + 1 ; j < n ; j++)
			vis[i * n + j] = vis[j * n + i] = fly_clear (spots[i], spots[j]);

	// from the start, always to the unvisited spot nearest along the
	// sight lines, through spots already seen; the nearest straight
	// line when none can be reached; at the end back to the start
	route = Hunk_HighAllocName (FLY_MAX_ROUTE, "flyroute");
	memset (used, 0, sizeof(used));
	cur = 0;
	used[0] = true;
	route[0] = 0;
	m = 1;
	for (;;)
	{
		fly_dijkstra (n, spots, vis, cur, dist, prev);
		best = -1;
		for (i = 0 ; i < n ; i++)
			if (!used[i] && dist[i] >= 0 && (best < 0 || dist[i] < dist[best]))
				best = i;
		if (best < 0)
		{
			for (i = 0 ; i < n ; i++)
			{
				if (used[i])
					continue;
				VectorSubtract (spots[i], spots[cur], d);
				dist[i] = DotProduct (d, d);
				if (best < 0 || dist[i] < dist[best])
					best = i;
			}
			if (best < 0)
				break;		// all visited
			prev[best] = -1;
			walls++;
		}
		m = fly_append (route, m, prev, best);
		for (i = best ; i >= 0 ; i = prev[i])
			used[i] = true;
		cur = best;
		if (m >= FLY_MAX_ROUTE - QOG_FLY_MAX_POINTS)
			break;
	}
	fly_dijkstra (n, spots, vis, cur, dist, prev);
	if (dist[0] < 0)
		walls++;	// the loop closes through a wall
	else if (cur != 0)
	{
		m = fly_append (route, m, prev, 0);
		m--;		// the start itself begins the loop again
	}

	// the route on the low hunk, with the time at each point
	fly_n = m;
	fly_pts = Hunk_AllocName (m * sizeof(vec3_t), "flyroute");
	fly_start = Hunk_AllocName ((m + 1) * sizeof(float), "flytimes");
	fly_cut = Hunk_AllocName (m, "flycuts");
	for (i = 0 ; i < m ; i++)
	{
		VectorCopy (spots[route[i]], fly_pts[i]);
		fly_cut[i] = route[i] != route[(i + 1) % m] && !vis[route[i] * n + route[(i + 1) % m]];
	}
	Hunk_FreeToHighMark (himark);
	t = 0;
	for (i = 0 ; i < m ; i++)
	{
		float	seg;

		fly_start[i] = t;
		VectorSubtract (fly_pts[(i + 1) % m], fly_pts[i], d);
		seg = Length (d) / FLY_SPEED;
		if (seg < FLY_MIN_SEG)
			seg = FLY_MIN_SEG;
		if (fly_cut[i])
			seg = 0;	// a cut takes no time
		t += seg;
	}
	fly_start[m] = t;
	Con_Printf ("qog_fly: %s, %i spots, route of %i points, %i cuts, %.1f s loop\n",
				cl.worldmodel->name, n, m, walls, t);
	return m;
}

int qog_fly_numpoints (void) { return fly_n; }
void qog_fly_point (int i, vec3_t feet) { VectorCopy (fly_pts[i], feet); }
float qog_fly_point_time (int i) { return fly_start[i]; }
float qog_fly_loop_time (void) { return fly_start[fly_n]; }

static const float *fly_p (int i)
{
	return fly_pts[((i % fly_n) + fly_n) % fly_n];
}

static int fly_iscut (int i)
{
	return fly_cut[((i % fly_n) + fly_n) % fly_n];
}

static float fly_segtime (int i)
{
	i = ((i % fly_n) + fly_n) % fly_n;
	return fly_start[i + 1] - fly_start[i];
}

static void fly_angles_from (const vec3_t dir, vec3_t angles)
{
	float	flat = sqrt (dir[0] * dir[0] + dir[1] * dir[1]);
	float	yaw = atan2 (dir[1], dir[0]) * (180 / M_PI);
	float	pitch = -atan2 (dir[2], flat) * (180 / M_PI) * 0.5f;

	if (yaw < 0)
		yaw += 360;
	if (pitch > FLY_MAX_PITCH)
		pitch = FLY_MAX_PITCH;
	if (pitch < -FLY_MAX_PITCH)
		pitch = -FLY_MAX_PITCH;
	angles[PITCH] = pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}

/* Closed Catmull-Rom spline through the route: position and direction at
   time t. Returns the index of the point last passed. */
static int fly_eval (double t, vec3_t feet, vec3_t dir)
{
	int		i, j;
	float	u, u2, u3, len = fly_start[fly_n];

	t = fmod (t, len);
	if (t < 0)
		t += len;
	for (i = 0 ; i < fly_n - 1 && t >= fly_start[i + 1] ; i++)
		;
	u = (float)((t - fly_start[i]) / (fly_start[i + 1] - fly_start[i]));
	u2 = u * u;
	u3 = u2 * u;
	for (j = 0 ; j < 3 ; j++)
	{
		float	p1 = fly_p (i)[j], p2 = fly_p (i + 1)[j];
		// across a cut the spline ends as if the line went on straight
		float	p0 = fly_iscut (i - 1) ? 2 * p1 - p2 : fly_p (i - 1)[j];
		float	p3 = fly_iscut (i + 1) ? 2 * p2 - p1 : fly_p (i + 2)[j];

		feet[j] = 0.5f * (2 * p1 + (p2 - p0) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u2
			+ (3 * p1 - p0 - 3 * p2 + p3) * u3);
		dir[j] = 0.5f * ((p2 - p0) + 2 * (2 * p0 - 5 * p1 + 4 * p2 - p3) * u
			+ 3 * (3 * p1 - p0 - 3 * p2 + p3) * u2);
	}
	return i;
}

/* The view looks along the path's direction averaged over about a second,
   so it turns smoothly, also where the route turns back. */
#define FLY_LOOK_SAMPLES	4
#define FLY_LOOK_STEP		0.15

int qog_fly_camera (double t, vec3_t feet, vec3_t angles)
{
	vec3_t	dir, sum, p, d;
	int		k, pt, j, steps;
	double	tt, back, fwd;

	pt = fly_eval (t, feet, dir);

	// the look window stays on this side of the cuts before and after
	tt = fmod (t, fly_start[fly_n]);
	if (tt < 0)
		tt += fly_start[fly_n];
	back = tt - fly_start[pt];
	fwd = fly_start[pt + 1] - tt;
	for (j = pt - 1, steps = 0 ; steps < fly_n && !fly_iscut (j) ; j--, steps++)
		back += fly_segtime (j);
	if (steps == fly_n)
		back = 1e9;		// no cut anywhere
	for (j = pt + 1, steps = 0 ; steps < fly_n && !fly_iscut (j) ; j++, steps++)
		fwd += fly_segtime (j);
	if (steps == fly_n)
		fwd = 1e9;

	VectorCopy (vec3_origin, sum);
	for (k = -FLY_LOOK_SAMPLES ; k <= FLY_LOOK_SAMPLES ; k++)
	{
		double	o = k * FLY_LOOK_STEP;

		if (o < -back)
			o = -back;
		if (o > fwd - 0.001)
			o = fwd - 0.001;
		fly_eval (t + o, p, d);
		VectorNormalize (d);
		VectorAdd (sum, d, sum);
	}
	fly_angles_from (sum, angles);
	return pt;
}

void qog_fly_point_angles (int i, vec3_t angles)
{
	vec3_t	feet;

	qog_fly_camera (fly_start[i], feet, angles);
}

/* ---- drawing ----------------------------------------------------------- */

void qog_fly_render (const vec3_t feet, const vec3_t angles, double time)
{
	float	viewheight = QOG_FLY_EYE, bob = 0;

	cl.time = time;
	// the same steps as V_CalcRefdef for a still player standing at feet
	VectorCopy (feet, r_refdef.vieworg);
	r_refdef.vieworg[2] += viewheight + bob;
	r_refdef.vieworg[0] += 1.0/32;
	r_refdef.vieworg[1] += 1.0/32;
	r_refdef.vieworg[2] += 1.0/32;
	VectorCopy (angles, r_refdef.viewangles);
	R_RenderView ();
}

unsigned qog_fly_hash_view (void)
{
	unsigned	h = 2166136261u;
	int			x, y;

	for (y = 0 ; y < vid.height - sb_lines ; y++)
	{
		const byte	*row = vid.buffer + y * vid.rowbytes;

		for (x = 0 ; x < (int)vid.width ; x++)
			h = (h ^ row[x]) * 16777619u;
	}
	return h;
}

void qog_fly_overlay (const char *line1, const char *line2)
{
	int		y = vid.height - sb_lines + 12;

	if (line1)
		Draw_String (8, y, (char *)line1);
	if (line2)
		Draw_String (8, y + 12, (char *)line2);
}
