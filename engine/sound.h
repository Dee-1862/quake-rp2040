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
// sound.h -- client sound i/o functions

#ifndef __SOUND__
#define __SOUND__

#define DEFAULT_SOUND_PACKET_VOLUME 255
#define DEFAULT_SOUND_PACKET_ATTENUATION 1.0

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct
{
	int left;
	int right;
} portable_samplepair_t;

typedef struct sfx_s
{
#ifdef QOG_SFX_NAMEPTR
	char	*name;			// QuakeOG: the name in the mapped pack's directory, or a small pool (snd_dma.c)
#else
	char 	name[MAX_QPATH];
#endif
	cache_user_t	cache;
} sfx_t;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct
{
	int 	length;
	int 	loopstart;
	int 	speed;
	int 	width;
	int 	stereo;
	byte	*flash;			// QuakeOG: unsigned 8-bit samples read in place from flash, or NULL
	byte	data[1];		// variable sized
} sfxcache_t;

typedef struct
{
	qboolean		gamealive;
	qboolean		soundalive;
	qboolean		splitbuffer;
	int				channels;
	int				samples;				// mono samples in buffer
	int				submission_chunk;		// don't mix less than this #
	int				samplepos;				// in mono samples
	int				samplebits;
	int				speed;
	unsigned char	*buffer;
} dma_t;

// !!! if this is changed, it much be changed in asm_i386.h too !!!
typedef struct
{
	sfx_t	*sfx;			// sfx number
	int		leftvol;		// 0-255 volume
	int		rightvol;		// 0-255 volume
	int		end;			// end time in global paintsamples
	int 	pos;			// sample position in sfx
	int		looping;		// where to loop, -1 = no looping
	int		entnum;			// to allow overriding a specific sound
	int		entchannel;		//
	vec3_t	origin;			// origin of sound effect
	vec_t	dist_mult;		// distance multiplier (attenuation/clipK)
	int		master_vol;		// 0-255 master volume
} channel_t;

typedef struct
{
	int		rate;
	int		width;
	int		channels;
	int		loopstart;
	int		samples;
	int		dataofs;		// chunk starts this many bytes from file start
} wavinfo_t;

void S_Init (void);
void S_Startup (void);
void S_Shutdown (void);
void S_StartSound (int entnum, int entchannel, sfx_t *sfx, vec3_t origin, float fvol,  float attenuation);
void S_StaticSound (sfx_t *sfx, vec3_t origin, float vol, float attenuation);
void S_StopSound (int entnum, int entchannel);
void S_StopAllSounds(qboolean clear);
void S_ClearBuffer (void);
void S_Update (vec3_t origin, vec3_t v_forward, vec3_t v_right, vec3_t v_up);
void S_ExtraUpdate (void);

sfx_t *S_PrecacheSound (char *sample);
void S_TouchSound (char *sample);
void S_ClearPrecache (void);
void S_BeginPrecaching (void);
void S_EndPrecaching (void);
void S_PaintChannels(int endtime);
void S_InitPaintChannels (void);

// picks a channel based on priorities, empty slots, number of channels
channel_t *SND_PickChannel(int entnum, int entchannel);

// spatializes a channel
void SND_Spatialize(channel_t *ch);

// initializes cycling through a DMA buffer and returns information on it
qboolean SNDDMA_Init(void);

// gets the current DMA position
int SNDDMA_GetDMAPos(void);

#ifdef QOG_SND_GETSAMPLES
// QuakeOG: the samples played so far, never wrapping (id's __sun__ path in
// GetSoundtime), for an output that counts them itself
int SNDDMA_GetSamples(void);
#endif

#ifdef QOG_SND_HOST
// QuakeOG (Agent W): the split game mixes on the server half for the client
// half (engine/net_link.c). What id's mixer read from the client's state
// comes with the client's sound calls instead.
extern int		snd_viewentity;			// cl.viewentity
extern int		snd_ambientstate;		// 0 no cl.worldmodel, 1 no leaf, 2 the leaf's levels below
extern byte		snd_ambientlevels[4];	// the listener's leaf's ambient_sound_level
#endif

// shutdown the DMA xfer.
void SNDDMA_Shutdown(void);

// ====================================================================
// User-setable variables
// ====================================================================

#ifdef QOG_MAX_CHANNELS
#define	MAX_CHANNELS			QOG_MAX_CHANNELS	// QuakeOG: 12 plus the static sounds a map may have
#else
#define	MAX_CHANNELS			128
#endif
#define	MAX_DYNAMIC_CHANNELS	8

#ifdef QOG_SND_HOSTMEM
// QuakeOG (Agent W): the paint buffer (512 pairs, only used while
// S_PaintChannels runs) and known_sfx (QOG_MAX_SFX entries) come from the
// port's memory, not the hunk (port/common/qog_snd_host.c)
void	*qog_snd_host_paintbuffer (void);
sfx_t	*qog_snd_host_sfxtable (void);
#endif

#ifdef QOG_SND_HASH
// QuakeOG (Agent W): a hash of every painted sample pair and every byte or
// word written to the DMA buffer, in time order (qog_sndhash), to compare mixers
extern unsigned	qog_snd_painthash, qog_snd_paintcount, qog_snd_outhash, qog_snd_outcount;
#endif


#ifdef QOG_SND_ONINIT
// QuakeOG: allocated by S_Init, so a host that never starts sound pays nothing
extern	channel_t   *channels;
#else
extern	channel_t   channels[MAX_CHANNELS];
#endif
// 0 to MAX_DYNAMIC_CHANNELS-1	= normal entity sounds
// MAX_DYNAMIC_CHANNELS to MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS -1 = water, etc
// MAX_DYNAMIC_CHANNELS + NUM_AMBIENTS to total_channels = static sounds

extern	int			total_channels;

//
// Fake dma is a synchronous faking of the DMA progress used for
// isolating performance in the renderer.  The fakedma_updates is
// number of times S_Update() is called per second.
//

extern qboolean 		fakedma;
extern int 			fakedma_updates;
extern int		paintedtime;
extern vec3_t listener_origin;
extern vec3_t listener_forward;
extern vec3_t listener_right;
extern vec3_t listener_up;
extern volatile dma_t *shm;
extern volatile dma_t sn;
extern vec_t sound_nominal_clip_dist;

extern	cvar_t loadas8bit;
extern	cvar_t bgmvolume;
extern	cvar_t volume;

extern qboolean	snd_initialized;

extern int		snd_blocked;

void S_LocalSound (char *s);
sfxcache_t *S_LoadSound (sfx_t *s);

wavinfo_t GetWavinfo (char *name, byte *wav, int wavlength);

void SND_InitScaletable (void);
void SNDDMA_Submit(void);

void S_AmbientOff (void);
void S_AmbientOn (void);

#endif
