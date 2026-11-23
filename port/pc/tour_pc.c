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
 * the frame that counts. */
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
		if (!sv.active || cls.signon != SIGNONS || Q_strcmp (sv.name, tour_maps[tour_map]))
			return;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		sv_player->v.flags = (int)sv_player->v.flags | FL_GODMODE | FL_NOTARGET;
		Tour_FindSpots ();
		tour_spot = tour_view = 0;
		tour_state = TOUR_RUNNING;
	}

	if (tour_spot >= tour_numspots)
	{
		Cbuf_AddText ("qog_mem\nqog_mem reset\n");
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
	VectorCopy (ang, sv_player->v.v_angle);
	sv_player->v.fixangle = 1;
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
}
