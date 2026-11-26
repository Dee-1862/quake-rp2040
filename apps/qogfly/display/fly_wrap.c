/* Link-time replacements for the fly-through (-Wl,--wrap in
 * apps/qogfly/CMakeLists.txt). Each engine call below does nothing useful
 * in a host that has no console, sound, server or console commands, but
 * links in the code and static RAM of the parts it would reach (the console
 * and screen, the menus, the sound mixer, the server). The replacements do
 * what id's functions do in this host, where Con_Init, S_Init and Host_Init
 * never run. No engine file changes. */
#include <stdarg.h>
#include <stdio.h>
#include "quakedef.h"

/* id's Con_Printf echoes to Sys_Printf and returns while con_initialized is
   false, which it always is here. */
void __wrap_Con_Printf (char *fmt, ...)
{
	char	msg[256];
	va_list	ap;

	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	va_end (ap);
	Sys_Printf ("%s", msg);
}

void __wrap_Con_SafePrintf (char *fmt, ...)
{
	char	msg[256];
	va_list	ap;

	va_start (ap, fmt);
	vsnprintf (msg, sizeof msg, fmt, ap);
	va_end (ap);
	Sys_Printf ("%s", msg);
}

/* Prints only with the developer cvar set, which nothing registers here. */
void __wrap_Con_DPrintf (char *fmt, ...)
{
	(void)fmt;
}

/* No server to abort, id's Host_Error would longjmp to a host_abortserver
   that Host_Init never set up. Stop with the message instead. */
void __wrap_Host_Error (char *error, ...)
{
	char	msg[256];
	va_list	ap;

	va_start (ap, error);
	vsnprintf (msg, sizeof msg, error, ap);
	va_end (ap);
	Sys_Error ("Host_Error: %s", msg);
}

/* Sound never starts (S_Init is not called), so id's S_ExtraUpdate returns
   at once. */
void __wrap_S_ExtraUpdate (void)
{
}

/* VID_Init registers the qog_mem and aim console commands and cvars, which
   need a console and a server. */
void __wrap_qog_mem_register (void)
{
}

void __wrap_qog_aim_register (void)
{
}
