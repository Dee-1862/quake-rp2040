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
// snd_link.c -- QuakeOG (Agent W): the split client's sound (QOG_SND_REMOTE)
//
// The split game's client half (the display chip) plays the sound, but id's
// mixer (snd_dma.c, snd_mix.c, snd_mem.c) runs on the server half (the main
// chip, QOG_SND_HOST), which has the room for its channels and the sounds in
// its flash. Each S_ call here is the same call there, made at the same
// point of the client's frame (engine/net_link.c, LK_SND):
//
//  - Calls with an answer, or whose effect the client must see before it goes
//    on, are exchanges: S_PrecacheSound (the sound's number), S_StartSound
//    and S_LocalSound (the random number id draws when an identical sound
//    started this frame comes back with the server's state), S_Update and
//    S_ExtraUpdate (they mix, and the samples come back before the answer).
//  - The rest are sent in order without waiting.
//
// What id's mixer read from client state is sent with the call: the view
// entity, and with S_Update the listener's leaf's ambient levels. The play
// position is the client's output's (qog_snd_out_samples), so the server
// half mixes exactly what id's mixer mixes for the same calls at the same
// positions. A sound is a number on the server half, which the client holds
// as an sfx_t pointer it never reads.

#include "quakedef.h"

#ifdef QOG_SND_REMOTE

#include "net_link.h"
#include "qog_snd.h"

// the archived sound cvars the client's menus and config.cfg use; setting
// one here sets the server half's too (Link_CvarChanged)
cvar_t	bgmvolume = {"bgmvolume", "1", true};
cvar_t	volume = {"volume", "0.7", true};
cvar_t	_snd_mixahead = {"_snd_mixahead", "0.1", true};
// QuakeOG: the background (ambient) level, mixed on main. Registered here
// too so the board menu can set it and the cvar sync carries it over.
cvar_t	ambient_level = {"ambient_level", "0.3", true};
cvar_t	snd_noextraupdate = {"snd_noextraupdate", "0"};	// read here too, see S_ExtraUpdate

static qboolean	snd_on;

static sfx_t *S_LinkHandle (int n)
{
	return n < 0 ? NULL : (sfx_t *)(size_t)(n + 1);
}

static int S_LinkNumber (sfx_t *sfx)
{
	return sfx ? (int)(size_t)sfx - 1 : -1;
}

static void S_Put32 (byte *b, int v)
{
	b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}

static void S_PutF (byte *b, float f)
{
	memcpy (b, &f, 4);
}

static void S_PutV (byte *b, vec3_t v)
{
	memcpy (b, v, 12);
}

// an operation that carries a name
static int S_LinkName (int op, int viewent, char *name)
{
	byte	b[1 + 4 + MAX_QPATH];
	int		n = 1, len = Q_strlen (name);

	if (len >= MAX_QPATH)
		len = MAX_QPATH - 1;
	b[0] = op;
	if ((op & ~LKS_REPLY) == LKS_LOCAL)
	{
		S_Put32 (b + 1, viewent);
		n = 5;
	}
	memcpy (b + n, name, len);
	b[n + len] = 0;
	return Link_Sound (b, n + len + 1);
}

static void S_LinkOp (int op)
{
	byte	b[1];

	b[0] = op;
	Link_Sound (b, 1);
}

void S_Init (void)
{
	Con_Printf ("\nSound Initialization\n");
	if (COM_CheckParm ("-nosound"))
		return;
	Cvar_RegisterVariable (&volume);
	Cvar_RegisterVariable (&bgmvolume);
	Cvar_RegisterVariable (&_snd_mixahead);
	Cvar_RegisterVariable (&snd_noextraupdate);
	Cvar_RegisterVariable (&ambient_level);
	if (!qog_snd_out_init ())
	{
		Con_Printf ("S_Startup: SNDDMA_Init failed.\n");
		return;
	}
	snd_on = true;
	Con_Printf ("Sound sampling rate: %i\n", QOG_SND_RATE);
}

void S_Shutdown (void)
{
	if (!snd_on)
		return;
	snd_on = false;
	qog_snd_out_shutdown ();
}

sfx_t *S_PrecacheSound (char *name)
{
	if (!snd_on)
		return NULL;
	return S_LinkHandle (S_LinkName (LKS_PRECACHE | LKS_REPLY, 0, name));
}

void S_TouchSound (char *name)
{
	if (!snd_on)
		return;
	S_LinkName (LKS_TOUCH, 0, name);
}

void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol, float attenuation)
{
	byte	b[37];

	if (!snd_on || !sfx)
		return;
	b[0] = LKS_START | LKS_REPLY;
	S_Put32 (b + 1, entnum);
	S_Put32 (b + 5, entchannel);
	S_Put32 (b + 9, S_LinkNumber (sfx));
	S_PutV (b + 13, origin);
	S_PutF (b + 25, fvol);
	S_PutF (b + 29, attenuation);
	S_Put32 (b + 33, cl.viewentity);
	Link_Sound (b, sizeof(b));
}

void S_LocalSound (char *sound)
{
	if (!snd_on)
		return;
	S_LinkName (LKS_LOCAL | LKS_REPLY, cl.viewentity, sound);
}

void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation)
{
	byte	b[29];

	if (!snd_on || !sfx)
		return;
	b[0] = LKS_STATIC;
	S_Put32 (b + 1, S_LinkNumber (sfx));
	S_PutV (b + 5, origin);
	S_PutF (b + 17, vol);
	S_PutF (b + 21, attenuation);
	S_Put32 (b + 25, cl.viewentity);
	Link_Sound (b, sizeof(b));
}

void S_StopSound (int entnum, int entchannel)
{
	byte	b[9];

	if (!snd_on)
		return;
	b[0] = LKS_STOP;
	S_Put32 (b + 1, entnum);
	S_Put32 (b + 5, entchannel);
	Link_Sound (b, sizeof(b));
}

void S_StopAllSounds (qboolean clear)
{
	byte	b[2];

	if (!snd_on)
		return;
	if (clear)
		qog_snd_out_clear ();	// what S_ClearBuffer does to id's DMA buffer
	b[0] = LKS_STOPALL;
	b[1] = clear;
	Link_Sound (b, sizeof(b));
}

void S_StopAllSoundsC (void)
{
	S_StopAllSounds (true);
}

void S_ClearBuffer (void)
{
	if (!snd_on)
		return;
	qog_snd_out_clear ();
	S_LinkOp (LKS_CLEAR);
}

void S_Update (vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	byte	b[62];
	mleaf_t	*l;
	int		n;

	if (!snd_on)
		return;
	b[0] = LKS_UPDATE | LKS_REPLY;
	S_Put32 (b + 1, qog_snd_out_samples ());
	S_Put32 (b + 5, cl.viewentity);
	// S_UpdateAmbientSounds' leaf, looked up here where the map is
	memset (b + 9, 0, 5);
	if (cl.worldmodel)
	{
		l = Mod_PointInLeaf (origin, cl.worldmodel);
		b[9] = l ? 2 : 1;
		if (l)
			memcpy (b + 10, l->ambient_sound_level, 4);
	}
	S_PutV (b + 14, origin);
	S_PutV (b + 26, forward);
	S_PutV (b + 38, right);
	S_PutV (b + 50, up);
	n = Link_Sound (b, sizeof(b));
	if (n == 1 || n == LINK_SNDQUEUED)		// queued, it mixes after the exchange
		qog_snd_out_submitted ();
}

void S_ExtraUpdate (void)
{
	byte	b[5];
	int		n;

	if (!snd_on)
		return;
	b[0] = LKS_EXTRA | LKS_REPLY;
	S_Put32 (b + 1, qog_snd_out_samples ());
	n = Link_Sound (b, sizeof(b));
	if (n == 1 || (n == LINK_SNDQUEUED && !snd_noextraupdate.value))
		qog_snd_out_submitted ();
}

void S_AmbientOff (void)
{
	if (snd_on)
		S_LinkOp (LKS_AMBOFF);
}

void S_AmbientOn (void)
{
	if (snd_on)
		S_LinkOp (LKS_AMBON);
}

void S_ClearPrecache (void)
{
}

void S_BeginPrecaching (void)
{
}

void S_EndPrecaching (void)
{
}

#endif	// QOG_SND_REMOTE
