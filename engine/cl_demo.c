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

#include "quakedef.h"
#ifdef QOG_LOOP_INPLACE
#include "net_loop.h"	// QuakeOG, Loop_DemoBuffer
#endif

void CL_FinishTimeDemo (void);

#ifdef QOG_DEMO_FLASH
// QuakeOG, a demo in a mapped pack (COM_FlashFile) is read in place, and
// each message is parsed where it lies, so playback needs no FILE and no
// message buffer. cls.demofile stays NULL while such a demo plays. A demo
// that is not in a mapped pack plays from a FILE as in id's code.
static byte		*cl_demoflash;
static int		cl_demoflashlen, cl_demoflashpos;
static byte		*cl_demosaveddata;	// net_message's buffer, given back when playback stops
static int		cl_demosavedmax;

/*
====================
CL_GetFlashMessage

id's three freads from a demo in place. A field that would run past the end
of the file is left as it was, as fread leaves it, and a message that does
not fit (or is empty, which fread also reports as a failure) stops playback.
====================
*/
static int CL_GetFlashMessage (void)
{
	int		i, len;
	float	f;

	if (cl_demoflashpos + 4 <= cl_demoflashlen)
	{
		memcpy (&len, cl_demoflash + cl_demoflashpos, 4);
		cl_demoflashpos += 4;
		net_message.cursize = len;
	}
	VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);
	for (i=0 ; i<3 ; i++)
	{
		if (cl_demoflashpos + 4 > cl_demoflashlen)
			continue;
		memcpy (&f, cl_demoflash + cl_demoflashpos, 4);
		cl_demoflashpos += 4;
		cl.mviewangles[0][i] = LittleFloat (f);
	}

	net_message.cursize = LittleLong (net_message.cursize);
	if (net_message.cursize > MAX_MSGLEN)
		Sys_Error ("Demo message > MAX_MSGLEN");
	if (net_message.cursize <= 0 || net_message.cursize > cl_demoflashlen - cl_demoflashpos)
	{
		CL_StopPlayback ();
		return 0;
	}
	net_message.data = cl_demoflash + cl_demoflashpos;	// read only, the parser never writes it
	net_message.maxsize = net_message.cursize;
	cl_demoflashpos += net_message.cursize;
	return 1;
}
#endif

/*
==============================================================================

DEMO CODE

When a demo is playing back, all NET_SendMessages are skipped, and
NET_GetMessages are read from the demo file.

Whenever cl.time gets past the last received message, another message is
read from the demo file.
==============================================================================
*/

/*
==============
CL_StopPlayback

Called when a demo file runs out, or the user starts a game
==============
*/
void CL_StopPlayback (void)
{
#ifdef QOG_BOARD_MENU
	qboolean	whole = true;	// QuakeOG, the demo ran to its end (the board's benchmark)
#endif

	if (!cls.demoplayback)
		return;

#ifdef QOG_DEMO_FLASH
	if (cl_demoflash)
	{
#ifdef QOG_BOARD_MENU
		// id's demos end with a disconnect, read when nothing follows it
		whole = cl_demoflashpos >= cl_demoflashlen;
#endif
		cl_demoflash = NULL;
		net_message.data = cl_demosaveddata;
		net_message.maxsize = cl_demosavedmax;
		net_message.cursize = 0;
	}
	else
#endif
	fclose (cls.demofile);
	cls.demoplayback = false;
	cls.demofile = NULL;
	cls.state = ca_disconnected;

	if (cls.timedemo)
	{
		CL_FinishTimeDemo ();
#ifdef QOG_BOARD_MENU
		M_BenchDone (whole);	// QuakeOG, the board shows the result (menu.c)
#endif
	}
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length and view angles
====================
*/
void CL_WriteDemoMessage (void)
{
	int		len;
	int		i;
	float	f;

	len = LittleLong (net_message.cursize);
	fwrite (&len, 4, 1, cls.demofile);
	for (i=0 ; i<3 ; i++)
	{
		f = LittleFloat (cl.viewangles[i]);
		fwrite (&f, 4, 1, cls.demofile);
	}
	fwrite (net_message.data, net_message.cursize, 1, cls.demofile);
	fflush (cls.demofile);
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage (void)
{
	int		r, i;
	float	f;
	
	if	(cls.demoplayback)
	{
	// decide if it is time to grab the next message		
		if (cls.signon == SIGNONS)	// allways grab until fully connected
		{
			if (cls.timedemo)
			{
				if (host_framecount == cls.td_lastframe)
					return 0;		// allready read this frame's message
				cls.td_lastframe = host_framecount;
			// if this is the second frame, grab the real td_starttime
			// so the bogus time on the first frame doesn't count
				if (host_framecount == cls.td_startframe + 1)
					cls.td_starttime = realtime;
			}
			else if ( /* cl.time > 0 && */ cl.time <= cl.mtime[0])
			{
					return 0;		// don't need another message yet
			}
		}
		
	// get the next message
#ifdef QOG_DEMO_FLASH
		if (cl_demoflash)
			return CL_GetFlashMessage ();
#endif
		fread (&net_message.cursize, 4, 1, cls.demofile);
		VectorCopy (cl.mviewangles[0], cl.mviewangles[1]);
		for (i=0 ; i<3 ; i++)
		{
			r = fread (&f, 4, 1, cls.demofile);
			cl.mviewangles[0][i] = LittleFloat (f);
		}
		
		net_message.cursize = LittleLong (net_message.cursize);
		if (net_message.cursize > MAX_MSGLEN)
			Sys_Error ("Demo message > MAX_MSGLEN");
#ifdef QOG_LOOP_INPLACE
		net_message.data = Loop_DemoBuffer ();	// QuakeOG, net_message has no buffer of its own
		net_message.maxsize = MAX_MSGLEN;
#endif
		r = fread (net_message.data, net_message.cursize, 1, cls.demofile);
		if (r != 1)
		{
			CL_StopPlayback ();
			return 0;
		}
	
		return 1;
	}

	while (1)
	{
		r = NET_GetMessage (cls.netcon);
		
		if (r != 1 && r != 2)
			return r;
	
	// discard nop keepalive message
		if (net_message.cursize == 1 && net_message.data[0] == svc_nop)
			Con_Printf ("<-- server to client keepalive\n");
		else
			break;
	}

	if (cls.demorecording)
		CL_WriteDemoMessage ();
	
	return r;
}


/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f (void)
{
	if (cmd_source != src_command)
		return;

	if (!cls.demorecording)
	{
		Con_Printf ("Not recording a demo.\n");
		return;
	}

// write a disconnect message to the demo file
#ifdef QOG_LOOP_INPLACE
	{	// QuakeOG, net_message points into a receive buffer, write elsewhere
		byte		one[4];
		sizebuf_t	saved = net_message;

		net_message.data = one;
		net_message.maxsize = sizeof(one);
		SZ_Clear (&net_message);
		MSG_WriteByte (&net_message, svc_disconnect);
		CL_WriteDemoMessage ();
		net_message = saved;
	}
#else
	SZ_Clear (&net_message);
	MSG_WriteByte (&net_message, svc_disconnect);
	CL_WriteDemoMessage ();
#endif

// finish up
	fclose (cls.demofile);
	cls.demofile = NULL;
	cls.demorecording = false;
	Con_Printf ("Completed demo\n");
}

/*
====================
CL_Record_f

record <demoname> <map> [cd track]
====================
*/
void CL_Record_f (void)
{
	int		c;
	char	name[MAX_OSPATH];
	int		track;

	if (cmd_source != src_command)
		return;

	c = Cmd_Argc();
	if (c != 2 && c != 3 && c != 4)
	{
		Con_Printf ("record <demoname> [<map> [cd track]]\n");
		return;
	}

	if (strstr(Cmd_Argv(1), ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return;
	}

	if (c == 2 && cls.state == ca_connected)
	{
		Con_Printf("Can not record - already connected to server\nClient demo recording must be started before connecting\n");
		return;
	}

// write the forced cd track number, or -1
	if (c == 4)
	{
		track = atoi(Cmd_Argv(3));
		Con_Printf ("Forcing CD track to %i\n", cls.forcetrack);
	}
	else
		track = -1;	

	sprintf (name, "%s/%s", com_gamedir, Cmd_Argv(1));
	
//
// start the map up
//
	if (c > 2)
		Cmd_ExecuteString ( va("map %s", Cmd_Argv(2)), src_command);
	
//
// open the demo file
//
	COM_DefaultExtension (name, ".dem");

	Con_Printf ("recording to %s.\n", name);
	cls.demofile = fopen (name, "wb");
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		return;
	}

	cls.forcetrack = track;
	fprintf (cls.demofile, "%i\n", cls.forcetrack);
	
	cls.demorecording = true;
}


/*
====================
CL_PlayDemo_f

play [demoname]
====================
*/
void CL_PlayDemo_f (void)
{
	char	name[256];
	int c;
	qboolean neg = false;

	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("play <demoname> : plays a demo\n");
		return;
	}

//
// disconnect from server
//
	CL_Disconnect ();
	
//
// open the demo file
//
	strcpy (name, Cmd_Argv(1));
	COM_DefaultExtension (name, ".dem");

	Con_Printf ("Playing demo from %s.\n", name);
#ifdef QOG_DEMO_FLASH
	cl_demoflash = COM_FlashFile (name, &cl_demoflashlen);
	if (cl_demoflash)
	{
		cls.demofile = NULL;
		cls.demoplayback = true;
		cls.state = ca_connected;
		cls.forcetrack = 0;
		cl_demosaveddata = net_message.data;
		cl_demosavedmax = net_message.maxsize;
		// the track number line, read as id's getc loop reads it
		for (cl_demoflashpos = 0 ; cl_demoflashpos < cl_demoflashlen ; )
		{
			c = cl_demoflash[cl_demoflashpos++];
			if (c == '\n')
				break;
			if (c == '-')
				neg = true;
			else
				cls.forcetrack = cls.forcetrack * 10 + (c - '0');
		}
		if (neg)
			cls.forcetrack = -cls.forcetrack;
		return;
	}
#endif
	COM_FOpenFile (name, &cls.demofile);
	if (!cls.demofile)
	{
		Con_Printf ("ERROR: couldn't open.\n");
		cls.demonum = -1;		// stop demo loop
		return;
	}

	cls.demoplayback = true;
	cls.state = ca_connected;
	cls.forcetrack = 0;

	while ((c = getc(cls.demofile)) != '\n')
		if (c == '-')
			neg = true;
		else
			cls.forcetrack = cls.forcetrack * 10 + (c - '0');

	if (neg)
		cls.forcetrack = -cls.forcetrack;
// ZOID, fscanf is evil
//	fscanf (cls.demofile, "%i\n", &cls.forcetrack);
}

/*
====================
CL_FinishTimeDemo

====================
*/
void CL_FinishTimeDemo (void)
{
	int		frames;
	float	time;
	
	cls.timedemo = false;
	
// the first frame didn't count
	frames = (host_framecount - cls.td_startframe) - 1;
	time = realtime - cls.td_starttime;
	if (!time)
		time = 1;
	Con_Printf ("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames/time);
#ifdef QOG_BOARD_MENU
	// QuakeOG, the result for the board's benchmark page (M_BenchDone). Nothing
	// reads these fields once the timedemo is over, the next one sets them.
	cls.td_startframe = frames;
	cls.td_starttime = time;
#endif
}

/*
====================
CL_TimeDemo_f

timedemo [demoname]
====================
*/
void CL_TimeDemo_f (void)
{
	if (cmd_source != src_command)
		return;

	if (Cmd_Argc() != 2)
	{
		Con_Printf ("timedemo <demoname> : gets demo speeds\n");
		return;
	}

	CL_PlayDemo_f ();
#ifdef QOG_BOARD_MENU
	// QuakeOG, the demo's name for the board's result. id's map command writes
	// cls.mapstring and nothing reads it.
	Q_strncpy (cls.mapstring, Cmd_Argv(1), sizeof(cls.mapstring) - 1);
	cls.mapstring[sizeof(cls.mapstring) - 1] = 0;
#endif

// cls.td_starttime will be grabbed at the second frame of the demo, so
// all the loading time doesn't get counted
	
	cls.timedemo = true;
	cls.td_startframe = host_framecount;
	cls.td_lastframe = -1;		// get a new message this frame
}

