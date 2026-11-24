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

#include "net_loop.h"
#ifdef QOG_NET_LINK
#include "net_link.h"
#endif

net_driver_t net_drivers[NET_DRIVER_SLOTS] =	// QuakeOG: was MAX_NET_DRIVERS
{
#ifdef QOG_NET_LINK
	{	// QuakeOG: the split game's link between the halves (net_link.c)
	"Link",
	false,
	Link_Init,
	Link_Listen,
	Link_SearchForHosts,
	Link_Connect,
	Link_CheckNewConnections,
	Link_GetMessage,
	Link_SendMessage,
	Link_SendUnreliableMessage,
	Link_CanSendMessage,
	Link_CanSendUnreliableMessage,
	Link_Close,
	Link_Shutdown
	}
#else
	{
	"Loopback",
	false,
	Loop_Init,
	Loop_Listen,
	Loop_SearchForHosts,
	Loop_Connect,
	Loop_CheckNewConnections,
	Loop_GetMessage,
	Loop_SendMessage,
	Loop_SendUnreliableMessage,
	Loop_CanSendMessage,
	Loop_CanSendUnreliableMessage,
	Loop_Close,
	Loop_Shutdown
	}
#endif
};
int net_numdrivers = 1;

net_landriver_t	net_landrivers[NET_LANDRIVER_SLOTS];	// QuakeOG: was MAX_NET_DRIVERS
int net_numlandrivers = 0;
