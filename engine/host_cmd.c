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

#include "quakedef.h"
#ifdef QOG_NET_LINK
#include "net_link.h"	// QuakeOG: the split game (net_link.c)
#endif

// QuakeOG: in the split game (QOG_NET_LINK) the client half keeps the
// console commands that act on the client, plus the client's part of map,
// save and load. The server half keeps the rest, and console lines the
// client does not know run there (Link_ForwardCommand). Commands that
// forward themselves to the server in id's code (god, kill, ...) are
// Cmd_ForwardToServer on the client.
#ifdef QOG_SPLIT_SERVER
#define	SPLIT_DEMOPLAYBACK	false	// the server half never plays a demo
#else
#define	SPLIT_DEMOPLAYBACK	cls.demoplayback
#endif

extern cvar_t	pausable;

int	current_skill;

void Mod_Print (void);

/*
==================
Host_Quit_f
==================
*/

extern void M_Menu_Quit_f (void);

#ifndef QOG_SPLIT_SERVER
void Host_Quit_f (void)
{
	if (key_dest != key_console && cls.state != ca_dedicated)
	{
		M_Menu_Quit_f ();
		return;
	}
	CL_Disconnect ();
	Host_ShutdownServer(false);

	Sys_Quit ();
}
#endif


/*
==================
Host_Status_f
==================
*/
#ifndef QOG_SPLIT_CLIENT	// QuakeOG: server half only, up to Host_Ping_f
void Host_Status_f (void)
{
	client_t	*client;
	int			seconds;
	int			minutes;
	int			hours = 0;
	int			j;
	void		(*print) (char *fmt, ...);
	
	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
		print = Con_Printf;
	}
	else
		print = SV_ClientPrintf;

	print ("host:    %s\n", Cvar_VariableString ("hostname"));
	print ("version: %4.2f\n", VERSION);
	if (tcpipAvailable)
		print ("tcp/ip:  %s\n", my_tcpip_address);
	if (ipxAvailable)
		print ("ipx:     %s\n", my_ipx_address);
	print ("map:     %s\n", sv.name);
	print ("players: %i active (%i max)\n\n", net_activeconnections, svs.maxclients);
	for (j=0, client = svs.clients ; j<svs.maxclients ; j++, client++)
	{
		if (!client->active)
			continue;
		seconds = (int)(net_time - client->netconnection->connecttime);
		minutes = seconds / 60;
		if (minutes)
		{
			seconds -= (minutes * 60);
			hours = minutes / 60;
			if (hours)
				minutes -= (hours * 60);
		}
		else
			hours = 0;
		print ("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j+1, client->name, (int)EV_FLOAT(client->edict, frags), hours, minutes, seconds);
		print ("   %s\n", client->netconnection->address);
	}
}


/*
==================
Host_God_f

Sets client to godmode
==================
*/
void Host_God_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch && !host_client->privileged)
		return;

	sv_player->v.flags = (int)sv_player->v.flags ^ FL_GODMODE;
	if (!((int)sv_player->v.flags & FL_GODMODE) )
		SV_ClientPrintf ("godmode OFF\n");
	else
		SV_ClientPrintf ("godmode ON\n");
}

void Host_Notarget_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch && !host_client->privileged)
		return;

	sv_player->v.flags = (int)sv_player->v.flags ^ FL_NOTARGET;
	if (!((int)sv_player->v.flags & FL_NOTARGET) )
		SV_ClientPrintf ("notarget OFF\n");
	else
		SV_ClientPrintf ("notarget ON\n");
}

qboolean noclip_anglehack;

void Host_Noclip_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch && !host_client->privileged)
		return;

	if (sv_player->v.movetype != MOVETYPE_NOCLIP)
	{
		noclip_anglehack = true;
		sv_player->v.movetype = MOVETYPE_NOCLIP;
		SV_ClientPrintf ("noclip ON\n");
	}
	else
	{
		noclip_anglehack = false;
		sv_player->v.movetype = MOVETYPE_WALK;
		SV_ClientPrintf ("noclip OFF\n");
	}
}

/*
==================
Host_Fly_f

Sets client to flymode
==================
*/
void Host_Fly_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch && !host_client->privileged)
		return;

	if (sv_player->v.movetype != MOVETYPE_FLY)
	{
		sv_player->v.movetype = MOVETYPE_FLY;
		SV_ClientPrintf ("flymode ON\n");
	}
	else
	{
		sv_player->v.movetype = MOVETYPE_WALK;
		SV_ClientPrintf ("flymode OFF\n");
	}
}


/*
==================
Host_Ping_f

==================
*/
void Host_Ping_f (void)
{
	int		i, j;
	float	total;
	client_t	*client;
	
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	SV_ClientPrintf ("Client ping times:\n");
	for (i=0, client = svs.clients ; i<svs.maxclients ; i++, client++)
	{
		if (!client->active)
			continue;
		total = 0;
		for (j=0 ; j<NUM_PING_TIMES ; j++)
			total+=client->ping_times[j];
		total /= NUM_PING_TIMES;
		SV_ClientPrintf ("%4i %s\n", (int)(total*1000), client->name);
	}
}
#endif	// QOG_SPLIT_CLIENT

/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/


/*
======================
Host_Map_f

handle a 
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
void Host_Map_f (void)
{
	int		i;
	char	name[MAX_QPATH];

	if (cmd_source != src_command)
		return;

#ifndef QOG_SPLIT_SERVER	// QuakeOG: the client's part
	cls.demonum = -1;		// stop demo loop in case this fails

	CL_Disconnect ();
#endif
	Host_ShutdownServer(false);

#ifndef QOG_SPLIT_SERVER
	key_dest = key_game;			// remove console or menu
	SCR_BeginLoadingPlaque ();

	cls.mapstring[0] = 0;
	for (i=0 ; i<Cmd_Argc() ; i++)
	{
		strcat (cls.mapstring, Cmd_Argv(i));
		strcat (cls.mapstring, " ");
	}
	strcat (cls.mapstring, "\n");
#endif

#ifdef QOG_SPLIT_CLIENT
	// QuakeOG: the spawn parms are taken now, as Cmd_Argv will not last;
	// the server spawns the map and sends "connect local" back if it ran
	strcpy (cls.spawnparms, "");
	for (i=2 ; i<Cmd_Argc() ; i++)
	{
		strcat (cls.spawnparms, Cmd_Argv(i));
		strcat (cls.spawnparms, " ");
	}
	Link_ForwardCommand (Cmd_Args () ? va ("map %s", Cmd_Args ()) : "map");
#else
	svs.serverflags = 0;			// haven't completed an episode yet
	strcpy (name, Cmd_Argv(1));
#ifdef QUAKE2
	SV_SpawnServer (name, NULL);
#else
	SV_SpawnServer (name);
#endif
	if (!sv.active)
		return;

#ifdef QOG_SPLIT_SERVER
	Link_ServerClientExec ("connect local");	// QuakeOG: the client connects now
#else
	if (cls.state != ca_dedicated)
	{
		strcpy (cls.spawnparms, "");

		for (i=2 ; i<Cmd_Argc() ; i++)
		{
			strcat (cls.spawnparms, Cmd_Argv(i));
			strcat (cls.spawnparms, " ");
		}

		Cmd_ExecuteString ("connect local", src_command);
	}
#endif
#endif	// QOG_SPLIT_CLIENT
}

#if defined(QOG_BOOT_ENTRY) && !defined(QOG_SPLIT_SERVER)
/*
==================
Host_QogBoot_f

QuakeOG (Agent AG): "qog_boot" starts the level the data image names
(COM_BootMap: qogboot.txt, else e1m1), so one board app starts any
episode's image. The board's command line runs it where it ran "map e1m1".
==================
*/
void Host_QogBoot_f (void)
{
	char	name[MAX_QPATH];

	COM_BootMap (name);
	if (!name[0])
	{
		Con_Printf ("qog_boot: the data image holds no level\n");
		return;
	}
	Cbuf_InsertText (va ("map %s\n", name));
}
#endif

#if defined(QOG_MISSING_MAPS) && defined(QOG_SPLIT_CLIENT)
/*
==================
Host_QogLoadFailed_f

QuakeOG (Agent AG): the server half did not load a save, its level is not
in the data image. The menu put up the loading plaque before "load", so
take it down now (id would wait 60 s) and say why. The level goes on.
==================
*/
void Host_QogLoadFailed_f (void)
{
	SCR_EndLoadingPlaque ();
	SCR_CenterPrint ("That save's level is not\nin this data image\n");
}
#endif

#if defined(QOG_MISSING_MAPS) && !defined(QOG_SPLIT_CLIENT)
/*
==================
Host_QogNewSpawnparms

QuakeOG (Agent AG): SV_SaveSpawnparms with the parms of a new player
(SetNewParms), what id's start gives a player who finished an episode.
==================
*/
static void Host_QogNewSpawnparms (void)
{
	int		i, j;

	svs.serverflags = pr_global_struct->serverflags;
	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
	{
		if (!host_client->active)
			continue;
		pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
		PR_ExecuteProgram (pr_global_struct->SetNewParms);
		for (j=0 ; j<NUM_SPAWN_PARMS ; j++)
			host_client->spawn_parms[j] = (&pr_global_struct->parm1)[j];
	}
}

/*
==================
Host_QogMissingLevel

QuakeOG (Agent AG): a board data image holds one episode. A level exit to
a level it does not hold (start after the episode's last level, a gate of
start to another episode, a secret level left out) would leave the server
without a level and the client waiting. It goes to the image's own start
level instead (COM_BootMap), with a new player's items, as id's start
does after an episode. Returns false when name is in the packs: then
nothing changes.
==================
*/
static qboolean Host_QogMissingLevel (char *name)
{
	char	level[MAX_QPATH];

	if (Q_strlen (name) < MAX_QPATH - 10 && COM_HasFile (va ("maps/%s.bsp", name)))
		return false;
	COM_BootMap (level);
	Con_Printf ("changelevel: %s is not in the data image, going to %s\n", name, level[0] ? level : "no level");
	if (!level[0])
		return true;
	Host_QogNewSpawnparms ();
	SV_SpawnServer (level);
	return true;
}
#endif

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
#ifndef QOG_SPLIT_CLIENT	// QuakeOG: server half only, changelevel and restart
void Host_Changelevel_f (void)
{
#ifdef QUAKE2
	char	level[MAX_QPATH];
	char	_startspot[MAX_QPATH];
	char	*startspot;

	if (Cmd_Argc() < 2)
	{
		Con_Printf ("changelevel <levelname> : continue game on a new level\n");
		return;
	}
	if (!sv.active || cls.demoplayback)
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}

	strcpy (level, Cmd_Argv(1));
	if (Cmd_Argc() == 2)
		startspot = NULL;
	else
	{
		strcpy (_startspot, Cmd_Argv(2));
		startspot = _startspot;
	}

	SV_SaveSpawnparms ();
	SV_SpawnServer (level, startspot);
#else
	char	level[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("changelevel <levelname> : continue game on a new level\n");
		return;
	}
	if (!sv.active || SPLIT_DEMOPLAYBACK)	// QuakeOG: SPLIT_DEMOPLAYBACK was cls.demoplayback
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}
#ifdef QOG_MISSING_MAPS
	if (Host_QogMissingLevel (Cmd_Argv(1)))
		return;
#endif
	SV_SaveSpawnparms ();
	strcpy (level, Cmd_Argv(1));
	SV_SpawnServer (level);
#endif
}

/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
void Host_Restart_f (void)
{
	char	mapname[MAX_QPATH];
#ifdef QUAKE2
	char	startspot[MAX_QPATH];
#endif

	if (SPLIT_DEMOPLAYBACK || !sv.active)	// QuakeOG: was cls.demoplayback
		return;

	if (cmd_source != src_command)
		return;
	strcpy (mapname, sv.name);	// must copy out, because it gets cleared
								// in sv_spawnserver
#ifdef QUAKE2
	strcpy(startspot, sv.startspot);
	SV_SpawnServer (mapname, startspot);
#else
	SV_SpawnServer (mapname);
#endif
}
#endif	// QOG_SPLIT_CLIENT

#ifndef QOG_SPLIT_SERVER	// QuakeOG: client half only, reconnect and connect
/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels
==================
*/
void Host_Reconnect_f (void)
{
	SCR_BeginLoadingPlaque ();
	cls.signon = 0;		// need new connection messages
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
void Host_Connect_f (void)
{
	char	name[MAX_QPATH];
	
	cls.demonum = -1;		// stop demo loop in case this fails
	if (cls.demoplayback)
	{
		CL_StopPlayback ();
		CL_Disconnect ();
	}
	strcpy (name, Cmd_Argv(1));
	CL_EstablishConnection (name);
	Host_Reconnect_f ();
}
#endif	// QOG_SPLIT_SERVER


/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define	SAVEGAME_VERSION	5

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current 
===============
*/
void Host_SavegameComment (char *text)
{
	int		i;
	char	kills[20];

#ifdef QOG_SPLIT_SERVER
	// QuakeOG: the client made it from its state and sent it with the save
	memcpy (text, link_save_comment, SAVEGAME_COMMENT_LENGTH + 1);
	return;
#endif
	for (i=0 ; i<SAVEGAME_COMMENT_LENGTH ; i++)
		text[i] = ' ';
	memcpy (text, cl.levelname, strlen(cl.levelname));
	sprintf (kills,"kills:%3i/%3i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
	memcpy (text+22, kills, strlen(kills));
// convert space to _ to make stdio happy
	for (i=0 ; i<SAVEGAME_COMMENT_LENGTH ; i++)
		if (text[i] == ' ')
			text[i] = '_';
	text[SAVEGAME_COMMENT_LENGTH] = '\0';
}


/*
===============
Host_Savegame_f
===============
*/
#ifdef QOG_SPLIT_CLIENT
// QuakeOG: the server saves. It needs the client's intermission flag and
// the comment, which come from client state.
void Host_Savegame_f (void)
{
	char	comment[SAVEGAME_COMMENT_LENGTH+1];

	if (cmd_source != src_command)
		return;
	Host_SavegameComment (comment);
	Link_SaveInfo (cl.intermission, comment);
	Link_ForwardCommand (Cmd_Argc () > 1 ? va ("save %s", Cmd_Args ()) : "save");
}
#else
#ifdef QOG_SPLIT_SERVER
#define	cl_intermission	link_cl_intermission	// QuakeOG: sent with the save
#else
#define	cl_intermission	cl.intermission
#endif
void Host_Savegame_f (void)
{
	char	name[256];
	FILE	*f;
	int		i;
	char	comment[SAVEGAME_COMMENT_LENGTH+1];

	if (cmd_source != src_command)
		return;

	if (!sv.active)
	{
		Con_Printf ("Not playing a local game.\n");
		return;
	}

	if (cl_intermission)	// QuakeOG: was cl.intermission
	{
		Con_Printf ("Can't save in intermission.\n");
		return;
	}

	if (svs.maxclients != 1)
	{
		Con_Printf ("Can't save multiplayer games.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("save <savename> : save a game\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}
		
	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active && (EV_FLOAT(svs.clients[i].edict, health) <= 0) )
		{
			Con_Printf ("Can't savegame with a dead player\n");
			return;
		}
	}

	sprintf (name, "%s/%s", com_gamedir, Cmd_Argv(1));
	COM_DefaultExtension (name, ".sav");
	
	Con_Printf ("Saving game to %s...\n", name);
	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}
	
	fprintf (f, "%i\n", SAVEGAME_VERSION);
	Host_SavegameComment (comment);
	fprintf (f, "%s\n", comment);
	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
		fprintf (f, "%f\n", svs.clients->spawn_parms[i]);
	fprintf (f, "%d\n", current_skill);
	fprintf (f, "%s\n", sv.name);
	fprintf (f, "%f\n",sv.time);

// write the light styles

	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "%s\n", sv.lightstyles[i]);
		else
			fprintf (f,"m\n");
	}


	ED_WriteGlobals (f);
	for (i=0 ; i<sv.num_edicts ; i++)
	{
		ED_Write (f, EDICT_NUM(i));
		fflush (f);
	}
	fclose (f);
	Con_Printf ("done.\n");
}
#undef	cl_intermission
#endif	// QOG_SPLIT_CLIENT


/*
===============
Host_Loadgame_f
===============
*/
// QuakeOG: the 32 KB line buffer was on the stack. It now lives in a
// temporary block on the high hunk, taken once for the header and again
// after the map has loaded, so the level loads with that space free.
#ifdef QOG_SPLIT_CLIENT
// QuakeOG: the server loads. Its part that disconnects and reconnects the
// client comes back as "disconnect" and "connect local".
void Host_Loadgame_f (void)
{
	if (cmd_source != src_command)
		return;
	if (Cmd_Argc() == 2)
		cls.demonum = -1;		// stop demo loop in case this fails
	Link_ForwardCommand (Cmd_Argc () > 1 ? va ("load %s", Cmd_Args ()) : "load");
}
#else
#ifdef QOG_LOADGAME_STRSIZE
// QuakeOG (Agent R): the split server has no z-buffer to borrow, so the
// buffer is sized for the biggest brace block of a save instead (a fight
// save of E1M1: the globals, 2,485 bytes). A bigger block still stops with
// id's "Loadgame buffer overflow".
#define LOADGAME_STRSIZE	QOG_LOADGAME_STRSIZE
#else
#define LOADGAME_STRSIZE	32768
#endif
#ifdef QOG_LOADGAME_ZTEMP
// QuakeOG: the line buffer at the end of the z-buffer, which is free between
// frames (r_shared.h): a load runs from the command buffer, before the
// frame is drawn, and while it runs the client is not signed on, so no 3D
// view is drawn. The status bar rows at the start of the z-buffer stay
// clear. A z-buffer too small for both takes the high hunk block as before.
extern byte	*r_ztemp_base;
extern int	r_ztemp_bytes;
#define LOADGAME_HUDBYTES	(320 * 48)
#define LOADGAME_ZROOM		(r_ztemp_base && r_ztemp_bytes >= LOADGAME_STRSIZE + LOADGAME_HUDBYTES)
#define LOADGAME_ALLOC()	(LOADGAME_ZROOM ? (char *)(r_ztemp_base + r_ztemp_bytes - LOADGAME_STRSIZE) : (char *)Hunk_HighAllocName (LOADGAME_STRSIZE, "loadgame"))
#else
#define LOADGAME_ALLOC()	((char *)Hunk_HighAllocName (LOADGAME_STRSIZE, "loadgame"))
#endif

/*
===============
Host_LoadgameEdicts

QuakeOG: the light styles, the globals and the edicts of a save, read with
the line buffer str (id's code from Host_Loadgame_f). Returns the edicts.
===============
*/
static int Host_LoadgameEdicts (FILE *f, char *str)
{
	char	*start;
	int		i, r;
	edict_t	*ent;
	int		entnum;

// load the light styles

	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		fscanf (f, "%s\n", str);
#ifdef QOG_HUNK_PACKED
		sv.lightstyles[i] = Hunk_AllocPacked (strlen(str)+1, 1);	// QuakeOG: packed, zone.c
#else
		sv.lightstyles[i] = Hunk_Alloc (strlen(str)+1);
#endif
		strcpy (sv.lightstyles[i], str);
	}

// load the edicts out of the savegame file
	entnum = -1;		// -1 is the globals
	while (!feof(f))
	{
		for (i=0 ; i<LOADGAME_STRSIZE-1 ; i++)	// QuakeOG: was sizeof(str)
		{
			r = fgetc (f);
			if (r == EOF || !r)
				break;
			str[i] = r;
			if (r == '}')
			{
				i++;
				break;
			}
		}
		if (i == LOADGAME_STRSIZE-1)	// QuakeOG: was sizeof(str)
			Sys_Error ("Loadgame buffer overflow");
		str[i] = 0;
		start = str;
		start = COM_Parse(str);
		if (!com_token[0])
			break;		// end of file
		if (strcmp(com_token,"{"))
			Sys_Error ("First token isn't a brace");
			
		if (entnum == -1)
		{	// parse the global vars
			ED_ParseGlobals (start);
		}
		else
		{	// parse an edict

			ent = EDICT_NUM(entnum);
			ED_CLEARFIELDS(ent);	// QuakeOG: was memset (&ent->v, 0, progs->entityfields * 4)
			ent->free = false;
			ED_ParseEdict (start, ent);
	
		// link it into the bsp tree
			if (!ent->free)
				SV_LinkEdict (ent, false);
		}

		entnum++;
	}
	
	return entnum;
}

#ifdef QOG_LOADGAME_STACKBUF
/*
===============
Host_LoadgameStack

QuakeOG (Agent AG): the line buffer on the stack of a frame of its own,
taken once the map has spawned, not on the high hunk under the loaded
level: a load needed the level's hunk plus the 8 KB buffer, which was the
server's peak (e2m7 with sound 179 KB of the board's 174). The deep chains
of the load (SV_SpawnServer, its physics frames) have returned by then.
===============
*/
#if defined(_MSC_VER)
#define QOG_NOINLINE_LG	__declspec(noinline)
#else
#define QOG_NOINLINE_LG	__attribute__((noinline))
#endif
static QOG_NOINLINE_LG int Host_LoadgameStack (FILE *f)
{
	char	str[LOADGAME_STRSIZE];

	return Host_LoadgameEdicts (f, str);
}
#endif

void Host_Loadgame_f (void)
{
	char	name[MAX_OSPATH];
	FILE	*f;
	char	mapname[MAX_QPATH];
	float	time, tfloat;
	char	*str, *start;	// QuakeOG: str was a stack array
	int		himark;
	int		i, r;
	edict_t	*ent;
	int		entnum;
	int		version;
	float			spawn_parms[NUM_SPAWN_PARMS];

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("load <savename> : load a game\n");
		return;
	}

#ifndef QOG_SPLIT_SERVER
	cls.demonum = -1;		// stop demo loop in case this fails
#endif

	sprintf (name, "%s/%s", com_gamedir, Cmd_Argv(1));
	COM_DefaultExtension (name, ".sav");
	
// we can't call SCR_BeginLoadingPlaque, because too much stack space has
// been used.  The menu calls it before stuffing loadgame command
//	SCR_BeginLoadingPlaque ();

	Con_Printf ("Loading game from %s...\n", name);
	f = fopen (name, "r");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	fscanf (f, "%i\n", &version);
	if (version != SAVEGAME_VERSION)
	{
		fclose (f);
		Con_Printf ("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
		return;
	}
	himark = Hunk_HighMark ();	// QuakeOG: buffer for the comment line
	str = LOADGAME_ALLOC ();
	if (!str)
		Sys_Error ("Host_Loadgame_f: no room for the line buffer");
	fscanf (f, "%s\n", str);
	Hunk_FreeToHighMark (himark);	// QuakeOG: free while the map loads
	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
		fscanf (f, "%f\n", &spawn_parms[i]);
// this silliness is so we can load 1.06 save files, which have float skill values
	fscanf (f, "%f\n", &tfloat);
#ifndef QOG_MISSING_MAPS	// QuakeOG (Agent AG): set once the level is known to be here
	current_skill = (int)(tfloat + 0.1);
	Cvar_SetValue ("skill", (float)current_skill);
#endif

#ifdef QUAKE2
	Cvar_SetValue ("deathmatch", 0);
	Cvar_SetValue ("coop", 0);
	Cvar_SetValue ("teamplay", 0);
#endif

	fscanf (f, "%s\n",mapname);
	fscanf (f, "%f\n",&time);

#ifdef QOG_MISSING_MAPS
	// QuakeOG (Agent AG): the saves on main's volume outlive the episode
	// image they were made with. A save of a level this image does not
	// hold is not loaded and the game goes on as it was, before anything
	// is stopped (SV_SpawnServer would leave no level and the client
	// waiting under the loading plaque).
	if (Q_strlen (mapname) >= MAX_QPATH - 10 || !COM_HasFile (va ("maps/%s.bsp", mapname)))
	{
		fclose (f);
		Con_Printf ("%s is not in the data image, the save is not loaded\n", mapname);
#ifdef QOG_SPLIT_SERVER
		Link_ServerClientExec ("qog_loadfailed");
#else
		SCR_EndLoadingPlaque ();
		SCR_CenterPrint ("That save's level is not\nin this data image\n");
#endif
		return;
	}
	current_skill = (int)(tfloat + 0.1);
	Cvar_SetValue ("skill", (float)current_skill);
#endif

#ifdef QOG_SPLIT_SERVER
	// QuakeOG: CL_Disconnect_f, the client's part on the client, the
	// server's part (it closes the connection and stops this server) here
	Link_ServerClientExec ("disconnect");
	Link_ServerPeerClosed ();
#else
	CL_Disconnect_f ();
#endif
	
#ifdef QUAKE2
	SV_SpawnServer (mapname, NULL);
#else
	SV_SpawnServer (mapname);
#endif
	if (!sv.active)
	{
		Con_Printf ("Couldn't load map\n");
		return;
	}
	sv.paused = true;		// pause until all clients connect
	sv.loadgame = true;

#ifdef QOG_LOADGAME_STACKBUF
	entnum = Host_LoadgameStack (f);
#else
	himark = Hunk_HighMark ();	// QuakeOG: buffer for the rest of the file
	str = LOADGAME_ALLOC ();
	if (!str)
		Sys_Error ("Host_Loadgame_f: no room for the line buffer");
	entnum = Host_LoadgameEdicts (f, str);
#endif
	sv.num_edicts = entnum;
	sv.time = time;

	fclose (f);
#ifndef QOG_LOADGAME_STACKBUF
	Hunk_FreeToHighMark (himark);	// QuakeOG: done with the line buffer
#endif

	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
		svs.clients->spawn_parms[i] = spawn_parms[i];

#ifdef QOG_SPLIT_SERVER
	Link_ServerClientExec ("connect local");	// QuakeOG: CL_EstablishConnection and Host_Reconnect_f
#else
	if (cls.state != ca_dedicated)
	{
		CL_EstablishConnection ("local");
		Host_Reconnect_f ();
	}
#endif
}
#endif	// QOG_SPLIT_CLIENT

#ifdef QUAKE2
void SaveGamestate()
{
	char	name[256];
	FILE	*f;
	int		i;
	char	comment[SAVEGAME_COMMENT_LENGTH+1];
	edict_t	*ent;

	sprintf (name, "%s/%s.gip", com_gamedir, sv.name);
	
	Con_Printf ("Saving game to %s...\n", name);
	f = fopen (name, "w");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}
	
	fprintf (f, "%i\n", SAVEGAME_VERSION);
	Host_SavegameComment (comment);
	fprintf (f, "%s\n", comment);
//	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
//		fprintf (f, "%f\n", svs.clients->spawn_parms[i]);
	fprintf (f, "%f\n", skill.value);
	fprintf (f, "%s\n", sv.name);
	fprintf (f, "%f\n", sv.time);

// write the light styles

	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		if (sv.lightstyles[i])
			fprintf (f, "%s\n", sv.lightstyles[i]);
		else
			fprintf (f,"m\n");
	}


	for (i=svs.maxclients+1 ; i<sv.num_edicts ; i++)
	{
		ent = EDICT_NUM(i);
		if ((int)ent->v.flags & FL_ARCHIVE_OVERRIDE)
			continue;
		fprintf (f, "%i\n",i);
		ED_Write (f, ent);
		fflush (f);
	}
	fclose (f);
	Con_Printf ("done.\n");
}

int LoadGamestate(char *level, char *startspot)
{
	char	name[MAX_OSPATH];
	FILE	*f;
	char	mapname[MAX_QPATH];
	float	time, sk;
	char	str[32768], *start;
	int		i, r;
	edict_t	*ent;
	int		entnum;
	int		version;
//	float	spawn_parms[NUM_SPAWN_PARMS];

	sprintf (name, "%s/%s.gip", com_gamedir, level);
	
	Con_Printf ("Loading game from %s...\n", name);
	f = fopen (name, "r");
	if (!f)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return -1;
	}

	fscanf (f, "%i\n", &version);
	if (version != SAVEGAME_VERSION)
	{
		fclose (f);
		Con_Printf ("Savegame is version %i, not %i\n", version, SAVEGAME_VERSION);
		return -1;
	}
	fscanf (f, "%s\n", str);
//	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
//		fscanf (f, "%f\n", &spawn_parms[i]);
	fscanf (f, "%f\n", &sk);
	Cvar_SetValue ("skill", sk);

	fscanf (f, "%s\n",mapname);
	fscanf (f, "%f\n",&time);

	SV_SpawnServer (mapname, startspot);

	if (!sv.active)
	{
		Con_Printf ("Couldn't load map\n");
		return -1;
	}

// load the light styles
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		fscanf (f, "%s\n", str);
#ifdef QOG_HUNK_PACKED
		sv.lightstyles[i] = Hunk_AllocPacked (strlen(str)+1, 1);	// QuakeOG: packed, zone.c
#else
		sv.lightstyles[i] = Hunk_Alloc (strlen(str)+1);
#endif
		strcpy (sv.lightstyles[i], str);
	}

// load the edicts out of the savegame file
	while (!feof(f))
	{
		fscanf (f, "%i\n",&entnum);
		for (i=0 ; i<sizeof(str)-1 ; i++)
		{
			r = fgetc (f);
			if (r == EOF || !r)
				break;
			str[i] = r;
			if (r == '}')
			{
				i++;
				break;
			}
		}
		if (i == sizeof(str)-1)
			Sys_Error ("Loadgame buffer overflow");
		str[i] = 0;
		start = str;
		start = COM_Parse(str);
		if (!com_token[0])
			break;		// end of file
		if (strcmp(com_token,"{"))
			Sys_Error ("First token isn't a brace");
			
		// parse an edict

		ent = EDICT_NUM(entnum);
		ED_CLEARFIELDS(ent);	// QuakeOG: was memset (&ent->v, 0, progs->entityfields * 4)
		ent->free = false;
		ED_ParseEdict (start, ent);
	
		// link it into the bsp tree
		if (!ent->free)
			SV_LinkEdict (ent, false);
	}
	
//	sv.num_edicts = entnum;
	sv.time = time;
	fclose (f);

//	for (i=0 ; i<NUM_SPAWN_PARMS ; i++)
//		svs.clients->spawn_parms[i] = spawn_parms[i];

	return 0;
}

// changing levels within a unit
void Host_Changelevel2_f (void)
{
	char	level[MAX_QPATH];
	char	_startspot[MAX_QPATH];
	char	*startspot;

	if (Cmd_Argc() < 2)
	{
		Con_Printf ("changelevel2 <levelname> : continue game on a new level in the unit\n");
		return;
	}
	if (!sv.active || cls.demoplayback)
	{
		Con_Printf ("Only the server may changelevel\n");
		return;
	}

	strcpy (level, Cmd_Argv(1));
	if (Cmd_Argc() == 2)
		startspot = NULL;
	else
	{
		strcpy (_startspot, Cmd_Argv(2));
		startspot = _startspot;
	}

	SV_SaveSpawnparms ();

	// save the current level's state
	SaveGamestate ();

	// try to restore the new level
	if (LoadGamestate (level, startspot))
		SV_SpawnServer (level, startspot);
}
#endif


//============================================================================

/*
======================
Host_Name_f
======================
*/
void Host_Name_f (void)
{
	char	*newName;

#ifndef QOG_SPLIT_SERVER
	if (Cmd_Argc () == 1)
	{
		Con_Printf ("\"name\" is \"%s\"\n", cl_name.string);
		return;
	}
#endif
	if (Cmd_Argc () == 2)
		newName = Cmd_Argv(1);	
	else
		newName = Cmd_Args();
	newName[15] = 0;

#ifndef QOG_SPLIT_SERVER
	if (cmd_source == src_command)
	{
		if (Q_strcmp(cl_name.string, newName) == 0)
			return;
		Cvar_Set ("_cl_name", newName);
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
		return;
	}
#endif

#ifndef QOG_SPLIT_CLIENT
	if (host_client->name[0] && strcmp(host_client->name, "unconnected") )
		if (Q_strcmp(host_client->name, newName) != 0)
			Con_Printf ("%s renamed to %s\n", host_client->name, newName);
	Q_strcpy (host_client->name, newName);
	EV_SETINT(host_client->edict, netname, host_client->name - pr_strings);
	
// send notification to all clients
	
	MSG_WriteByte (&sv.reliable_datagram, svc_updatename);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteString (&sv.reliable_datagram, host_client->name);
#endif
}

	
void Host_Version_f (void)
{
	Con_Printf ("Version %4.2f\n", VERSION);
	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");
}

#ifdef IDGODS
void Host_Please_f (void)
{
	client_t *cl;
	int			j;
	
	if (cmd_source != src_command)
		return;

	if ((Cmd_Argc () == 3) && Q_strcmp(Cmd_Argv(1), "#") == 0)
	{
		j = Q_atof(Cmd_Argv(2)) - 1;
		if (j < 0 || j >= svs.maxclients)
			return;
		if (!svs.clients[j].active)
			return;
		cl = &svs.clients[j];
		if (cl->privileged)
		{
			cl->privileged = false;
			cl->edict->v.flags = (int)cl->edict->v.flags & ~(FL_GODMODE|FL_NOTARGET);
			cl->edict->v.movetype = MOVETYPE_WALK;
			noclip_anglehack = false;
		}
		else
			cl->privileged = true;
	}

	if (Cmd_Argc () != 2)
		return;

	for (j=0, cl = svs.clients ; j<svs.maxclients ; j++, cl++)
	{
		if (!cl->active)
			continue;
		if (Q_strcasecmp(cl->name, Cmd_Argv(1)) == 0)
		{
			if (cl->privileged)
			{
				cl->privileged = false;
				cl->edict->v.flags = (int)cl->edict->v.flags & ~(FL_GODMODE|FL_NOTARGET);
				cl->edict->v.movetype = MOVETYPE_WALK;
				noclip_anglehack = false;
			}
			else
				cl->privileged = true;
			break;
		}
	}
}
#endif


#ifndef QOG_SPLIT_CLIENT	// QuakeOG: server half only, say and tell
void Host_Say(qboolean teamonly)
{
	client_t *client;
	client_t *save;
	int		j;
	char	*p;
	unsigned char	text[64];
	qboolean	fromServer = false;

	if (cmd_source == src_command)
	{
#ifndef QOG_SPLIT_SERVER
		if (cls.state == ca_dedicated)
#endif
		{
			fromServer = true;
			teamonly = false;
		}
#ifndef QOG_SPLIT_SERVER
		else
		{
			Cmd_ForwardToServer ();
			return;
		}
#endif
	}

	if (Cmd_Argc () < 2)
		return;

	save = host_client;

	p = Cmd_Args();
// remove quotes if present
	if (*p == '"')
	{
		p++;
		p[Q_strlen(p)-1] = 0;
	}

// turn on color set 1
	if (!fromServer)
		sprintf (text, "%c%s: ", 1, save->name);
	else
		sprintf (text, "%c<%s> ", 1, hostname.string);

	j = sizeof(text) - 2 - Q_strlen(text);  // -2 for /n and null terminator
	if (Q_strlen(p) > j)
		p[j] = 0;

	strcat (text, p);
	strcat (text, "\n");

	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client || !client->active || !client->spawned)
			continue;
		if (teamplay.value && teamonly && EV_FLOAT(client->edict, team) != EV_FLOAT(save->edict, team))
			continue;
		host_client = client;
		SV_ClientPrintf("%s", text);
	}
	host_client = save;

	Sys_Printf("%s", &text[1]);
}


void Host_Say_f(void)
{
	Host_Say(false);
}


void Host_Say_Team_f(void)
{
	Host_Say(true);
}


void Host_Tell_f(void)
{
	client_t *client;
	client_t *save;
	int		j;
	char	*p;
	char	text[64];

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (Cmd_Argc () < 3)
		return;

	Q_strcpy(text, host_client->name);
	Q_strcat(text, ": ");

	p = Cmd_Args();

// remove quotes if present
	if (*p == '"')
	{
		p++;
		p[Q_strlen(p)-1] = 0;
	}

// check length & truncate if necessary
	j = sizeof(text) - 2 - Q_strlen(text);  // -2 for /n and null terminator
	if (Q_strlen(p) > j)
		p[j] = 0;

	strcat (text, p);
	strcat (text, "\n");

	save = host_client;
	for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++)
	{
		if (!client->active || !client->spawned)
			continue;
		if (Q_strcasecmp(client->name, Cmd_Argv(1)))
			continue;
		host_client = client;
		SV_ClientPrintf("%s", text);
		break;
	}
	host_client = save;
}
#endif	// QOG_SPLIT_CLIENT


/*
==================
Host_Color_f
==================
*/
void Host_Color_f(void)
{
	int		top, bottom;
	int		playercolor;
	
#ifndef QOG_SPLIT_SERVER
	if (Cmd_Argc() == 1)
	{
		Con_Printf ("\"color\" is \"%i %i\"\n", ((int)cl_color.value) >> 4, ((int)cl_color.value) & 0x0f);
		Con_Printf ("color <0-13> [0-13]\n");
		return;
	}
#endif

	if (Cmd_Argc() == 2)
		top = bottom = atoi(Cmd_Argv(1));
	else
	{
		top = atoi(Cmd_Argv(1));
		bottom = atoi(Cmd_Argv(2));
	}
	
	top &= 15;
	if (top > 13)
		top = 13;
	bottom &= 15;
	if (bottom > 13)
		bottom = 13;
	
	playercolor = top*16 + bottom;

#ifndef QOG_SPLIT_SERVER
	if (cmd_source == src_command)
	{
		Cvar_SetValue ("_cl_color", playercolor);
		if (cls.state == ca_connected)
			Cmd_ForwardToServer ();
		return;
	}
#endif

#ifndef QOG_SPLIT_CLIENT
	host_client->colors = playercolor;
	EV_SETFLOAT(host_client->edict, team, bottom + 1);

// send notification to all clients
	MSG_WriteByte (&sv.reliable_datagram, svc_updatecolors);
	MSG_WriteByte (&sv.reliable_datagram, host_client - svs.clients);
	MSG_WriteByte (&sv.reliable_datagram, host_client->colors);
#endif
}

/*
==================
Host_Kill_f
==================
*/
#ifndef QOG_SPLIT_CLIENT	// QuakeOG: server half only, kill to the view commands
void Host_Kill_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (EV_FLOAT(sv_player, health) <= 0)
	{
		SV_ClientPrintf ("Can't suicide -- allready dead!\n");
		return;
	}
	
	pr_global_struct->time = sv.time;
	pr_global_struct->self = EDICT_TO_PROG(sv_player);
	PR_ExecuteProgram (pr_global_struct->ClientKill);
}


/*
==================
Host_Pause_f
==================
*/
void Host_Pause_f (void)
{
	
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}
	if (!pausable.value)
		SV_ClientPrintf ("Pause not allowed.\n");
	else
	{
		sv.paused ^= 1;

		if (sv.paused)
		{
			SV_BroadcastPrintf ("%s paused the game\n", pr_strings + EV_INT(sv_player, netname));
		}
		else
		{
			SV_BroadcastPrintf ("%s unpaused the game\n",pr_strings + EV_INT(sv_player, netname));
		}

	// send notification to all clients
		MSG_WriteByte (&sv.reliable_datagram, svc_setpause);
		MSG_WriteByte (&sv.reliable_datagram, sv.paused);
	}
}

//===========================================================================


/*
==================
Host_PreSpawn_f
==================
*/
void Host_PreSpawn_f (void)
{
	if (cmd_source == src_command)
	{
		Con_Printf ("prespawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("prespawn not valid -- allready spawned\n");
		return;
	}
	
	SZ_Write (&host_client->message, sv.signon.data, sv.signon.cursize);
	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 2);
	host_client->sendsignon = true;
}

/*
==================
Host_Spawn_f
==================
*/
void Host_Spawn_f (void)
{
	int		i;
	client_t	*client;
	edict_t	*ent;

	if (cmd_source == src_command)
	{
		Con_Printf ("spawn is not valid from the console\n");
		return;
	}

	if (host_client->spawned)
	{
		Con_Printf ("Spawn not valid -- allready spawned\n");
		return;
	}

// run the entrance script
	if (sv.loadgame)
	{	// loaded games are fully inited allready
		// if this is the last client to be connected, unpause
		sv.paused = false;
	}
	else
	{
		// set up the edict
		ent = host_client->edict;

		ED_CLEARFIELDS(ent);	// QuakeOG: was memset (&ent->v, 0, progs->entityfields * 4)
		EV_SETFLOAT(ent, colormap, NUM_FOR_EDICT(ent));
		EV_SETFLOAT(ent, team, (host_client->colors & 15) + 1);
		EV_SETINT(ent, netname, host_client->name - pr_strings);

		// copy spawn parms out of the client_t

		for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
			(&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];

		// call the spawn function

		pr_global_struct->time = sv.time;
		pr_global_struct->self = EDICT_TO_PROG(sv_player);
		PR_ExecuteProgram (pr_global_struct->ClientConnect);

		if ((Sys_FloatTime() - host_client->netconnection->connecttime) <= sv.time)
			Sys_Printf ("%s entered the game\n", host_client->name);

		PR_ExecuteProgram (pr_global_struct->PutClientInServer);	
	}


// send all current names, colors, and frag counts
	SZ_Clear (&host_client->message);

// send time of update
	MSG_WriteByte (&host_client->message, svc_time);
	MSG_WriteFloat (&host_client->message, sv.time);

	for (i=0, client = svs.clients ; i<svs.maxclients ; i++, client++)
	{
		MSG_WriteByte (&host_client->message, svc_updatename);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteString (&host_client->message, client->name);
		MSG_WriteByte (&host_client->message, svc_updatefrags);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteShort (&host_client->message, client->old_frags);
		MSG_WriteByte (&host_client->message, svc_updatecolors);
		MSG_WriteByte (&host_client->message, i);
		MSG_WriteByte (&host_client->message, client->colors);
	}
	
// send all current light styles
	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
	{
		MSG_WriteByte (&host_client->message, svc_lightstyle);
		MSG_WriteByte (&host_client->message, (char)i);
		MSG_WriteString (&host_client->message, sv.lightstyles[i]);
	}

//
// send some stats
//
	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALSECRETS);
	MSG_WriteLong (&host_client->message, pr_global_struct->total_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_TOTALMONSTERS);
	MSG_WriteLong (&host_client->message, pr_global_struct->total_monsters);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_SECRETS);
	MSG_WriteLong (&host_client->message, pr_global_struct->found_secrets);

	MSG_WriteByte (&host_client->message, svc_updatestat);
	MSG_WriteByte (&host_client->message, STAT_MONSTERS);
	MSG_WriteLong (&host_client->message, pr_global_struct->killed_monsters);

	
//
// send a fixangle
// Never send a roll angle, because savegames can catch the server
// in a state where it is expecting the client to correct the angle
// and it won't happen if the game was just loaded, so you wind up
// with a permanent head tilt
	ent = EDICT_NUM( 1 + (host_client - svs.clients) );
	MSG_WriteByte (&host_client->message, svc_setangle);
	for (i=0 ; i < 2 ; i++)
		MSG_WriteAngle (&host_client->message, ent->v.angles[i] );
	MSG_WriteAngle (&host_client->message, 0 );

	SV_WriteClientdataToMessage (sv_player, &host_client->message);

	MSG_WriteByte (&host_client->message, svc_signonnum);
	MSG_WriteByte (&host_client->message, 3);
	host_client->sendsignon = true;
}

/*
==================
Host_Begin_f
==================
*/
void Host_Begin_f (void)
{
	if (cmd_source == src_command)
	{
		Con_Printf ("begin is not valid from the console\n");
		return;
	}

	host_client->spawned = true;
}

//===========================================================================


/*
==================
Host_Kick_f

Kicks a user off of the server
==================
*/
void Host_Kick_f (void)
{
	char		*who;
	char		*message = NULL;
	client_t	*save;
	int			i;
	qboolean	byNumber = false;

	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
	}
	else if (pr_global_struct->deathmatch && !host_client->privileged)
		return;

	save = host_client;

	if (Cmd_Argc() > 2 && Q_strcmp(Cmd_Argv(1), "#") == 0)
	{
		i = Q_atof(Cmd_Argv(2)) - 1;
		if (i < 0 || i >= svs.maxclients)
			return;
		if (!svs.clients[i].active)
			return;
		host_client = &svs.clients[i];
		byNumber = true;
	}
	else
	{
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (!host_client->active)
				continue;
			if (Q_strcasecmp(host_client->name, Cmd_Argv(1)) == 0)
				break;
		}
	}

	if (i < svs.maxclients)
	{
		if (cmd_source == src_command)
#ifdef QOG_SPLIT_SERVER
			who = cl_name.string;	// QuakeOG: the client's name, copied from the client
#else
			if (cls.state == ca_dedicated)
				who = "Console";
			else
				who = cl_name.string;
#endif
		else
			who = save->name;

		// can't kick yourself!
		if (host_client == save)
			return;

		if (Cmd_Argc() > 2)
		{
			message = COM_Parse(Cmd_Args());
			if (byNumber)
			{
				message++;							// skip the #
				while (*message == ' ')				// skip white space
					message++;
				message += Q_strlen(Cmd_Argv(2));	// skip the number
			}
			while (*message && *message == ' ')
				message++;
		}
		if (message)
			SV_ClientPrintf ("Kicked by %s: %s\n", who, message);
		else
			SV_ClientPrintf ("Kicked by %s\n", who);
		SV_DropClient (false);
	}

	host_client = save;
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

/*
==================
Host_Give_f
==================
*/
void Host_Give_f (void)
{
	char	*t;
	int		v, w;
	eval_t	*val;

	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}

	if (pr_global_struct->deathmatch && !host_client->privileged)
		return;

	t = Cmd_Argv(1);
	v = atoi (Cmd_Argv(2));
	
	switch (t[0])
	{
   case '0':
   case '1':
   case '2':
   case '3':
   case '4':
   case '5':
   case '6':
   case '7':
   case '8':
   case '9':
      // MED 01/04/97 added hipnotic give stuff
      if (hipnotic)
      {
         if (t[0] == '6')
         {
            if (t[1] == 'a')
               EV_SETFLOAT(sv_player, items, (int)EV_FLOAT(sv_player, items) | HIT_PROXIMITY_GUN);
            else
               EV_SETFLOAT(sv_player, items, (int)EV_FLOAT(sv_player, items) | IT_GRENADE_LAUNCHER);
         }
         else if (t[0] == '9')
            EV_SETFLOAT(sv_player, items, (int)EV_FLOAT(sv_player, items) | HIT_LASER_CANNON);
         else if (t[0] == '0')
            EV_SETFLOAT(sv_player, items, (int)EV_FLOAT(sv_player, items) | HIT_MJOLNIR);
         else if (t[0] >= '2')
            EV_SETFLOAT(sv_player, items, (int)EV_FLOAT(sv_player, items) | (IT_SHOTGUN << (t[0] - '2')));
      }
      else
      {
         if (t[0] >= '2')
            EV_SETFLOAT(sv_player, items, (int)EV_FLOAT(sv_player, items) | (IT_SHOTGUN << (t[0] - '2')));
      }
		break;
	
    case 's':
		if (rogue)
		{
	        val = GetEdictFieldValue(sv_player, "ammo_shells1");
		    if (val)
			    val->_float = v;
		}

        EV_SETFLOAT(sv_player, ammo_shells, v);
        break;		
    case 'n':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, "ammo_nails1");
			if (val)
			{
				val->_float = v;
				if (EV_FLOAT(sv_player, weapon) <= IT_LIGHTNING)
					EV_SETFLOAT(sv_player, ammo_nails, v);
			}
		}
		else
		{
			EV_SETFLOAT(sv_player, ammo_nails, v);
		}
        break;		
    case 'l':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, "ammo_lava_nails");
			if (val)
			{
				val->_float = v;
				if (EV_FLOAT(sv_player, weapon) > IT_LIGHTNING)
					EV_SETFLOAT(sv_player, ammo_nails, v);
			}
		}
        break;
    case 'r':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, "ammo_rockets1");
			if (val)
			{
				val->_float = v;
				if (EV_FLOAT(sv_player, weapon) <= IT_LIGHTNING)
					EV_SETFLOAT(sv_player, ammo_rockets, v);
			}
		}
		else
		{
			EV_SETFLOAT(sv_player, ammo_rockets, v);
		}
        break;		
    case 'm':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, "ammo_multi_rockets");
			if (val)
			{
				val->_float = v;
				if (EV_FLOAT(sv_player, weapon) > IT_LIGHTNING)
					EV_SETFLOAT(sv_player, ammo_rockets, v);
			}
		}
        break;		
    case 'h':
        EV_SETFLOAT(sv_player, health, v);
        break;		
    case 'c':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, "ammo_cells1");
			if (val)
			{
				val->_float = v;
				if (EV_FLOAT(sv_player, weapon) <= IT_LIGHTNING)
					EV_SETFLOAT(sv_player, ammo_cells, v);
			}
		}
		else
		{
			EV_SETFLOAT(sv_player, ammo_cells, v);
		}
        break;		
    case 'p':
		if (rogue)
		{
			val = GetEdictFieldValue(sv_player, "ammo_plasma");
			if (val)
			{
				val->_float = v;
				if (EV_FLOAT(sv_player, weapon) > IT_LIGHTNING)
					EV_SETFLOAT(sv_player, ammo_cells, v);
			}
		}
        break;		
    }
}

edict_t	*FindViewthing (void)
{
	int		i;
	edict_t	*e;
	
	for (i=0 ; i<sv.num_edicts ; i++)
	{
		e = EDICT_NUM(i);
		if ( !strcmp (pr_strings + e->v.classname, "viewthing") )
			return e;
	}
	Con_Printf ("No viewthing on map\n");
	return NULL;
}

/*
==================
Host_Viewmodel_f
==================
*/
#ifndef QOG_SPLIT_SERVER	// QuakeOG: these set and read the client's model list
void Host_Viewmodel_f (void)
{
	edict_t	*e;
	model_t	*m;

	e = FindViewthing ();
	if (!e)
		return;

	m = Mod_ForName (Cmd_Argv(1), false);
	if (!m)
	{
		Con_Printf ("Can't load %s\n", Cmd_Argv(1));
		return;
	}
	
	e->v.frame = 0;
	cl.model_precache[(int)e->v.modelindex] = m;
}

/*
==================
Host_Viewframe_f
==================
*/
void Host_Viewframe_f (void)
{
	edict_t	*e;
	int		f;
	model_t	*m;

	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];

	f = atoi(Cmd_Argv(1));
	if (f >= m->numframes)
		f = m->numframes-1;

	e->v.frame = f;		
}


void PrintFrameName (model_t *m, int frame)
{
	aliashdr_t 			*hdr;
	maliasframedesc_t	*pframedesc;

	hdr = (aliashdr_t *)Mod_Extradata (m);
	if (!hdr)
		return;
	pframedesc = &hdr->frames[frame];
	
	Con_Printf ("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
void Host_Viewnext_f (void)
{
	edict_t	*e;
	model_t	*m;
	
	e = FindViewthing ();
	if (!e)
		return;
	m = cl.model_precache[(int)e->v.modelindex];

	e->v.frame = e->v.frame + 1;
	if (e->v.frame >= m->numframes)
		e->v.frame = m->numframes - 1;

	PrintFrameName (m, e->v.frame);		
}

/*
==================
Host_Viewprev_f
==================
*/
void Host_Viewprev_f (void)
{
	edict_t	*e;
	model_t	*m;

	e = FindViewthing ();
	if (!e)
		return;

	m = cl.model_precache[(int)e->v.modelindex];

	e->v.frame = e->v.frame - 1;
	if (e->v.frame < 0)
		e->v.frame = 0;

	PrintFrameName (m, e->v.frame);		
}
#endif	// QOG_SPLIT_SERVER
#endif	// QOG_SPLIT_CLIENT

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/


/*
==================
Host_Startdemos_f
==================
*/
#ifndef QOG_SPLIT_SERVER	// QuakeOG: client half only, the demo commands
void Host_Startdemos_f (void)
{
	int		i, c;

	if (cls.state == ca_dedicated)
	{
		if (!sv.active)
			Cbuf_AddText ("map start\n");
		return;
	}

	c = Cmd_Argc() - 1;
	if (c > MAX_DEMOS)
	{
		Con_Printf ("Max %i demos in demoloop\n", MAX_DEMOS);
		c = MAX_DEMOS;
	}
	Con_Printf ("%i demo(s) in loop\n", c);

	for (i=1 ; i<c+1 ; i++)
		strncpy (cls.demos[i-1], Cmd_Argv(i), sizeof(cls.demos[0])-1);

#ifdef QOG_DEMOS_OPTIONAL
	// QuakeOG: the loop keeps only the demos that are present. Without any
	// (the board's data image leaves them out) it does not start, so the
	// game opens on the console instead of a missing file error.
	{
		char	name[MAX_OSPATH];
		int		n = 0;

		for (i=0 ; i<c ; i++)
		{
			Q_strncpy (name, cls.demos[i], sizeof(name) - 5);
			COM_DefaultExtension (name, ".dem");
			if (COM_HasFile (name))
				memmove (cls.demos[n++], cls.demos[i], sizeof(cls.demos[0]));
		}
		if (n < c)
			cls.demos[n][0] = 0;
		if (!n)
		{
			cls.demonum = -1;
			return;
		}
	}
#endif

	if (!sv.active && cls.demonum != -1 && !cls.demoplayback)
	{
		cls.demonum = 0;
		CL_NextDemo ();
	}
	else
		cls.demonum = -1;
}


/*
==================
Host_Demos_f

Return to looping demos
==================
*/
void Host_Demos_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (cls.demonum == -1)
		cls.demonum = 1;
	CL_Disconnect_f ();
	CL_NextDemo ();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
void Host_Stopdemo_f (void)
{
	if (cls.state == ca_dedicated)
		return;
	if (!cls.demoplayback)
		return;
	CL_StopPlayback ();
	CL_Disconnect ();
}
#endif	// QOG_SPLIT_SERVER

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void Host_InitCommands (void)
{
#ifdef QOG_SPLIT_CLIENT
	// QuakeOG: the client half's commands. The ones id's code forwards to
	// the server as a client message are that forward here. The server's
	// own commands are not here, so they run on the server half.
	Cmd_AddCommand ("quit", Host_Quit_f);
	Cmd_AddCommand ("god", Cmd_ForwardToServer);
	Cmd_AddCommand ("notarget", Cmd_ForwardToServer);
	Cmd_AddCommand ("fly", Cmd_ForwardToServer);
	Cmd_AddCommand ("map", Host_Map_f);
	Cmd_AddCommand ("connect", Host_Connect_f);
	Cmd_AddCommand ("reconnect", Host_Reconnect_f);
	Cmd_AddCommand ("name", Host_Name_f);
	Cmd_AddCommand ("noclip", Cmd_ForwardToServer);
	Cmd_AddCommand ("version", Host_Version_f);
	Cmd_AddCommand ("say", Cmd_ForwardToServer);
	Cmd_AddCommand ("say_team", Cmd_ForwardToServer);
	Cmd_AddCommand ("tell", Cmd_ForwardToServer);
	Cmd_AddCommand ("color", Host_Color_f);
	Cmd_AddCommand ("kill", Cmd_ForwardToServer);
	Cmd_AddCommand ("pause", Cmd_ForwardToServer);
	Cmd_AddCommand ("ping", Cmd_ForwardToServer);
	Cmd_AddCommand ("load", Host_Loadgame_f);
	Cmd_AddCommand ("save", Host_Savegame_f);
	Cmd_AddCommand ("give", Cmd_ForwardToServer);
	Cmd_AddCommand ("startdemos", Host_Startdemos_f);
	Cmd_AddCommand ("demos", Host_Demos_f);
	Cmd_AddCommand ("stopdemo", Host_Stopdemo_f);
	Cmd_AddCommand ("mcache", Mod_Print);
#ifdef QOG_BOOT_ENTRY
	Cmd_AddCommand ("qog_boot", Host_QogBoot_f);	// QuakeOG (Agent AG)
#endif
#ifdef QOG_MISSING_MAPS
	Cmd_AddCommand ("qog_loadfailed", Host_QogLoadFailed_f);	// QuakeOG (Agent AG), sent by the server half
#endif
#elif defined(QOG_SPLIT_SERVER)
	// QuakeOG: the server half's commands, the client's are on the client
	Cmd_AddCommand ("status", Host_Status_f);
	Cmd_AddCommand ("god", Host_God_f);
	Cmd_AddCommand ("notarget", Host_Notarget_f);
	Cmd_AddCommand ("fly", Host_Fly_f);
	Cmd_AddCommand ("map", Host_Map_f);
	Cmd_AddCommand ("restart", Host_Restart_f);
	Cmd_AddCommand ("changelevel", Host_Changelevel_f);
	Cmd_AddCommand ("name", Host_Name_f);
	Cmd_AddCommand ("noclip", Host_Noclip_f);
	Cmd_AddCommand ("version", Host_Version_f);
	Cmd_AddCommand ("say", Host_Say_f);
	Cmd_AddCommand ("say_team", Host_Say_Team_f);
	Cmd_AddCommand ("tell", Host_Tell_f);
	Cmd_AddCommand ("color", Host_Color_f);
	Cmd_AddCommand ("kill", Host_Kill_f);
	Cmd_AddCommand ("pause", Host_Pause_f);
	Cmd_AddCommand ("spawn", Host_Spawn_f);
	Cmd_AddCommand ("begin", Host_Begin_f);
	Cmd_AddCommand ("prespawn", Host_PreSpawn_f);
	Cmd_AddCommand ("kick", Host_Kick_f);
	Cmd_AddCommand ("ping", Host_Ping_f);
	Cmd_AddCommand ("load", Host_Loadgame_f);
	Cmd_AddCommand ("save", Host_Savegame_f);
	Cmd_AddCommand ("give", Host_Give_f);
	Cmd_AddCommand ("mcache", Mod_Print);
#else
	Cmd_AddCommand ("status", Host_Status_f);
	Cmd_AddCommand ("quit", Host_Quit_f);
	Cmd_AddCommand ("god", Host_God_f);
	Cmd_AddCommand ("notarget", Host_Notarget_f);
	Cmd_AddCommand ("fly", Host_Fly_f);
	Cmd_AddCommand ("map", Host_Map_f);
	Cmd_AddCommand ("restart", Host_Restart_f);
	Cmd_AddCommand ("changelevel", Host_Changelevel_f);
#ifdef QUAKE2
	Cmd_AddCommand ("changelevel2", Host_Changelevel2_f);
#endif
	Cmd_AddCommand ("connect", Host_Connect_f);
	Cmd_AddCommand ("reconnect", Host_Reconnect_f);
	Cmd_AddCommand ("name", Host_Name_f);
	Cmd_AddCommand ("noclip", Host_Noclip_f);
	Cmd_AddCommand ("version", Host_Version_f);
#ifdef IDGODS
	Cmd_AddCommand ("please", Host_Please_f);
#endif
	Cmd_AddCommand ("say", Host_Say_f);
	Cmd_AddCommand ("say_team", Host_Say_Team_f);
	Cmd_AddCommand ("tell", Host_Tell_f);
	Cmd_AddCommand ("color", Host_Color_f);
	Cmd_AddCommand ("kill", Host_Kill_f);
	Cmd_AddCommand ("pause", Host_Pause_f);
	Cmd_AddCommand ("spawn", Host_Spawn_f);
	Cmd_AddCommand ("begin", Host_Begin_f);
	Cmd_AddCommand ("prespawn", Host_PreSpawn_f);
	Cmd_AddCommand ("kick", Host_Kick_f);
	Cmd_AddCommand ("ping", Host_Ping_f);
	Cmd_AddCommand ("load", Host_Loadgame_f);
	Cmd_AddCommand ("save", Host_Savegame_f);
	Cmd_AddCommand ("give", Host_Give_f);

	Cmd_AddCommand ("startdemos", Host_Startdemos_f);
	Cmd_AddCommand ("demos", Host_Demos_f);
	Cmd_AddCommand ("stopdemo", Host_Stopdemo_f);

	Cmd_AddCommand ("viewmodel", Host_Viewmodel_f);
	Cmd_AddCommand ("viewframe", Host_Viewframe_f);
	Cmd_AddCommand ("viewnext", Host_Viewnext_f);
	Cmd_AddCommand ("viewprev", Host_Viewprev_f);

	Cmd_AddCommand ("mcache", Mod_Print);
#ifdef QOG_BOOT_ENTRY
	Cmd_AddCommand ("qog_boot", Host_QogBoot_f);	// QuakeOG (Agent AG)
#endif
#endif
}
