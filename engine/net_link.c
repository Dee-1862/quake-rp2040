/*
Copyright (C) 1996-1997 Id Software, Inc.
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
// net_link.c -- QuakeOG: the split game's net driver (QOG_NET_LINK)
//
// The server (SV_*, QuakeC, edicts) runs on one chip or process
// (QOG_SPLIT_SERVER) and the client (CL_*, renderer, input) on another
// (QOG_SPLIT_CLIENT). This driver replaces the loopback driver and joins
// them over a reliable byte stream (port/common/qog_link.c, the inter-chip
// UART on the board, a TCP socket on the PC).
//
// The two halves run in lockstep so that the game behaves exactly as the
// single-process game does. In id's Host_Frame the local server runs in the
// middle of the client's frame. Here the client stops at that point
// (Link_ClientFrame), sends what its frame produced and waits, the server
// runs its frame and answers, and the client goes on. Console lines that
// only the server knows run there at once, inside the client's command
// buffer (Link_ForwardCommand), and what id's server did to client state
// directly (start the client's reconnect, clear its memory, print on its
// console, add to the shared command buffer) comes back as items the client
// carries out at the same point. Shared globals cross with every item that
// follows a change: the client's realtime, host_frametime and key_dest, the
// server's host_frametime and active state, and the position of the shared
// random sequence (qog_rand), so both halves draw the same numbers.
//
// Quake's messages travel as items with loopback semantics: a reliable
// message makes the sender wait until the receiver has read it (canSend),
// the receive buffers are the loopback's sizes and read in place
// (QOG_LOOP_INPLACE), and an overflow behaves as there.
//
// Stream items: [kind][len lo][len hi][len bytes]

#include "quakedef.h"

#ifdef QOG_NET_LINK

#include "net_link.h"
#include "qog_link.h"
#include "qog_rand.h"

#define LINK_TEXTMAX	4096	// console.c MAXPRINTMSG
#define LINK_CLTEXT	256		// the client's text buffer: console lines it runs, pieces of the rest
#define LINK_SVTEXT	1024	// the server's: a console line (Cbuf_Execute's line buffer)

#ifndef QOG_SINGLE_PLAYER
#error QOG_NET_LINK needs QOG_SINGLE_PLAYER
#endif
#ifndef QOG_LOOP_INPLACE
#error QOG_NET_LINK needs QOG_LOOP_INPLACE
#endif
#if defined(QOG_SPLIT_CLIENT) == defined(QOG_SPLIT_SERVER)
#error QOG_NET_LINK needs exactly one of QOG_SPLIT_CLIENT and QOG_SPLIT_SERVER
#endif

enum {
	LK_CSTATE = 1,	// client: realtime, host_frametime, random calls, key_dest is key_game
	LK_SSTATE,		// server: host_frametime, random calls, sv and svs fields the client reads
	LK_REL,			// a reliable Quake message
	LK_UNREL,		// an unreliable Quake message
	LK_ACKREL,		// the last reliable message was read: the sender may send again
	LK_CONNECT,		// client: connect local
	LK_ACCEPT,		// server: the connection was taken (Loop_CheckNewConnections)
	LK_CLOSE,		// the sender closed its socket
	LK_CMD,			// client: a console line for the server, answered by LK_SYNCDONE
	LK_CVAR,		// "name" "value": a cvar set on the other half
	LK_SAVEINFO,	// client: intermission flag and comment for the next save
	LK_TICK,		// client: its part of the frame is done, run the server frame
	LK_DONE,		// server: frame done
	LK_SYNCDONE,	// server: LK_CMD done
	LK_PRINT,		// server: console text (byte 0: safe)
	LK_CBUF,		// server: text for the command buffer (byte 0: insert)
	LK_CLEXEC,		// server: a console line the client runs now
	LK_CLEARMEM,	// server: Host_ClearMemory ran
	LK_CENTEROFF,	// server: scr_centertime_off = 0
	LK_HOSTERROR,	// server: Host_Error, the client stops its frame too
	LK_ENDGAME,		// server: Host_EndGame
	LK_QUIT			// client: the game quits
};

qboolean	link_applying;

static qsocket_t	*link_sock;		// this half's end of the one connection
static byte		*link_rx;			// its receive buffer, as the loopback's
static int		link_rxsize;
static int		link_pending;		// delivered bytes still at the front (QOG_LOOP_INPLACE)

// the server's state the client reads: host_frametime 8, random calls 4,
// max_edicts, maxclients, maxclientslimit 12, active, paused, noclip_anglehack 3, map name 64
#define SSTATE_SIZE	(12 + 12 + 3 + 64)
static qboolean	link_started;

#ifdef QOG_SPLIT_CLIENT
#define LINK_RXSIZE		QOG_LOOP_CLIENT_RX
#define LINK_ROLE		QOG_LINK_ROLE_CLIENT
#else
#define LINK_RXSIZE		QOG_LOOP_SERVER_RX
#define LINK_ROLE		QOG_LINK_ROLE_SERVER
#endif
#ifndef QOG_LOOP_CLIENT_RX
#define QOG_LOOP_CLIENT_RX	NET_MAXMESSAGE
#endif
#ifndef QOG_LOOP_SERVER_RX
#define QOG_LOOP_SERVER_RX	NET_MAXMESSAGE
#endif

//=============================================================================
// stream helpers

// QuakeOG (Agent T): the save list of the load and save menus, a side frame
// pair outside the stream (QOG_LINK_T_SAVES_ASK, QOG_LINK_T_SAVES in
// port/common/qog_link.h): slot i is a found byte and the comment M_ScanSaves
// reads from s<i>.sav, 39 bytes padded with zeros
#define LINK_SAVESLOTS	12		// menu.c MAX_SAVEGAMES
#define LINK_SAVEENTRY	(1 + SAVEGAME_COMMENT_LENGTH)
#define LINK_SAVESIZE	(3 + LINK_SAVESLOTS * LINK_SAVEENTRY)
#if LINK_SAVESIZE > QOG_LINK_CHUNK + 2
#error the save list does not fit one link frame
#endif

static void put16 (byte *b, int v) { b[0] = v & 0xff; b[1] = (v >> 8) & 0xff; }
static void put32 (byte *b, unsigned v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static unsigned get32 (const byte *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24); }

static void Link_SyncState (void);

static void Link_RawItem (int kind, const void *data, int len)
{
	byte	h[3];

	h[0] = kind;
	put16 (h + 1, len);
	qog_link_write (h, 3);
	if (len)
		qog_link_write (data, len);
}

static void Link_Item (int kind, const void *data, int len)
{
	Link_SyncState ();
	Link_RawItem (kind, data, len);
}

static void Link_ItemText (int kind, int flag, const char *text)
{
	byte	h[4];
	int		n = Q_strlen ((char *)text);

	if (n > LINK_TEXTMAX)
		n = LINK_TEXTMAX;
	Link_SyncState ();
	h[0] = kind;		// written in pieces: no copy of the text
	put16 (h + 1, n + 2);
	h[3] = flag;
	qog_link_write (h, 4);
	qog_link_write (text, n);
	h[0] = 0;
	qog_link_write (h, 1);
}

static void Link_Skip (int len)
{
	byte	junk[64];

	while (len > 0)
	{
		int n = len > (int)sizeof(junk) ? (int)sizeof(junk) : len;
		qog_link_read (junk, n);
		len -= n;
	}
}

// reads an item's header, returns its kind and the payload length
static int Link_ReadHeader (int *len)
{
	byte	h[3];

	qog_link_read (h, 3);
	*len = h[1] | (h[2] << 8);
	return h[0];
}

// a short payload into a buffer, the rest skipped, always NUL terminated
static void Link_ReadText (byte *buf, int size, int len)
{
	int n = len < size - 1 ? len : size - 1;

	qog_link_read (buf, n);
	buf[n] = 0;
	Link_Skip (len - n);
}

//=============================================================================
// the random sequence (port/common/qog_rand.c)

static void Link_RandTo (unsigned calls)
{
	while ((int)(calls - qog_rand_calls) > 0)
		rand ();
	if (calls != qog_rand_calls)
		Con_DPrintf ("link: random sequence ahead of the other half by %u\n", qog_rand_calls - calls);
}

//=============================================================================
// the loopback semantics

static int IntAlign (int value)
{
	return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}

static void Link_Consume (void)
{
	int	length = link_pending;

	if (!length || !link_sock)
		return;
	link_pending = 0;
	link_sock->receiveMessageLength -= length;
	if (link_sock->receiveMessageLength)
		Q_memcpy (link_sock->receiveMessage, &link_sock->receiveMessage[length], link_sock->receiveMessageLength);
}

// a Quake message item arrived: into the receive buffer as Loop_Send* put it
static void Link_Deliver (int type, int len)
{
	byte	*b;

	if (!link_sock || !link_sock->receiveMessage)
	{
		Link_Skip (len);	// nobody is listening, as a send to a closed loopback socket
		return;
	}
	Link_Consume ();
	if (type == 1)
	{
		if (link_sock->receiveMessageLength + len + 4 > link_rxsize)
			Sys_Error ("Link_SendMessage: overflow\n");
	}
	else if (link_sock->receiveMessageLength + len + sizeof(byte) + sizeof(short) > link_rxsize)
	{
		Link_Skip (len);	// a datagram that does not fit is dropped, as in the loopback
		return;
	}
	b = link_sock->receiveMessage + link_sock->receiveMessageLength;
	b[0] = type;
	b[1] = len & 0xff;
	b[2] = len >> 8;
	b[3] = 0;
	qog_link_read (b + 4, len);
	link_sock->receiveMessageLength = IntAlign (link_sock->receiveMessageLength + len + 4);
}

static void Link_SetupSocket (qsocket_t *sock)
{
	sock->receiveMessage = link_rx;
	sock->receiveMessageSize = link_rxsize;
	sock->receiveMessageLength = 0;
	sock->sendMessageLength = 0;
	sock->canSend = true;
	sock->driverdata = (void *)1;	// the other end is open (the loopback's peer pointer)
	link_pending = 0;
}

//=============================================================================
// cvars set on one half are set on the other

void Link_CvarChanged (cvar_t *var)
{
	static byte	buf[256];
	int		a = Q_strlen (var->name), b = Q_strlen (var->string);

	if (link_applying || a + b + 2 > (int)sizeof(buf))
		return;
	memcpy (buf, var->name, a + 1);
	memcpy (buf + a + 1, var->string, b + 1);
	Link_Item (LK_CVAR, buf, a + b + 2);
}

static void Link_ApplyCvar (int len)
{
	static byte	buf[256];
	char	*value;

	Link_ReadText (buf, sizeof(buf), len);
	value = (char *)buf + Q_strlen ((char *)buf) + 1;
	if (value >= (char *)buf + sizeof(buf) || !Cvar_FindVar ((char *)buf))
		return;		// a cvar this half does not have
	link_applying = true;
	Cvar_Set ((char *)buf, value);
	link_applying = false;
}

//=============================================================================
// statistics: link bytes per client frame, from the client's side

static unsigned	ls_frames, ls_last_tx, ls_last_rx;
static unsigned	ls_sum_c2s, ls_sum_s2c, ls_max_c2s, ls_max_s2c, ls_max_frame;
static double	ls_wait;	// seconds the client waited for the server
static unsigned	ls_play, ls_play_c2s, ls_play_s2c;	// frames of play (no signon, level change or load)
static qboolean	ls_signed;

static void Link_Stats_f (void)
{
	qog_link_stats_t *s = &qog_link_stats;

	if (ls_play)
		Con_Printf ("link: %u frames of play, most wire bytes in one client to server %u, server to client %u\n",
			ls_play, ls_play_c2s, ls_play_s2c);
	Con_Printf ("link: %u frames, wire bytes per frame client to server avg %u max %u, server to client avg %u max %u (frame %u)\n",
		ls_frames, ls_frames ? ls_sum_c2s / ls_frames : 0, ls_max_c2s,
		ls_frames ? ls_sum_s2c / ls_frames : 0, ls_max_s2c, ls_max_frame);
	Con_Printf ("link: wire tx %u rx %u, stream tx %u rx %u, frames tx %u rx %u, resends %u dups %u crc drops %u full drops %u sessions %u, waited %.2f s\n",
		s->wire_tx, s->wire_rx, s->data_tx, s->data_rx, s->frames_tx, s->frames_rx,
		s->resends, s->dups, s->crc_drops, s->full_drops, s->sessions, ls_wait);
#if defined(QOG_LINK_PIPELINE) && defined(QOG_SPLIT_CLIENT)
	{
		extern unsigned	host_qog_pipelined;	// host.c
		Con_Printf ("link: %u frames drawn while the server half ran its next frame\n", host_qog_pipelined);
	}
#endif
}

//=============================================================================
// the client half

#ifdef QOG_SPLIT_CLIENT

qog_svmirror_t		sv;		// the server's sv.active, sv.paused ... as the server last sent them
qog_svsmirror_t	svs;
qboolean		noclip_anglehack;	// host_cmd.c's on the server, read by the client (cl_parse.c, view.c)
cvar_t			sv_gravity = {"sv_gravity", "800", false, true};	// sv_phys.c's, read by r_part.c

static qboolean	link_waiting;		// inside an exchange
static int		link_error;			// LK_HOSTERROR or LK_ENDGAME arrived
static char		link_errtext[256];

static struct { double realtime, frametime; unsigned rand; int keygame; } cs_sent;

static void Link_SyncState (void)
{
	byte	b[21];
	int		keygame = key_dest == key_game;

	if (cs_sent.realtime == realtime && cs_sent.frametime == host_frametime
		&& cs_sent.rand == qog_rand_calls && cs_sent.keygame == keygame)
		return;
	cs_sent.realtime = realtime;
	cs_sent.frametime = host_frametime;
	cs_sent.rand = qog_rand_calls;
	cs_sent.keygame = keygame;
	memcpy (b, &realtime, 8);
	memcpy (b + 8, &host_frametime, 8);
	put32 (b + 16, qog_rand_calls);
	b[20] = keygame;
	Link_RawItem (LK_CSTATE, b, sizeof(b));
}

static void Link_ApplySState (int len)
{
	byte	b[SSTATE_SIZE];

	if (len != sizeof(b))
		Sys_Error ("link: bad server state (%d bytes)", len);
	qog_link_read (b, len);
	memcpy (&host_frametime, b, 8);
	Link_RandTo (get32 (b + 8));
	sv.max_edicts = get32 (b + 12);
	svs.maxclients = get32 (b + 16);
	svs.maxclientslimit = get32 (b + 20);
	sv.active = b[24];
	sv.paused = b[25];
	noclip_anglehack = b[26];
	memcpy (sv.name, b + 27, 64);
	sv.name[63] = 0;
	cs_sent.frametime = host_frametime;		// the server set it, nothing to tell
	cs_sent.rand = qog_rand_calls;
}

// read the server's items until the one that ends this exchange
static void Link_ClientWait (int endkind)
{
	static byte	text[LINK_CLTEXT + 1];	// short lines; prints and added text come in pieces
	double	t0 = Sys_FloatTime ();
	int		kind, len, flag, n;

	link_waiting = true;
	for (;;)
	{
		kind = Link_ReadHeader (&len);
		switch (kind)
		{
		case LK_SSTATE:		Link_ApplySState (len); break;
		case LK_REL:		Link_Deliver (1, len); break;
		case LK_UNREL:		Link_Deliver (2, len); break;
		case LK_ACKREL:		Link_Skip (len); if (link_sock) link_sock->canSend = true; break;
		case LK_ACCEPT:
			Link_Skip (len);
			if (link_sock)
			{	// Loop_CheckNewConnections cleared the client's side too
				link_sock->receiveMessageLength = 0;
				link_sock->canSend = true;
				link_pending = 0;
			}
			break;
		case LK_CLOSE:		Link_Skip (len); if (link_sock) link_sock->driverdata = NULL; break;
		case LK_CVAR:		Link_ApplyCvar (len); break;
		case LK_PRINT:
		case LK_CBUF:
			qog_link_read (text, 1);
			flag = text[0];
			len--;
			if (kind == LK_CBUF && flag)
			{	// an insert must be one piece (only id's QUAKE2 code inserts here)
				Link_ReadText (text, sizeof(text), len);
				Cbuf_InsertText ((char *)text);
				break;
			}
			while (len > 0)
			{
				n = len > LINK_CLTEXT ? LINK_CLTEXT : len;
				qog_link_read (text, n);
				text[n] = 0;
				len -= n;
				if (kind == LK_CBUF)
					Cbuf_AddText ((char *)text);
				else if (flag)
					Con_SafePrintf ("%s", text);
				else
					Con_Printf ("%s", text);
			}
			break;
		case LK_CLEXEC:
			Link_ReadText (text, sizeof(text), len);
			Cmd_ExecuteString ((char *)text + 1, src_command);	// byte 0 is the flag
			break;
		case LK_CLEARMEM:	Link_Skip (len); Host_ClearMemory (); break;
		case LK_CENTEROFF:
			{
				extern float scr_centertime_off;
				Link_Skip (len);
				scr_centertime_off = 0;
			}
			break;
		case LK_HOSTERROR:
		case LK_ENDGAME:
			Link_ReadText ((byte *)link_errtext, sizeof(link_errtext), len);
			link_error = kind;
			break;
		case LK_DONE:
		case LK_SYNCDONE:
			Link_Skip (len);
			if (kind != endkind)
				Sys_Error ("link: exchange out of step");
			link_waiting = false;
			ls_wait += Sys_FloatTime () - t0;
			if (link_error)
			{	// the client's half of Host_Error / Host_EndGame on the server
				int what = link_error;
				link_error = 0;
				if (what == LK_HOSTERROR)
				{
					SCR_EndLoadingPlaque ();
					CL_Disconnect ();
					cls.demonum = -1;
				}
				else if (cls.demonum != -1)
					CL_NextDemo ();
				else
					CL_Disconnect ();
				longjmp (host_abortserver, 1);
			}
			return;
		default:
			Sys_Error ("link: unknown item %d from the server", kind);
		}
	}
}

static void Link_ClientStats (void)
{
	unsigned	c2s, s2c;

	c2s = qog_link_stats.wire_tx - ls_last_tx;
	s2c = qog_link_stats.wire_rx - ls_last_rx;
	ls_last_tx = qog_link_stats.wire_tx;
	ls_last_rx = qog_link_stats.wire_rx;
	ls_frames++;
	ls_sum_c2s += c2s;
	ls_sum_s2c += s2c;
	if (c2s > ls_max_c2s)
		ls_max_c2s = c2s;
	if (s2c > ls_max_s2c)
	{
		ls_max_s2c = s2c;
		ls_max_frame = host_framecount;
	}
	if (ls_signed && cls.signon == SIGNONS)
	{	// a frame of play: signed on before and after it
		ls_play++;
		if (c2s > ls_play_c2s)
			ls_play_c2s = c2s;
		if (s2c > ls_play_s2c)
			ls_play_s2c = s2c;
	}
	ls_signed = cls.signon == SIGNONS;
}

void Link_ClientFrame (void)
{
	Link_Item (LK_TICK, NULL, 0);
	Link_ClientWait (LK_DONE);
	Link_ClientStats ();
}

#ifdef QOG_LINK_PIPELINE
// QuakeOG (Agent V): Link_ClientFrame in two halves for host.c's pipelined
// frame. The tick goes onto the wire at once, so the server half starts its
// frame while this half draws the last one, then the client waits for the
// answer as Link_ClientFrame does. No other exchange may start in between.
void Link_ClientTick (void)
{
	Link_Item (LK_TICK, NULL, 0);
	qog_link_push ();
	link_waiting = true;
}

void Link_ClientDone (void)
{
	Link_ClientWait (LK_DONE);
	Link_ClientStats ();
}
#endif

void Link_ForwardCommand (char *text)
{
	if (link_waiting)
	{
		Con_Printf ("link: \"%s\" not sent, the server is busy\n", text);
		return;
	}
	Link_ItemText (LK_CMD, 0, text);
	Link_ClientWait (LK_SYNCDONE);
}

void Link_SaveInfo (qboolean intermission, char *comment)
{
	Link_ItemText (LK_SAVEINFO, intermission, comment);
}

void Link_Quit (void)
{
	if (!link_started)
		return;
	Link_Stats_f ();
	Link_Item (LK_QUIT, NULL, 0);
	qog_link_drain ();
}

// `qog_sv <command>`: run a console line on the server even if the client has
// a command of that name (qog_mem, ...)
static void Link_Sv_f (void)
{
	if (Cmd_Argc () < 2)
	{
		Con_Printf ("qog_sv <command> : run a console line on the server\n");
		return;
	}
	Link_ForwardCommand (Cmd_Args ());
}

// QuakeOG (Agent T): the save list (see LINK_SAVESLOTS). The client asks
// between exchanges, while the server waits for its frame, and asks again
// every 250 ms until the answer with its number arrives (5 s at most).
static byte		link_saves[LINK_SAVESIZE];
static qboolean	link_saves_got;
static byte		link_saves_seq;

static void Link_ClientSide (const uint8_t *p, int len)
{
	if (p[0] == QOG_LINK_T_SAVES && len == LINK_SAVESIZE && p[1] == link_saves_seq)
	{
		memcpy (link_saves, p, len);
		link_saves_got = true;
	}
}

void Link_ScanSaves (char names[][SAVEGAME_COMMENT_LENGTH+1], int *found, int count)
{
	byte		ask[2];
	unsigned	start, last, now;
	byte		*e;
	int			i;

	for (i=0 ; i<count ; i++)
	{
		names[i][0] = 0;
		found[i] = false;
	}
	if (!link_started || link_waiting)
		return;
	link_saves_got = false;
	ask[0] = QOG_LINK_T_SAVES_ASK;
	ask[1] = ++link_saves_seq;
	start = last = qog_link_hw_ms ();
	qog_link_send_side (ask, 2);
	while (!link_saves_got)
	{
		now = qog_link_hw_ms ();
		if (now - start > 5000)
		{
			Con_Printf ("link: no save list from the server\n");
			return;
		}
		if (now - last > 250)
		{
			qog_link_send_side (ask, 2);
			last = now;
		}
		qog_link_pump ();
		if (!link_saves_got)
			qog_link_hw_idle ();
	}
	for (i=0 ; i<count && i<link_saves[2] ; i++)
	{
		e = link_saves + 3 + i * LINK_SAVEENTRY;
		found[i] = e[0];
		memcpy (names[i], e + 1, SAVEGAME_COMMENT_LENGTH);
		names[i][SAVEGAME_COMMENT_LENGTH] = 0;
	}
}

// `qog_saves`: the save list the load and save menus show, for tests
static void Link_Saves_f (void)
{
	extern char	m_filenames[][SAVEGAME_COMMENT_LENGTH+1];
	extern int	loadable[];
	void		M_ScanSaves (void);
	int			i;

	M_ScanSaves ();
	for (i=0 ; i<LINK_SAVESLOTS ; i++)
		Con_Printf ("s%i: %i %s\n", i, loadable[i], m_filenames[i]);
}

#endif	// QOG_SPLIT_CLIENT

//=============================================================================
// the server half

#ifdef QOG_SPLIT_SERVER

qboolean	link_keygame = true;
qboolean	link_cl_intermission;
char		link_save_comment[SAVEGAME_COMMENT_LENGTH + 1];

static qboolean	link_inexchange;	// prints reach the client's console only from here
static qboolean	link_conn;			// the client has a connection (pending or taken)
static qboolean	link_connpending;
static int		link_open;			// 1 in a frame exchange, 2 in a command

static byte	ss_sent[SSTATE_SIZE];
extern qboolean	noclip_anglehack;	// host_cmd.c
static qboolean	ss_valid;

static void Link_SyncState (void)
{
	byte	b[sizeof(ss_sent)];

	memset (b, 0, sizeof(b));
	memcpy (b, &host_frametime, 8);
	put32 (b + 8, qog_rand_calls);
	put32 (b + 12, sv.max_edicts);
	put32 (b + 16, svs.maxclients);
	put32 (b + 20, svs.maxclientslimit);
	b[24] = sv.active;
	b[25] = sv.paused;
	b[26] = noclip_anglehack;
	Q_strncpy ((char *)b + 27, sv.name, 63);
	if (ss_valid && !memcmp (b, ss_sent, sizeof(b)))
		return;
	memcpy (ss_sent, b, sizeof(b));
	ss_valid = true;
	Link_RawItem (LK_SSTATE, b, sizeof(b));
}

static void Link_ApplyCState (int len)
{
	byte	b[21];

	if (len != sizeof(b))
		Sys_Error ("link: bad client state (%d bytes)", len);
	qog_link_read (b, len);
	memcpy (&realtime, b, 8);
	memcpy (&host_frametime, b + 8, 8);
	Link_RandTo (get32 (b + 16));
	link_keygame = b[20];
	// what the client knows already, so nothing goes back for it
	memcpy (ss_sent, &host_frametime, 8);
	put32 (ss_sent + 8, qog_rand_calls);
}

void Link_ServerPeerClosed (void)
{
	if (link_conn)
	{
		link_conn = false;
		link_connpending = false;
		if (link_sock)
			link_sock->driverdata = NULL;
	}
	if (sv.active)
		Host_ShutdownServer (false);	// what the client's CL_Disconnect did to a local server
}

void Link_ServerPrint (char *text, qboolean safe)
{
	if (link_inexchange)
		Link_ItemText (LK_PRINT, safe, text);
}

void Link_ServerCbuf (char *text, qboolean insert)
{
	Link_ItemText (LK_CBUF, insert, text);
}

void Link_ServerClientExec (char *text)
{
	Link_ItemText (LK_CLEXEC, 0, text);
}

void Link_ServerClearMemory (void)
{
	Link_Item (LK_CLEARMEM, NULL, 0);
}

void Link_ServerCenterOff (void)
{
	Link_Item (LK_CENTEROFF, NULL, 0);
}

void Link_ServerHostError (char *text)
{
	Link_ItemText (LK_HOSTERROR, 0, text);
}

void Link_ServerEndGame (char *text)
{
	Link_ItemText (LK_ENDGAME, 0, text);
}

static void Link_ServerFinish (int kind)
{
	Link_Item (kind, NULL, 0);
	link_inexchange = false;
}

// the client's part of the frame: items up to LK_TICK, console lines run as they come
void Link_ServerWaitTick (void)
{
	static byte	text[LINK_SVTEXT + 2];
	int		kind, len;

	if (!link_started)
		Sys_Error ("link: not started");
	link_open = 1;
	for (;;)
	{
		kind = Link_ReadHeader (&len);
		link_inexchange = true;
		switch (kind)
		{
		case LK_CSTATE:		Link_ApplyCState (len); break;
		case LK_REL:		Link_Deliver (1, len); break;
		case LK_UNREL:		Link_Deliver (2, len); break;
		case LK_ACKREL:		Link_Skip (len); if (link_sock) link_sock->canSend = true; break;
		case LK_CONNECT:
			Link_Skip (len);
			link_conn = true;
			link_connpending = true;
			break;
		case LK_CLOSE:
			Link_Skip (len);
			if (link_conn)
				Link_ServerPeerClosed ();
			break;
		case LK_CVAR:		Link_ApplyCvar (len); break;
		case LK_SAVEINFO:
			Link_ReadText (text, sizeof(text), len);
			link_cl_intermission = text[0];
			Q_strncpy (link_save_comment, (char *)text + 1, SAVEGAME_COMMENT_LENGTH);
			link_save_comment[SAVEGAME_COMMENT_LENGTH] = 0;
			break;
		case LK_CMD:
			Link_ReadText (text, sizeof(text), len);
			if (COM_CheckParm ("-linkdebug"))
				Sys_Printf ("link: command \"%s\"\n", text);
			link_open = 2;
			Cmd_ExecuteString ((char *)text + 1, src_command);	// byte 0 is the flag
			link_open = 1;
			Link_ServerFinish (LK_SYNCDONE);
			break;
		case LK_TICK:
			Link_Skip (len);
			return;		// link_inexchange stays on through the server frame
		case LK_QUIT:
			Link_Skip (len);
			Sys_Printf ("link: the client quit\n");
			Sys_Quit ();
			break;
		default:
			Sys_Error ("link: unknown item %d from the client", kind);
		}
	}
}

void Link_ServerFrameDone (void)
{
	link_open = 0;
	Link_ServerFinish (LK_DONE);
}

// Host_Error or Host_EndGame jumped out of the server's frame: the client
// still waits for the end of the command or the frame
void Link_ServerAbort (void)
{
	if (link_open == 2)
	{
		Link_ServerFinish (LK_SYNCDONE);
		link_open = 1;
		Link_ServerWaitTick ();		// the rest of the client's frame
	}
	if (link_open == 1)
		Link_ServerFrameDone ();
}

// QuakeOG (Agent T): the client's load and save menus ask for the save list
// (see LINK_SAVESLOTS). The saves are this half's files: main's FAT volume
// on the board (port/rp2040/sys_rp2040.c), the gamedir on the PC. Each
// slot is read as M_ScanSaves reads it. Runs from the link's pump, while
// the server waits for the client's frame.
static void Link_ServerSide (const uint8_t *p, int len)
{
	byte	out[LINK_SAVESIZE];
	char	name[MAX_OSPATH];
	FILE	*f;
	int		i, version;

	if (p[0] != QOG_LINK_T_SAVES_ASK || len < 2)
		return;
	memset (out, 0, sizeof(out));
	out[0] = QOG_LINK_T_SAVES;
	out[1] = p[1];
	out[2] = LINK_SAVESLOTS;
	for (i=0 ; i<LINK_SAVESLOTS ; i++)
	{
		sprintf (name, "%s/s%i.sav", com_gamedir, i);
		f = fopen (name, "r");
		if (!f)
			continue;
		fscanf (f, "%i\n", &version);
		fscanf (f, "%79s\n", name);
		fclose (f);
		out[3 + i * LINK_SAVEENTRY] = 1;
		strncpy ((char *)out + 4 + i * LINK_SAVEENTRY, name, SAVEGAME_COMMENT_LENGTH);
	}
	qog_link_send_side (out, sizeof(out));
}

#endif	// QOG_SPLIT_SERVER

//=============================================================================
// the driver

int Link_Init (void)
{
	link_rxsize = LINK_RXSIZE;
	link_rx = Hunk_AllocName (link_rxsize + 4, "link_rx");	// 4 spare bytes, as net_loop.c
	Cmd_AddCommand ("qog_link", Link_Stats_f);
#ifdef QOG_SPLIT_CLIENT
	Cmd_AddCommand ("qog_sv", Link_Sv_f);
	Cmd_AddCommand ("qog_saves", Link_Saves_f);	// QuakeOG (Agent T)
	Cvar_RegisterVariable (&sv_gravity);
	qog_link_side = Link_ClientSide;
#else
	qog_link_side = Link_ServerSide;	// QuakeOG (Agent T): the save list
#endif
	if (!COM_CheckParm ("-nolink"))
	{
		qog_link_start (LINK_ROLE);
		link_started = true;
	}
	return 0;
}

void Link_Shutdown (void)
{
}

void Link_Listen (qboolean state)
{
}

void Link_SearchForHosts (qboolean xmit)
{
}

// net_message for a demo is read in the client's receive buffer, idle while
// one plays (cl_demo.c, as net_loop.c)
byte *Loop_DemoBuffer (void)
{
	if (link_sock)
		Sys_Error ("Loop_DemoBuffer: demo played while connected");
	return link_rx;
}

qsocket_t *Link_Connect (char *host)
{
#ifdef QOG_SPLIT_CLIENT
	if (Q_strcmp (host, "local") != 0)
		return NULL;
	if (!link_sock)
	{
		if ((link_sock = NET_NewQSocket ()) == NULL)
		{
			Con_Printf ("Link_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy (link_sock->address, "localhost");
	}
	Link_SetupSocket (link_sock);
	Link_Item (LK_CONNECT, NULL, 0);
	return link_sock;
#else
	return NULL;
#endif
}

qsocket_t *Link_CheckNewConnections (void)
{
#ifdef QOG_SPLIT_SERVER
	if (!link_connpending)
		return NULL;
	link_connpending = false;
	if (!link_sock)
	{
		if ((link_sock = NET_NewQSocket ()) == NULL)
		{
			Con_Printf ("Link_CheckNewConnections: no qsocket available\n");
			return NULL;
		}
		Q_strcpy (link_sock->address, "LOCAL");
	}
	Link_SetupSocket (link_sock);
	Link_Item (LK_ACCEPT, NULL, 0);
	return link_sock;
#else
	return NULL;
#endif
}

int Link_GetMessage (qsocket_t *sock)
{
	int		ret, length;

	Link_Consume ();
	if (sock->receiveMessageLength == 0)
		return 0;

	ret = sock->receiveMessage[0];
	length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
	net_message.data = &sock->receiveMessage[4];	// in place, as QOG_LOOP_INPLACE
	net_message.maxsize = net_message.cursize = length;
	link_pending = IntAlign (length + 4);

	if (sock->driverdata && ret == 1)
		Link_Item (LK_ACKREL, NULL, 0);	// the sender's canSend comes back on

	return ret;
}

int Link_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	if (!sock->driverdata)
		return -1;
	Link_Item (LK_REL, data->data, data->cursize);
	sock->canSend = false;
	return 1;
}

int Link_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	if (!sock->driverdata)
		return -1;
	Link_Item (LK_UNREL, data->data, data->cursize);
	return 1;
}

qboolean Link_CanSendMessage (qsocket_t *sock)
{
	if (!sock->driverdata)
		return false;
	return sock->canSend;
}

qboolean Link_CanSendUnreliableMessage (qsocket_t *sock)
{
	return true;
}

void Link_Close (qsocket_t *sock)
{
	if (sock->driverdata)
		Link_Item (LK_CLOSE, NULL, 0);
	sock->driverdata = NULL;
	sock->receiveMessageLength = 0;
	sock->sendMessageLength = 0;
	sock->canSend = true;
	link_pending = 0;
	if (sock == link_sock)
		link_sock = NULL;
#ifdef QOG_SPLIT_SERVER
	link_conn = false;	// the server dropped the client
	link_connpending = false;
#endif
}

#endif	// QOG_NET_LINK
