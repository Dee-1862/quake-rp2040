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
/* QuakeOG: what the split game's server half (QOG_SPLIT_SERVER) keeps of the
 * client side of id's engine.
 *
 *  - The console is the client's. Con_Printf here prints to the port's log
 *    and sends the text to the client's console (engine/net_link.c), where
 *    id's single-process game would have printed it.
 *  - V_CalcRoll is view.c's, used by SV_ClientThink, with the two client
 *    cvars it reads (set on the client, copied here like every cvar).
 *  - The server builds no renderer: the stubs below stand for calls the
 *    shared code (model.c, zone.c) makes into it, which never happen on a
 *    server or do nothing there.
 */
#include "quakedef.h"
#include "net_link.h"
#include "qog_mem.h"

#define MAXPRINTMSG 4096

qboolean	con_initialized = true;
int			con_linewidth = 78;

void Con_Printf (char *fmt, ...)
{
	va_list	argptr;
	char	msg[MAXPRINTMSG];

	va_start (argptr, fmt);
	vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);
	Sys_Printf ("%s", msg);
	Link_ServerPrint (msg, false);
}

void Con_DPrintf (char *fmt, ...)
{
	va_list	argptr;
	char	msg[MAXPRINTMSG];

	if (!developer.value)
		return;
	va_start (argptr, fmt);
	vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);
	Con_Printf ("%s", msg);
}

void Con_SafePrintf (char *fmt, ...)
{
	va_list	argptr;
	char	msg[MAXPRINTMSG];

	va_start (argptr, fmt);
	vsnprintf (msg, sizeof(msg), fmt, argptr);
	va_end (argptr);
	Sys_Printf ("%s", msg);
	Link_ServerPrint (msg, true);
}

// view.c
cvar_t	cl_rollspeed = {"cl_rollspeed", "200"};
cvar_t	cl_rollangle = {"cl_rollangle", "2.0"};

float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	vec3_t	forward, right, up;
	float	sign;
	float	side;
	float	value;

	AngleVectors (angles, forward, right, up);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs(side);

	value = cl_rollangle.value;
	if (side < cl_rollspeed.value)
		side = side * value / cl_rollspeed.value;
	else
		side = value;

	return side*sign;
}

void Sys_QogRegister (void)
{
	Cvar_RegisterVariable (&cl_rollspeed);
	Cvar_RegisterVariable (&cl_rollangle);
	Cvar_RegisterVariable (&cl_name);
	qog_mem_register ();
}

// renderer state and calls the shared loaders touch (model.c, common.c)
cvar_t		cl_name = {"_cl_name", "player", true};	// host_cmd.c kick, copied from the client
int			r_pixbytes = 1;
unsigned short	d_8to16table[256];
texture_t	*r_notexture_mip;
void R_InitSky (texture_t *mt) { (void)mt; }
void Draw_BeginDisc (void) {}
void Draw_EndDisc (void) {}
