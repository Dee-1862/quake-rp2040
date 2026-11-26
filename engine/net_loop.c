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
// net_loop.c

#include "quakedef.h"
#include "net_loop.h"

qboolean	localconnectpending = false;
qsocket_t	*loop_client = NULL;
qsocket_t	*loop_server = NULL;

#ifdef QOG_SINGLE_PLAYER
// QuakeOG, one receive buffer per direction instead of two 8 KB buffers in
// every socket. The client side holds one reliable message (at most
// MAX_MSGLEN, the size of client_t.message) plus queued datagrams. The server
// side only gets cls.message (1 KB) and movement datagrams. Overflow behaves
// as in id's code, a reliable message stops with Sys_Error, a datagram is
// dropped. Measured peaks over every shareware level and demo, 3844 bytes
// to the client, 40 to the server.
#ifndef QOG_LOOP_CLIENT_RX
#define QOG_LOOP_CLIENT_RX	NET_MAXMESSAGE
#endif
#ifndef QOG_LOOP_SERVER_RX
#define QOG_LOOP_SERVER_RX	NET_MAXMESSAGE
#endif
// 4 spare bytes, id's datagram check counts a 3-byte header but writes 4
#ifdef QOG_LOOP_ONINIT
// QuakeOG, on the hunk from Loop_Init, so a host without networking pays nothing
static byte	*loop_client_rx;
static byte	*loop_server_rx;
#else
static byte	loop_client_rx[QOG_LOOP_CLIENT_RX + 4];
static byte	loop_server_rx[QOG_LOOP_SERVER_RX + 4];
#endif
#define LOOP_RXSIZE(s)	((s)->receiveMessageSize)
#else
#define LOOP_RXSIZE(s)	NET_MAXMESSAGE
#endif

#ifdef QOG_LOOP_INPLACE
#ifndef QOG_SINGLE_PLAYER
#error QOG_LOOP_INPLACE needs QOG_SINGLE_PLAYER
#endif
// QuakeOG, net_message is read where the message lies in the receive
// buffer, so it needs no 6.5 KB buffer of its own. id moved the rest of the
// buffer down as soon as it copied a message out. Here that move waits until
// the message has been parsed, the next read of the buffer or the next send
// into it does it first, so every check and every byte stored is as in id's
// code. Demos are read into the client buffer, which is idle while one
// plays (Loop_DemoBuffer).
static int	loop_pending[2];	// delivered bytes still at the front of loop_client_rx, loop_server_rx

static int *Loop_Pending (qsocket_t *sock)
{
	return &loop_pending[sock->receiveMessage == loop_client_rx ? 0 : 1];
}

static void Loop_Consume (qsocket_t *sock)
{
	int		*pending = Loop_Pending (sock);
	int		length = *pending;

	if (!length)
		return;
	*pending = 0;
	sock->receiveMessageLength -= length;
	if (sock->receiveMessageLength)
		Q_memcpy(sock->receiveMessage, &sock->receiveMessage[length], sock->receiveMessageLength);
}

byte *Loop_DemoBuffer (void)
{
	if (loop_client)
		Sys_Error ("Loop_DemoBuffer: demo played while connected");
	return loop_client_rx;
}
#endif

int Loop_Init (void)
{
	if (cls.state == ca_dedicated)
		return -1;
#ifdef QOG_LOOP_ONINIT
	loop_client_rx = Hunk_AllocName (QOG_LOOP_CLIENT_RX + 4, "loop_crx");
	loop_server_rx = Hunk_AllocName (QOG_LOOP_SERVER_RX + 4, "loop_srx");
#endif
	return 0;
}


void Loop_Shutdown (void)
{
}


void Loop_Listen (qboolean state)
{
}


void Loop_SearchForHosts (qboolean xmit)
{
	if (!sv.active)
		return;

	hostCacheCount = 1;
	if (Q_strcmp(hostname.string, "UNNAMED") == 0)
		Q_strcpy(hostcache[0].name, "local");
	else
		Q_strcpy(hostcache[0].name, hostname.string);
	Q_strcpy(hostcache[0].map, sv.name);
	hostcache[0].users = net_activeconnections;
	hostcache[0].maxusers = svs.maxclients;
	hostcache[0].driver = net_driverlevel;
	Q_strcpy(hostcache[0].cname, "local");
}


qsocket_t *Loop_Connect (char *host)
{
	if (Q_strcmp(host,"local") != 0)
		return NULL;
	
	localconnectpending = true;

	if (!loop_client)
	{
		if ((loop_client = NET_NewQSocket ()) == NULL)
		{
			Con_Printf("Loop_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy (loop_client->address, "localhost");
#ifdef QOG_SINGLE_PLAYER
		loop_client->receiveMessage = loop_client_rx;
		loop_client->receiveMessageSize = QOG_LOOP_CLIENT_RX;
#endif
	}
	loop_client->receiveMessageLength = 0;
	loop_client->sendMessageLength = 0;
	loop_client->canSend = true;
#ifdef QOG_LOOP_INPLACE
	loop_pending[0] = 0;
#endif

	if (!loop_server)
	{
		if ((loop_server = NET_NewQSocket ()) == NULL)
		{
			Con_Printf("Loop_Connect: no qsocket available\n");
			return NULL;
		}
		Q_strcpy (loop_server->address, "LOCAL");
#ifdef QOG_SINGLE_PLAYER
		loop_server->receiveMessage = loop_server_rx;
		loop_server->receiveMessageSize = QOG_LOOP_SERVER_RX;
#endif
	}
	loop_server->receiveMessageLength = 0;
	loop_server->sendMessageLength = 0;
	loop_server->canSend = true;
#ifdef QOG_LOOP_INPLACE
	loop_pending[1] = 0;
#endif

	loop_client->driverdata = (void *)loop_server;
	loop_server->driverdata = (void *)loop_client;
	
	return loop_client;	
}


qsocket_t *Loop_CheckNewConnections (void)
{
	if (!localconnectpending)
		return NULL;

	localconnectpending = false;
	loop_server->sendMessageLength = 0;
	loop_server->receiveMessageLength = 0;
	loop_server->canSend = true;
	loop_client->sendMessageLength = 0;
	loop_client->receiveMessageLength = 0;
	loop_client->canSend = true;
#ifdef QOG_LOOP_INPLACE
	loop_pending[0] = loop_pending[1] = 0;
#endif
	return loop_server;
}


static int IntAlign(int value)
{
	return (value + (sizeof(int) - 1)) & (~(sizeof(int) - 1));
}


int Loop_GetMessage (qsocket_t *sock)
{
	int		ret;
	int		length;

#ifdef QOG_LOOP_INPLACE
	Loop_Consume (sock);
#endif
	if (sock->receiveMessageLength == 0)
		return 0;

	ret = sock->receiveMessage[0];
	length = sock->receiveMessage[1] + (sock->receiveMessage[2] << 8);
	// alignment byte skipped here
#ifdef QOG_LOOP_INPLACE
	net_message.data = &sock->receiveMessage[4];	// QuakeOG, in place, see above
	net_message.maxsize = net_message.cursize = length;
	*Loop_Pending (sock) = IntAlign(length + 4);
#else
	SZ_Clear (&net_message);
	SZ_Write (&net_message, &sock->receiveMessage[4], length);

	length = IntAlign(length + 4);
	sock->receiveMessageLength -= length;

	if (sock->receiveMessageLength)
		Q_memcpy(sock->receiveMessage, &sock->receiveMessage[length], sock->receiveMessageLength);
#endif

	if (sock->driverdata && ret == 1)
		((qsocket_t *)sock->driverdata)->canSend = true;

	return ret;
}


int Loop_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	byte *buffer;
	int  *bufferLength;

	if (!sock->driverdata)
		return -1;
#ifdef QOG_LOOP_INPLACE
	Loop_Consume ((qsocket_t *)sock->driverdata);
#endif

	bufferLength = &((qsocket_t *)sock->driverdata)->receiveMessageLength;

	if ((*bufferLength + data->cursize + 4) > LOOP_RXSIZE((qsocket_t *)sock->driverdata))	// QuakeOG, was NET_MAXMESSAGE
		Sys_Error("Loop_SendMessage: overflow\n");

	buffer = ((qsocket_t *)sock->driverdata)->receiveMessage + *bufferLength;

	// message type
	*buffer++ = 1;

	// length
	*buffer++ = data->cursize & 0xff;
	*buffer++ = data->cursize >> 8;

	// align
	buffer++;

	// message
	Q_memcpy(buffer, data->data, data->cursize);
	*bufferLength = IntAlign(*bufferLength + data->cursize + 4);

	sock->canSend = false;
	return 1;
}


int Loop_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	byte *buffer;
	int  *bufferLength;

	if (!sock->driverdata)
		return -1;
#ifdef QOG_LOOP_INPLACE
	Loop_Consume ((qsocket_t *)sock->driverdata);
#endif

	bufferLength = &((qsocket_t *)sock->driverdata)->receiveMessageLength;

	if ((*bufferLength + data->cursize + sizeof(byte) + sizeof(short)) > LOOP_RXSIZE((qsocket_t *)sock->driverdata))	// QuakeOG, was NET_MAXMESSAGE
		return 0;

	buffer = ((qsocket_t *)sock->driverdata)->receiveMessage + *bufferLength;

	// message type
	*buffer++ = 2;

	// length
	*buffer++ = data->cursize & 0xff;
	*buffer++ = data->cursize >> 8;

	// align
	buffer++;

	// message
	Q_memcpy(buffer, data->data, data->cursize);
	*bufferLength = IntAlign(*bufferLength + data->cursize + 4);
	return 1;
}


qboolean Loop_CanSendMessage (qsocket_t *sock)
{
	if (!sock->driverdata)
		return false;
	return sock->canSend;
}


qboolean Loop_CanSendUnreliableMessage (qsocket_t *sock)
{
	return true;
}


void Loop_Close (qsocket_t *sock)
{
	if (sock->driverdata)
		((qsocket_t *)sock->driverdata)->driverdata = NULL;
	sock->receiveMessageLength = 0;
	sock->sendMessageLength = 0;
	sock->canSend = true;
#ifdef QOG_LOOP_INPLACE
	*Loop_Pending (sock) = 0;
#endif
	if (sock == loop_client)
		loop_client = NULL;
	else
		loop_server = NULL;
}
