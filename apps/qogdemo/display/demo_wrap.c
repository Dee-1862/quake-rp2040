/* What the demo player links instead of id's host, server, console, keys,
 * menus, network and sound objects (apps/qogdemo).
 *
 * The client and renderer objects refer to globals and functions of those
 * parts. Defined here, the linker never takes the objects themselves from
 * the engine library, and with them their static RAM (the server state
 * alone is 11 KB) and everything they refer to in turn. Each definition is
 * what id's own does in a host where Host_Init's server, console, keys,
 * menus, network and sound never start, which is how port/common/qog_demo.c
 * runs on the PC too. No engine file changes.
 *
 * The server state is in demo_sv.c. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include "quakedef.h"

/* ---- host.c ------------------------------------------------------------ */

quakeparms_t	host_parms;
qboolean	host_initialized;
double		host_frametime;
double		host_time;
double		realtime;
double		oldrealtime;
int			host_framecount;
int			host_hunklevel;
jmp_buf		host_abortserver;
byte		*host_basepal;
byte		*host_colormap;
cvar_t		teamplay = {"teamplay","0",false,true};	// Sbar's deathmatch overlay reads it
cvar_t		developer = {"developer","0"};

/* ---- sv_phys.c, the client's particles fall by the server's gravity ---- */

cvar_t		sv_gravity = {"sv_gravity","800",false,true};

/* id's, it shuts the server down if one runs, which never happens here,
   then plays the next demo of the loop or disconnects, and leaves the
   frame (port/common/qog_demo.c sets host_abortserver). */
void Host_EndGame (char *message, ...)
{
	(void)message;
	if (cls.demonum != -1)
		CL_NextDemo ();
	else
		CL_Disconnect ();
	longjmp (host_abortserver, 1);
}

/* No server to abort, id's Host_Error would longjmp to it. Stop with the
   message instead, as apps/qogfly does. */
void Host_Error (char *error, ...)
{
	char	msg[256];
	va_list	ap;

	va_start (ap, error);
	vsnprintf (msg, sizeof msg, error, ap);
	va_end (ap);
	Sys_Error ("Host_Error: %s", msg);
}

/* id's, without clearing the server state (never used here). */
void Host_ClearMemory (void)
{
	D_FlushCaches ();
	Mod_ClearAll ();
	if (host_hunklevel)
		Hunk_FreeToLowMark (host_hunklevel);
	cls.signon = 0;
	memset (&cl, 0, sizeof(cl));
}

void Host_ShutdownServer (qboolean crash)
{
	(void)crash;		// no server ever runs
}

/* Cvar_Set's notice to the clients of a server cvar change, sent only
   while a server runs (cvar.c). */
void SV_BroadcastPrintf (char *fmt, ...)
{
	(void)fmt;
}

/* ---- host_cmd.c, keys.c, menu.c ---------------------------------------- */

qboolean	noclip_anglehack;	// only the server's noclip command sets it
keydest_t	key_dest;			// key_game, no console or menu ever opens
int			key_count;			// SCR_ModalMessage's key wait, never run here
int			key_lastpress;

void M_Draw (void)	// SCR_UpdateScreen's menu, not run here
{
}

/* id's, for Sbar's intermission screens */
void M_DrawPic (int x, int y, qpic_t *pic)
{
	Draw_Pic (x + ((vid.width - 320)>>1), y, pic);
}

void M_Menu_Options_f (void)	// the video menu's escape key (vid_rp2040.c)
{
}

/* vid_rp2040.c's Sys_SendKeyEvents turns the buttons into key events. The
   demo host never calls it (port/common/qog_demo.c reads the buttons in
   apps/qogdemo/display/main.c), and id's Key_Event needs the key tables
   and the menus. */
void Key_Event (int key, qboolean down)
{
	(void)key;
	(void)down;
}

/* ---- console.c --------------------------------------------------------- */

qboolean	con_initialized;	// false, Con_Init never runs
qboolean	con_forcedup;		// only SCR_SetUpToDrawConsole sets it
int			con_notifylines;

/* id's Con_Printf echoes to Sys_Printf and returns while the console is
   not initialized. */
void Con_Printf (char *fmt, ...)
{
	char	msg[256];
	va_list	ap;

	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	va_end (ap);
	Sys_Printf ("%s", msg);
}

void Con_SafePrintf (char *fmt, ...)
{
	char	msg[256];
	va_list	ap;

	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	va_end (ap);
	Sys_Printf ("%s", msg);
}

void Con_DPrintf (char *fmt, ...)	// prints only with developer set
{
	(void)fmt;
}

/* A console that never starts. The host never runs the draws (they are
   called from SCR_UpdateScreen, which it does not use), and clearing the
   notify times of empty lines does nothing. */
void Con_ClearNotify (void) {}
void Con_CheckResize (void) {}
void Con_DrawNotify (void) {}
void Con_DrawConsole (int lines, qboolean drawinput) { (void)lines; (void)drawinput; }
void Con_DrawCharacter (int cx, int line, int num) { (void)cx; (void)line; (void)num; }
void Con_NotifyBox (char *text) { (void)text; }

/* ---- sound, S_Init never runs, so id's functions return at once -------- */

void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation) {}
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation) {}
void S_StopSound (int entnum, int entchannel) {}
void S_StopAllSounds (qboolean clear) {}
void S_ClearBuffer (void) {}
void S_Update (vec3_t origin, vec3_t v_forward, vec3_t v_right, vec3_t v_up) {}
void S_ExtraUpdate (void) {}
sfx_t *S_PrecacheSound (char *sample) { return NULL; }
void S_TouchSound (char *sample) {}
void S_BeginPrecaching (void) {}
void S_EndPrecaching (void) {}
void S_LocalSound (char *s) {}

/* ---- network, a demo client is never connected ------------------------- */

sizebuf_t	net_message;		// a demo's messages are parsed in place (QOG_DEMO_FLASH)

/* The buffer a demo read through a FILE goes into (net_loop.c). On the
   board every demo is in the mapped pack and is read in place. */
byte *Loop_DemoBuffer (void)
{
	Sys_Error ("qogdemo: a demo outside the mapped pack");
	return NULL;
}

int NET_SendUnreliableMessage (struct qsocket_s *sock, sizebuf_t *data) { return -1; }
int NET_SendMessage (struct qsocket_s *sock, sizebuf_t *data) { return -1; }
qboolean NET_CanSendMessage (struct qsocket_s *sock) { return false; }
int NET_GetMessage (struct qsocket_s *sock) { return -1; }
void NET_Close (struct qsocket_s *sock) {}
struct qsocket_s *NET_Connect (char *host) { return NULL; }

/* ---- world.c, chase.c's TraceLine, used only with chase_active on ------ */

qboolean SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	Sys_Error ("qogdemo: chase_active is not supported");
	return true;
}

/* ---- port helpers called from VID_Init -------------------------------- */

void qog_mem_set_video (int bytes) { (void)bytes; }	// the PC's memory meter

void qog_mem_register (void) {}	// console commands for the full game
void qog_aim_register (void) {}
