/* Level tour (PC development tool).
 *
 * Moves the player to every entity in a map (monsters, items, triggers,
 * doors, lifts) and looks four ways from each spot, so the renderer and
 * network peaks that qog_mem prints cover most of the level, not only the
 * spawn view. The player is put in noclip with god mode and notarget, so
 * nothing in the level reacts to it.
 *
 *   qog_tour <map> [<map> ...]   tour each map in turn, print qog_mem after
 *                                each one, reset the peaks, then quit
 *
 * Spots closer than TOUR_SPACING to one already chosen are skipped. Each
 * view is held for two frames, so the client has the new position before
 * the frame that counts.
 *
 *   qog_tour_fight <map> [<map> ...]   the same tour without notarget,
 *                                with every weapon and the rocket launcher
 *                                firing all the way, so monsters wake up,
 *                                fire back, die, gib and drop backpacks.
 *                                For edict peaks closer to real play. */
#include <string.h>
#include "quakedef.h"

#define TOUR_MAX_MAPS		48
#define TOUR_MAX_SPOTS		1024
#define TOUR_SPACING		96
#define TOUR_FRAMES_PER_VIEW	2

static char		tour_maps[TOUR_MAX_MAPS][MAX_QPATH];
static int		tour_nummaps, tour_map;
static enum { TOUR_IDLE, TOUR_LOADING, TOUR_RUNNING } tour_state;
static vec3_t	tour_spots[TOUR_MAX_SPOTS];
static int		tour_numspots, tour_spot, tour_view;
static qboolean	tour_fight;

static void Tour_StartMap (void)
{
	Cbuf_AddText (va ("map %s\n", tour_maps[tour_map]));
	tour_state = TOUR_LOADING;
}

static void Tour_AddSpot (vec3_t p)
{
	int		i;
	vec3_t	d, eye;

	// the view is drawn from origin + view_ofs. From inside solid the whole
	// map counts as visible, a view no player can ever have, so skip it.
	VectorCopy (p, eye);
	eye[2] += 22;
	if (SV_PointContents (eye) == CONTENTS_SOLID)
		return;

	for (i = 0 ; i < tour_numspots ; i++)
	{
		VectorSubtract (p, tour_spots[i], d);
		if (DotProduct (d, d) < TOUR_SPACING * TOUR_SPACING)
			return;
	}
	if (tour_numspots < TOUR_MAX_SPOTS)
	{
		// VectorCopy is a macro that names its target three times
		VectorCopy (p, tour_spots[tour_numspots]);
		tour_numspots++;
	}
}

/* QuakeOG (Agent AD): edict fields by QuakeC field number, either layout */
static int Tour_GetInt (edict_t *ent, int ofs)
{
#ifdef QOG_EDICT_SPARSE
	return ED_FieldGet (ent, ofs);
#else
	return ((int *)&ent->v)[ofs];
#endif
}

static void Tour_SetFloat (edict_t *ent, int ofs, float f)
{
#ifdef QOG_EDICT_SPARSE
	ED_FieldSet (ent, ofs, *(int *)&f);
#else
	((float *)&ent->v)[ofs] = f;
#endif
}

/* A level exit, or a teleporter whose target no edict names. e4m5 and e4m8
   each have one aimed at an info_null, which QuakeC removes at spawn, so
   touching it is "couldn't find target" and a Host_Error in id's game too. */
static qboolean Tour_Disarm (edict_t *ent)
{
	char	*cls = pr_strings + Tour_GetInt (ent, EV_OFS(classname));
	char	*target;
	int		i;
	edict_t	*e;

	if (!Q_strcmp (cls, "trigger_changelevel"))
		return true;
	if (Q_strcmp (cls, "trigger_teleport"))
		return false;
	target = pr_strings + Tour_GetInt (ent, EV_OFS(target));
	for (i = 1 ; i < sv.num_edicts ; i++)
	{
		e = EDICT_NUM (i);
		if (!e->free && !Q_strcmp (pr_strings + Tour_GetInt (e, EV_OFS(targetname)), target))
			return false;
	}
	Con_Printf ("qog_tour: %s has a teleporter to %s, which is gone, taken out\n", sv.name, target);
	return true;
}

static void Tour_FindSpots (void)
{
	int		i;
	edict_t	*ent;
	vec3_t	p;

	tour_numspots = 0;
	Tour_AddSpot (sv_player->v.origin);
	for (i = 1 ; i < sv.num_edicts ; i++)
	{
		ent = EDICT_NUM (i);
		if (ent->free || ent == sv_player)
			continue;
		// QuakeOG (Agent AD): a spot inside a level exit changed the level
		// in the middle of a tour, and a teleporter whose target is gone
		// stops the game, so the tour takes both out first
		if (Tour_Disarm (ent))
		{
			Tour_SetFloat (ent, EV_OFS(solid), SOLID_NOT);
			SV_LinkEdict (ent, false);
		}
		// brush entities have their origin at 0 0 0, so use the box centre
		VectorAdd (ent->v.absmin, ent->v.absmax, p);
		VectorScale (p, 0.5, p);
		Tour_AddSpot (p);
	}
	Con_Printf ("qog_tour: %s, %d spots from %d edicts\n", sv.name, tour_numspots, sv.num_edicts);
}

static void cmd_qog_tour (void)
{
	int		i;

	if (Cmd_Argc () < 2)
	{
		Con_Printf ("qog_tour <map> [<map> ...]\n");
		return;
	}
	tour_nummaps = 0;
	for (i = 1 ; i < Cmd_Argc () && tour_nummaps < TOUR_MAX_MAPS ; i++)
		Q_strncpy (tour_maps[tour_nummaps++], Cmd_Argv (i), MAX_QPATH - 1);
	tour_map = 0;
	tour_fight = !Q_strcmp (Cmd_Argv (0), "qog_tour_fight");
	Tour_StartMap ();
}

/* Called from the PC main loop after every Host_Frame. */
void qog_tour_frame (void)
{
	vec3_t	ang;

	if (tour_state == TOUR_IDLE)
		return;

	if (tour_state == TOUR_LOADING)
	{
#ifdef QOG_SPLIT_SERVER
		// QuakeOG (Agent S): the split game's server half runs the tour, its
		// client is in the game once it has spawned. Text added here joins
		// the client's command buffer (engine/net_link.c).
		if (!sv.active || !svs.clients[0].spawned || Q_strcmp (sv.name, tour_maps[tour_map]))
			return;
#else
		if (!sv.active || cls.signon != SIGNONS || Q_strcmp (sv.name, tour_maps[tour_map]))
			return;
#endif
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		sv_player->v.flags = (int)sv_player->v.flags | FL_GODMODE | (tour_fight ? 0 : FL_NOTARGET);
		if (tour_fight)
			Cbuf_AddText ("impulse 9\nimpulse 7\n+attack\n");
		Tour_FindSpots ();
		tour_spot = tour_view = 0;
		tour_state = TOUR_RUNNING;
	}

	if (tour_spot >= tour_numspots)
	{
#ifdef QOG_SPLIT_SERVER
		Cbuf_AddText ("qog_mem hunk\n");	// QuakeOG (Agent S): the client's hunk blocks of this level
#endif
		Cbuf_AddText (tour_fight ? "-attack\nqog_mem\nqog_mem reset\n" : "qog_mem\nqog_mem reset\n");
		if (++tour_map < tour_nummaps)
			Tour_StartMap ();
		else
		{
			tour_state = TOUR_IDLE;
			Cbuf_AddText ("qog_quit\n");
		}
		return;
	}

	ang[0] = 0;
	ang[1] = (tour_view / TOUR_FRAMES_PER_VIEW) * 90;
	ang[2] = 0;
	VectorCopy (tour_spots[tour_spot], sv_player->v.origin);
	VectorCopy (ang, sv_player->v.angles);
	EV_SETVECTOR(sv_player, v_angle, ang);	// id's layout or QOG_EDICT_SPARSE
	EV_SETFLOAT(sv_player, fixangle, 1);
	SV_LinkEdict (sv_player, false);

	if (++tour_view >= 4 * TOUR_FRAMES_PER_VIEW)
	{
		tour_view = 0;
		tour_spot++;
	}
}

void qog_tour_register (void)
{
	Cmd_AddCommand ("qog_tour", cmd_qog_tour);
	Cmd_AddCommand ("qog_tour_fight", cmd_qog_tour);
}
