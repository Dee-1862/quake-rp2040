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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "r_local.h"
#ifdef QOG_NET_LINK
#include "net_link.h"	// QuakeOG: the split game (net_link.c)
#endif
#include "qog_phase.h"	// QuakeOG (Agent V): frame phase times (QOG_PHASE_TIME)

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t host_parms;

qboolean	host_initialized;		// true if into command execution

double		host_frametime;
double		host_time;
double		realtime;				// without any filtering or bounding
double		oldrealtime;			// last frame run
int			host_framecount;

int			host_hunklevel;

int			minimum_memory;

client_t	*host_client;			// current client

jmp_buf 	host_abortserver;

byte		*host_basepal;
byte		*host_colormap;

cvar_t	host_framerate = {"host_framerate","0"};	// set for slow motion
cvar_t	host_speeds = {"host_speeds","0"};			// set for running times

cvar_t	sys_ticrate = {"sys_ticrate","0.05"};
cvar_t	serverprofile = {"serverprofile","0"};

cvar_t	fraglimit = {"fraglimit","0",false,true};
cvar_t	timelimit = {"timelimit","0",false,true};
cvar_t	teamplay = {"teamplay","0",false,true};

cvar_t	samelevel = {"samelevel","0"};
cvar_t	noexit = {"noexit","0",false,true};

#ifdef QUAKE2
cvar_t	developer = {"developer","1"};	// should be 0 for release!
#else
cvar_t	developer = {"developer","0"};
#endif

cvar_t	skill = {"skill","1"};						// 0 - 3
cvar_t	deathmatch = {"deathmatch","0"};			// 0, 1, or 2
cvar_t	coop = {"coop","0"};			// 0 or 1

cvar_t	pausable = {"pausable","1"};

cvar_t	temp1 = {"temp1","0"};


/*
================
Host_EndGame
================
*/
void Host_EndGame (char *message, ...)
{
	va_list		argptr;
	char		string[1024];
	
	va_start (argptr,message);
	vsprintf (string,message,argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n",string);

	if (sv.active)
		Host_ShutdownServer (false);

#ifdef QOG_SPLIT_SERVER
	Link_ServerEndGame (string);	// QuakeOG: the client does the rest
#else
	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n",string);	// dedicated servers exit

	if (cls.demonum != -1)
		CL_NextDemo ();
	else
		CL_Disconnect ();
#endif

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	qboolean inerror = false;
	
	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

#ifndef QOG_SPLIT_SERVER
	SCR_EndLoadingPlaque ();		// reenable screen updates
#endif

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	Con_Printf ("Host_Error: %s\n",string);

	if (sv.active)
		Host_ShutdownServer (false);

#ifdef QOG_SPLIT_SERVER
	Link_ServerHostError (string);	// QuakeOG: the client stops its frame and disconnects
#else
	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n",string);	// dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;
#endif

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void	Host_FindMaxClients (void)
{
	int		i;

#ifdef QOG_SPLIT_CLIENT
	// QuakeOG: the server half sets maxclients and deathmatch
	if (COM_CheckParm ("-dedicated"))
		Sys_Error ("the split client cannot be a dedicated server");
	cls.state = ca_disconnected;
#else
	svs.maxclients = 1;
		
	i = COM_CheckParm ("-dedicated");
#ifdef QOG_SPLIT_SERVER
	i = 0;	// QuakeOG: the split server has one client, the client half
#endif
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi (com_argv[i+1]);
		}
		else
			svs.maxclients = 8;
	}
#ifndef QOG_SPLIT_SERVER
	else
		cls.state = ca_disconnected;
#endif

	i = COM_CheckParm ("-listen");
#ifdef QOG_SPLIT_SERVER
	i = 0;
#endif
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi (com_argv[i+1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;
#ifndef QOG_SINGLE_PLAYER	// QuakeOG: one client slot and two loopback sockets, not four and five
	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;
#endif
	svs.clients = Hunk_AllocName (svs.maxclientslimit*sizeof(client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_SetValue ("deathmatch", 1.0);
	else
		Cvar_SetValue ("deathmatch", 0.0);
#endif	// QOG_SPLIT_CLIENT
}


/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Host_InitCommands ();
	
	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_speeds);

	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&serverprofile);

	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&deathmatch);
	Cvar_RegisterVariable (&coop);

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();
	
	host_time = 1.0;		// so a think at time 0 won't get called
}


/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	FILE	*f;

// dedicated servers initialize the host but don't parse and set the
// config.cfg cvars
#ifdef QOG_SPLIT_SERVER
	return;		// QuakeOG: the client half writes config.cfg
#endif
	if (host_initialized & !isDedicated)
	{
		f = fopen (va("%s/config.cfg",com_gamedir), "w");
		if (!f)
		{
			Con_Printf ("Couldn't write config.cfg.\n");
			return;
		}
		
		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		fclose (f);
	}
}


#ifndef QOG_SPLIT_CLIENT	// QuakeOG: server only
/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	
	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);
	
	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;
	
	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);
	
	for (i=0 ; i<svs.maxclients ; i++)
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	
	va_start (argptr,fmt);
	vsprintf (string, fmt,argptr);
	va_end (argptr);
	
	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		saveSelf;
	int		i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}
	
		if (host_client->edict && host_client->spawned)
		{
		// call the prog function for removing a client
		// this will set the body to a dead frame, among other things
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG(host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf ("Client %s removed\n",host_client->name);
	}

// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

// send notification to all clients
	for (i=0, client = svs.clients ; i<svs.maxclients ; i++, client++)
	{
		if (!client->active)
			continue;
		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer(qboolean crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	char		message[4];
	double	start;

	if (!sv.active)
		return;

	sv.active = false;

#ifndef QOG_SPLIT_SERVER	// QuakeOG: the split client disconnected first (net_link.c)
// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();
#endif

// flush any pending messages - like the score!!!
	start = Sys_FloatTime();
	do
	{
		count = 0;
		for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage(host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage(host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_FloatTime() - start) > 3.0)
			break;
	}
	while (count);

// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte(&buf, svc_disconnect);
	count = NET_SendToAll(&buf, 5);
	if (count)
		Con_Printf("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i=0, host_client = svs.clients ; i<svs.maxclients ; i++, host_client++)
		if (host_client->active)
			SV_DropClient(crash);

//
// clear structures
//
	memset (&sv, 0, sizeof(sv));
	memset (svs.clients, 0, svs.maxclientslimit*sizeof(client_t));
}
#else	// QOG_SPLIT_CLIENT
// QuakeOG: the server half shuts itself down when the client closes its
// connection. What the client sees of it is sv.active going false at once.
void Host_ShutdownServer(qboolean crash)
{
	sv.active = false;
}
#endif	// QOG_SPLIT_CLIENT


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	Con_DPrintf ("Clearing memory\n");
#ifndef QOG_SPLIT_SERVER
	D_FlushCaches ();
#endif
	Mod_ClearAll ();
	if (host_hunklevel)
		Hunk_FreeToLowMark (host_hunklevel);

#ifdef QOG_SPLIT_SERVER
	memset (&sv, 0, sizeof(sv));
	Link_ServerClearMemory ();	// QuakeOG: the client clears its half at the same point
#else
	cls.signon = 0;
#ifndef QOG_SPLIT_CLIENT	// QuakeOG: the client's sv is the server's report, not cleared here
	memset (&sv, 0, sizeof(sv));
#endif
	memset (&cl, 0, sizeof(cl));
#endif
}


//============================================================================


/*
===================
Host_FilterTime

Returns false if the time is too short to run a frame
===================
*/
#ifndef QOG_SPLIT_SERVER	// QuakeOG: the split server takes the client's times (net_link.c)
qboolean Host_FilterTime (float time)
{
	realtime += time;

	if (!cls.timedemo && realtime - oldrealtime < 1.0/72.0)
		return false;		// framerate is too high

	host_frametime = realtime - oldrealtime;
	oldrealtime = realtime;

	if (host_framerate.value > 0)
		host_frametime = host_framerate.value;
	else
	{	// don't allow really long or short frames
		if (host_frametime > 0.1)
			host_frametime = 0.1;
		if (host_frametime < 0.001)
			host_frametime = 0.001;
	}
	
	return true;
}
#endif


/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	char	*cmd;

	while (1)
	{
		cmd = Sys_ConsoleInput ();
		if (!cmd)
			break;
		Cbuf_AddText (cmd);
	}
}


#ifndef QOG_SPLIT_CLIENT	// QuakeOG: server only
#ifdef QOG_SPLIT_SERVER
#define	key_dest	(link_keygame ? key_game : key_console)	// QuakeOG: the client's key_dest
#endif
/*
==================
Host_ServerFrame

==================
*/
#ifdef FPS_20

void _Host_ServerFrame (void)
{
// run the world state	
	pr_global_struct->frametime = host_frametime;

// read client messages
	SV_RunClients ();
	
// move things around and think
// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics ();
}

void Host_ServerFrame (void)
{
	float	save_host_frametime;
	float	temp_host_frametime;

// run the world state	
	pr_global_struct->frametime = host_frametime;

// set the time and clear the general datagram
	SV_ClearDatagram ();
	
// check for new clients
	SV_CheckForNewClients ();

	temp_host_frametime = save_host_frametime = host_frametime;
	while(temp_host_frametime > (1.0/72.0))
	{
		if (temp_host_frametime > 0.05)
			host_frametime = 0.05;
		else
			host_frametime = temp_host_frametime;
		temp_host_frametime -= host_frametime;
		_Host_ServerFrame ();
	}
	host_frametime = save_host_frametime;

// send all messages to the clients
	SV_SendClientMessages ();
}

#else

void Host_ServerFrame (void)
{
	QOG_PH_T (hp_phys);	// QuakeOG (Agent V)

// run the world state
	pr_global_struct->frametime = host_frametime;

// set the time and clear the general datagram
	SV_ClearDatagram ();

// check for new clients
	SV_CheckForNewClients ();

// read client messages
	SV_RunClients ();

// move things around and think
// always pause in single player if in console or menus
	if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game) )
		SV_Physics ();
	QOG_HP_ADD (QOG_HP_SVPHYS, hp_phys);	// QuakeOG

// send all messages to the clients
	{
	QOG_PH_T (hp_send);	// QuakeOG
	SV_SendClientMessages ();
	QOG_HP_ADD (QOG_HP_SVSEND, hp_send);
	}
}

#endif
#ifdef QOG_SPLIT_SERVER
#undef	key_dest
#endif
#endif	// QOG_SPLIT_CLIENT


/*
==================
Host_Frame

Runs all active servers
==================
*/
#ifdef QOG_SPLIT_SERVER
// QuakeOG: the split server's frame. The client half drives it: its frame
// time, its realtime and its console lines arrive in Link_ServerWaitTick,
// and the client counts the frame's rand () call. What is left of id's
// frame is the server's part.
void _Host_Frame (float time)
{
	if (setjmp (host_abortserver) )
	{
		Link_ServerAbort ();	// the client still waits for its answer
		return;
	}

	{
	QOG_PH_T (hp_wait);	// QuakeOG (Agent V)
	Link_ServerWaitTick ();
	QOG_HP_ADD (QOG_HP_SVWAIT, hp_wait);
	}

	Cbuf_Execute ();	// empty: the command buffer is the client's
	NET_Poll();
	Host_GetConsoleCommands ();
	if (sv.active)
	{
		QOG_PH_T (hp_sv);	// QuakeOG
		Host_ServerFrame ();
		QOG_HP_ADD (QOG_HP_SVFRAME, hp_sv);
	}
	host_time += host_frametime;

	Link_ServerFrameDone ();
	host_framecount++;
#ifdef QOG_PHASE_TIME
	qog_hp_frames++;	// QuakeOG
#endif
}
#else
#ifdef QOG_LINK_PIPELINE
/*
==================
QuakeOG (Agent V): the split client draws while the server half runs

In id's frame the server's frame sits between the client's input and its
screen update, so in the split game the display chip waits for the main
chip and then the main chip waits for the display chip. With
QOG_LINK_PIPELINE (board client only) a frame of play leaves its screen
update (SCR_UpdateScreen, S_Update, CL_DecayLights) pending, and the next
frame runs it right after it has sent its input to the server half
(Link_ClientTick), so the server's frame runs on the main chip while the
display chip draws. The order of everything the two halves exchange is
id's: the input and the frame time of frame k+1 never depend on frame k's
screen update, except for V_DriftPitch's change to the view angles, which
runs at the end of frame k as in id's order, and except for console lines,
which may read the frame (qog_shot) or change what it shows, so any line
other than "wait" runs the pending update first. While the update runs,
realtime, host_frametime, host_framecount, cl.viewangles and key_dest
have the values they had at the end of frame k, so it draws the same
picture. Only frames of play are deferred (signed on, not a demo, the game
keys, no intermission or pause, no loading plaque).
==================
*/
extern qboolean	scr_drawloading, scr_initialized;	// screen.c
void V_DriftPitch (void);
qboolean Cbuf_QogIdle (void);	// cmd.c
extern qboolean	v_qog_drifted;	// view.c: V_CalcRefdef skips V_DriftPitch
unsigned	host_qog_pipelined;	// frames whose screen update ran while the server half ran

static qboolean	pl_pending;
static double	pl_realtime, pl_frametime;
static vec3_t	pl_viewangles;
static int		pl_framecount;
static keydest_t	pl_keydest;

static qboolean Host_PipelineCan (void)
{
	return cls.state == ca_connected && cls.signon == SIGNONS && cl.worldmodel && sv.active
		&& !cls.demoplayback && !cls.timedemo && key_dest == key_game
		&& !cl.intermission && !cl.paused && !host_speeds.value
		&& !scr_skipupdate && !block_drawing && !scr_disabled_for_loading && !scr_drawloading
		&& scr_initialized && con_initialized;
}

// the end of frame k when its screen update waits: what id's update would
// have changed that frame k+1's input reads (V_CalcRefdef's pitch drift)
static void Host_PipelineDefer (void)
{
	V_DriftPitch ();
	pl_realtime = realtime;
	pl_frametime = host_frametime;
	VectorCopy (cl.viewangles, pl_viewangles);
	pl_framecount = host_framecount;
	pl_keydest = key_dest;
	pl_pending = true;
}

// frame k's screen update, sound and light decay, as at the end of frame k
static void Host_PipelineFlush (void)
{
	double		rt, ft;
	vec3_t		va;
	int			fc;
	keydest_t	kd;

	if (!pl_pending)
		return;
	pl_pending = false;
	rt = realtime;
	ft = host_frametime;
	VectorCopy (cl.viewangles, va);
	fc = host_framecount;
	kd = key_dest;
	realtime = pl_realtime;
	host_frametime = pl_frametime;
	VectorCopy (pl_viewangles, cl.viewangles);
	host_framecount = pl_framecount;
	key_dest = pl_keydest;
	v_qog_drifted = true;
	{
	QOG_PH_T (hp_scr);
	SCR_UpdateScreen ();
	QOG_HP_ADD (QOG_HP_SCREEN, hp_scr);
	}
	{
	QOG_PH_T (hp_snd);
	S_Update (r_origin, vpn, vright, vup);	// signed on: Host_PipelineCan
	CL_DecayLights ();
	CDAudio_Update();
	QOG_HP_ADD (QOG_HP_SOUND, hp_snd);
	}
	v_qog_drifted = false;
	realtime = rt;
	host_frametime = ft;
	VectorCopy (va, cl.viewangles);
	host_framecount = fc;
	key_dest = kd;
	host_qog_pipelined++;
}
#endif	// QOG_LINK_PIPELINE

void _Host_Frame (float time)
{
	static double		time1 = 0;
	static double		time2 = 0;
	static double		time3 = 0;
	int			pass1, pass2, pass3;

	if (setjmp (host_abortserver) )
	{
#ifdef QOG_LINK_PIPELINE
		pl_pending = false;	// QuakeOG: the frame it was for is gone
#endif
		return;			// something bad happened, or the server disconnected
	}

// keep the random time dependent
	rand ();

// decide the simulation time
	if (!Host_FilterTime (time))
		return;			// don't run too fast, or packets will flood out

	{
	QOG_PH_T (hp_frame);	// QuakeOG (Agent V): frame phase times
	QOG_PH_T (hp_in);

// get new key events
	Sys_SendKeyEvents ();

// allow mice or other external controllers to add commands
	IN_Commands ();

#ifdef QOG_LINK_PIPELINE
	if (!Cbuf_QogIdle ())
		Host_PipelineFlush ();	// QuakeOG: a console line runs after the last frame's update, as in id's order
#endif

// process console commands
	Cbuf_Execute ();

	NET_Poll();

// if running the server locally, make intentions now
	if (sv.active)
		CL_SendCmd ();

//-------------------
//
// server operations
//
//-------------------

// check for commands typed to the host
	Host_GetConsoleCommands ();
	QOG_HP_ADD (QOG_HP_INPUT, hp_in);	// QuakeOG

#ifdef QOG_SPLIT_CLIENT
#ifdef QOG_LINK_PIPELINE
	Link_ClientTick ();		// QuakeOG: the server half starts its frame
	Host_PipelineFlush ();	// and the last frame's screen update runs meanwhile
	{
	QOG_PH_T (hp_link);
	Link_ClientDone ();
	QOG_HP_ADD (QOG_HP_LINK, hp_link);
	}
#else
	{
	QOG_PH_T (hp_link);	// QuakeOG
	Link_ClientFrame ();	// QuakeOG: the server half runs its frame here (net_link.c)
	QOG_HP_ADD (QOG_HP_LINK, hp_link);
	}
#endif
#else
	if (sv.active)
		Host_ServerFrame ();
#endif

//-------------------
//
// client operations
//
//-------------------

// if running the server remotely, send intentions now after
// the incoming messages have been read
	if (!sv.active)
		CL_SendCmd ();

	host_time += host_frametime;

// fetch results from server
	if (cls.state == ca_connected)
	{
		QOG_PH_T (hp_read);	// QuakeOG
		CL_ReadFromServer ();
		QOG_HP_ADD (QOG_HP_READ, hp_read);
	}

#ifdef QOG_LINK_PIPELINE
	if (Host_PipelineCan ())
		Host_PipelineDefer ();	// QuakeOG: drawn while the server half runs the next frame
	else
#endif
	{
// update video
	if (host_speeds.value)
		time1 = Sys_FloatTime ();

	{
	QOG_PH_T (hp_scr);	// QuakeOG
	SCR_UpdateScreen ();
	QOG_HP_ADD (QOG_HP_SCREEN, hp_scr);
	}

	if (host_speeds.value)
		time2 = Sys_FloatTime ();

	{
	QOG_PH_T (hp_snd);	// QuakeOG
// update audio
	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else
		S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update();
	QOG_HP_ADD (QOG_HP_SOUND, hp_snd);
	}

	if (host_speeds.value)
	{
		pass1 = (time1 - time3)*1000;
		time3 = Sys_FloatTime ();
		pass2 = (time2 - time1)*1000;
		pass3 = (time3 - time2)*1000;
		Con_Printf ("%3i tot %3i server %3i gfx %3i snd\n",
					pass1+pass2+pass3, pass1, pass2, pass3);
	}
	}

	host_framecount++;
	QOG_HP_ADD (QOG_HP_FRAME, hp_frame);	// QuakeOG
#ifdef QOG_PHASE_TIME
	qog_hp_frames++;
#endif
	}
}
#endif	// QOG_SPLIT_SERVER

void Host_Frame (float time)
{
	double	time1, time2;
	static double	timetotal;
	static int		timecount;
	int		i, c, m;

	if (!serverprofile.value)
	{
		_Host_Frame (time);
		return;
	}
	
	time1 = Sys_FloatTime ();
	_Host_Frame (time);
	time2 = Sys_FloatTime ();	
	
	timetotal += time2 - time1;
	timecount++;
	
	if (timecount < 1000)
		return;

	m = timetotal*1000/timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
#ifndef QOG_SPLIT_CLIENT	// QuakeOG: the split client has no server's clients
	for (i=0 ; i<svs.maxclients ; i++)
	{
		if (svs.clients[i].active)
			c++;
	}
#endif

	Con_Printf ("serverprofile: %2i clients %2i msec\n",  c,  m);
}

//============================================================================


extern int vcrFile;
#define	VCR_SIGNATURE	0x56435231
// "VCR1"

void Host_InitVCR (quakeparms_t *parms)
{
	int		i, len, n;
	char	*p;
	
	if (COM_CheckParm("-playback"))
	{
		if (com_argc != 2)
			Sys_Error("No other parameters allowed with -playback\n");

		Sys_FileOpenRead("quake.vcr", &vcrFile);
		if (vcrFile == -1)
			Sys_Error("playback file not found\n");

		Sys_FileRead (vcrFile, &i, sizeof(int));
		if (i != VCR_SIGNATURE)
			Sys_Error("Invalid signature in vcr file\n");

		Sys_FileRead (vcrFile, &com_argc, sizeof(int));
		com_argv = malloc(com_argc * sizeof(char *));
		com_argv[0] = parms->argv[0];
		for (i = 0; i < com_argc; i++)
		{
			Sys_FileRead (vcrFile, &len, sizeof(int));
			p = malloc(len);
			Sys_FileRead (vcrFile, p, len);
			com_argv[i+1] = p;
		}
		com_argc++; /* add one for arg[0] */
		parms->argc = com_argc;
		parms->argv = com_argv;
	}

	if ( (n = COM_CheckParm("-record")) != 0)
	{
		vcrFile = Sys_FileOpenWrite("quake.vcr");

		i = VCR_SIGNATURE;
		Sys_FileWrite(vcrFile, &i, sizeof(int));
		i = com_argc - 1;
		Sys_FileWrite(vcrFile, &i, sizeof(int));
		for (i = 1; i < com_argc; i++)
		{
			if (i == n)
			{
				len = 10;
				Sys_FileWrite(vcrFile, &len, sizeof(int));
				Sys_FileWrite(vcrFile, "-playback", len);
				continue;
			}
			len = Q_strlen(com_argv[i]) + 1;
			Sys_FileWrite(vcrFile, &len, sizeof(int));
			Sys_FileWrite(vcrFile, com_argv[i], len);
		}
	}
	
}

/*
====================
Host_Init
====================
*/
void Host_Init (quakeparms_t *parms)
{

	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else
		minimum_memory = MINIMUM_MEMORY_LEVELPAK;
#ifdef QOG_MIN_MEMORY
	// QuakeOG: the board's hunk is sized from the measured need, far below
	// id's 5.3 MB floor. A hunk that is too small still stops in Hunk_Alloc.
	minimum_memory = QOG_MIN_MEMORY;
#endif

	if (COM_CheckParm ("-minmemory"))
		parms->memsize = minimum_memory;

	host_parms = *parms;

	if (parms->memsize < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory available, can't execute game", parms->memsize / (float)0x100000);

	com_argc = parms->argc;
	com_argv = parms->argv;

	Memory_Init (parms->membase, parms->memsize);
	Cbuf_Init ();
	Cmd_Init ();
#ifdef QOG_SPLIT_SERVER
	// QuakeOG: the split server, no client, console or menus (net_link.c)
	Host_InitVCR (parms);
	COM_Init (parms->basedir);
	Host_InitLocal ();
	Sys_QogRegister ();
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();
#ifdef QOG_SND_HOST
	S_Init ();		// QuakeOG (Agent W): the mixer for the client half (snd_link.c)
#endif
	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");
	Con_Printf ("%4.1f megabyte heap\n",parms->memsize/ (1024*1024.0));
#else
	V_Init ();
	Chase_Init ();
	Host_InitVCR (parms);
	COM_Init (parms->basedir);
	Host_InitLocal ();
	W_LoadWadFile ("gfx.wad");
	Key_Init ();
	Con_Init ();
	M_Init ();
#ifndef QOG_SPLIT_CLIENT	// QuakeOG: the split client has no server
	PR_Init ();
#endif
	Mod_Init ();
	NET_Init ();
#ifndef QOG_SPLIT_CLIENT
	SV_Init ();
#endif

	Con_Printf ("Exe: "__TIME__" "__DATE__"\n");
	Con_Printf ("%4.1f megabyte heap\n",parms->memsize/ (1024*1024.0));

	R_InitTextures ();		// needed even for dedicated servers

	if (cls.state != ca_dedicated)
	{
#ifdef QOG_PALETTE_FLASH
		// QuakeOG: the palette (768 bytes) is only ever read too, as the
		// colormap below, so use it in place when the pack is mapped
		host_basepal = COM_FlashFile ("gfx/palette.lmp", NULL);
		if (!host_basepal)
#endif
		host_basepal = (byte *)COM_LoadHunkFile ("gfx/palette.lmp");
		if (!host_basepal)
			Sys_Error ("Couldn't load gfx/palette.lmp");
		// QuakeOG: the colormap (16 KB) is only ever read, so use it in place
		// when the pack is mapped. The image is aligned, so vid.fullbright can
		// still read the int after the table.
		host_colormap = COM_FlashFile ("gfx/colormap.lmp", NULL);
		if (!host_colormap)
			host_colormap = (byte *)COM_LoadHunkFile ("gfx/colormap.lmp");
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

#ifndef ID_WIN32 // on non win32, mouse comes before video for security reasons
		IN_Init ();
#endif
		VID_Init (host_basepal);

		Draw_Init ();
		SCR_Init ();
		R_Init ();
#ifndef	ID_WIN32
	// on Win32, sound initialization has to come before video initialization, so we
	// can put up a popup if the sound hardware is in use
		S_Init ();
#else

#ifdef	GLQUAKE
	// FIXME: doesn't use the new one-window approach yet
		S_Init ();
#endif

#endif	// ID_WIN32
		CDAudio_Init ();
		Sbar_Init ();
		CL_Init ();
#ifdef ID_WIN32 // on non win32, mouse comes before video for security reasons
		IN_Init ();
#endif
	}

	Cbuf_InsertText ("exec quake.rc\n");
#endif	// QOG_SPLIT_SERVER

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	host_initialized = true;
	
	Sys_Printf ("========Quake Initialized=========\n");	
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown(void)
{
	static qboolean isdown = false;
	
	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

#ifdef QOG_SPLIT_SERVER
	NET_Shutdown ();	// QuakeOG: nothing else to shut down
#else
// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();
#ifdef QOG_SPLIT_CLIENT
	Link_Quit ();		// QuakeOG: the server half quits with the client
#endif

	CDAudio_Shutdown ();
	NET_Shutdown ();
	S_Shutdown();
	IN_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		VID_Shutdown();
	}
#endif
}

