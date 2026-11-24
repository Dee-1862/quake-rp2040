/*
Copyright (C) 2026 QuakeOG contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/
// net_link.h -- QuakeOG: the split game's net driver (QOG_NET_LINK), see net_link.c

int			Link_Init (void);
void		Link_Listen (qboolean state);
void		Link_SearchForHosts (qboolean xmit);
qsocket_t 	*Link_Connect (char *host);
qsocket_t 	*Link_CheckNewConnections (void);
int			Link_GetMessage (qsocket_t *sock);
int			Link_SendMessage (qsocket_t *sock, sizebuf_t *data);
int			Link_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data);
qboolean	Link_CanSendMessage (qsocket_t *sock);
qboolean	Link_CanSendUnreliableMessage (qsocket_t *sock);
void		Link_Close (qsocket_t *sock);
void		Link_Shutdown (void);

// both halves
void		Link_CvarChanged (cvar_t *var);	// a cvar was set here: the other half sets it too
extern qboolean	link_applying;				// true while a cvar from the other half is set

#ifdef QOG_SPLIT_CLIENT
void		Link_ClientFrame (void);		// where the server frame ran: hand the frame to the server and wait
void		Link_ForwardCommand (char *text);	// run a console line on the server, now
void		Link_SaveInfo (qboolean intermission, char *comment);	// what save needs from the client
void		Link_Quit (void);
#endif

#ifdef QOG_SPLIT_SERVER
void		Link_ServerWaitTick (void);		// the client's part of the frame, its console lines run here
void		Link_ServerFrameDone (void);
void		Link_ServerAbort (void);		// after host_abortserver: finish the exchange
void		Link_ServerPrint (char *text, qboolean safe);
void		Link_ServerCbuf (char *text, qboolean insert);
void		Link_ServerClientExec (char *text);	// a console line the client runs now
void		Link_ServerClearMemory (void);
void		Link_ServerCenterOff (void);
void		Link_ServerHostError (char *text);
void		Link_ServerEndGame (char *text);
void		Link_ServerPeerClosed (void);	// the client closes its connection (a local CL_Disconnect)
extern qboolean	link_keygame;			// the client's key_dest == key_game
extern qboolean	link_cl_intermission;	// cl.intermission, sent with save
extern char		link_save_comment[];		// Host_SavegameComment, sent with save
void		Sys_QogRegister (void);		// the port's console commands (port)
#endif

// QuakeOG (Agent W): the split game's sound. The client half's S_ calls
// (snd_link.c, QOG_SND_REMOTE) run as these operations on the server half,
// which mixes with id's code (QOG_SND_HOST) and sends the 8-bit samples back.
enum {
	LKS_PRECACHE = 1,	// name: S_PrecacheSound, answers the sound's number or -1
	LKS_TOUCH,			// name: S_TouchSound
	LKS_START,			// S_StartSound (its random number is drawn on the server half)
	LKS_LOCAL,			// name: S_LocalSound
	LKS_STOP,			// S_StopSound
	LKS_STOPALL,		// S_StopAllSounds
	LKS_CLEAR,			// S_ClearBuffer
	LKS_STATIC,			// S_StaticSound
	LKS_UPDATE,			// S_Update with the listener, answers 1 if it mixed
	LKS_EXTRA,			// S_ExtraUpdate, answers 1 if it mixed
	LKS_AMBOFF,			// S_AmbientOff
	LKS_AMBON			// S_AmbientOn
};
#define LKS_REPLY	0x80	// with the operation: the client waits for the answer

#ifdef QOG_SND_REMOTE
int			Link_Sound (const byte *op, int len);	// returns the answer, -1 when none came
#define LINK_SNDQUEUED	-2		// sent from inside an exchange, runs after it
extern unsigned	link_sndasync, link_sndasync_start;
#endif
#ifdef QOG_SND_HOST
void		Link_ServerSoundPcm (unsigned t, const byte *a, int an, const byte *b, int bn);	// samples from time t
#endif
