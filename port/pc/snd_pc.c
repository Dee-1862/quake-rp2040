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
/* Windows sound output for the PC build: id's four SNDDMA_* calls over a
 * looping waveOut ring, following the wave path of id's snd_win.c.
 *
 * Mixes at 11025 Hz, the rate every Quake sound is stored at, so sounds are
 * never resampled and can be played in place from flash (engine/snd_mem.c),
 * exactly as the board will do through I2S. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include "quakedef.h"

#define WAV_BUFFERS      64
#define WAV_MASK         (WAV_BUFFERS - 1)
#define WAV_BUFFER_SIZE  0x0400
#define SND_SPEED        11025

extern volatile dma_t sn;   /* snd_dma.c */

static HWAVEOUT s_wave;
static WAVEHDR  s_hdr[WAV_BUFFERS];
static byte    *s_buffer;
static int      s_sent, s_completed;

qboolean SNDDMA_Init(void) {
    WAVEFORMATEX fmt;
    int i;

    memset((void *)&sn, 0, sizeof sn);
    shm = &sn;
    shm->channels = 2;
    shm->samplebits = 16;
    shm->speed = SND_SPEED;

    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = (WORD)shm->channels;
    fmt.wBitsPerSample = (WORD)shm->samplebits;
    fmt.nSamplesPerSec = shm->speed;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    if (waveOutOpen(&s_wave, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        Con_Printf("SNDDMA_Init: no wave output device, sound off\n");
        shm = NULL;
        return false;
    }

    s_buffer = calloc(WAV_BUFFERS, WAV_BUFFER_SIZE);
    if (!s_buffer) { waveOutClose(s_wave); shm = NULL; return false; }
    for (i = 0; i < WAV_BUFFERS; i++) {
        memset(&s_hdr[i], 0, sizeof s_hdr[i]);
        s_hdr[i].dwBufferLength = WAV_BUFFER_SIZE;
        s_hdr[i].lpData = (LPSTR)s_buffer + i * WAV_BUFFER_SIZE;
        waveOutPrepareHeader(s_wave, &s_hdr[i], sizeof s_hdr[i]);
    }
    shm->soundalive = true;
    shm->splitbuffer = false;
    shm->samples = WAV_BUFFERS * WAV_BUFFER_SIZE / (shm->samplebits / 8);
    shm->samplepos = 0;
    shm->submission_chunk = 1;
    shm->buffer = s_buffer;
    s_sent = s_completed = 0;
    return true;
}

int SNDDMA_GetDMAPos(void) {
    int s = s_sent * WAV_BUFFER_SIZE / (shm->samplebits / 8);
    return s & (shm->samples - 1);
}

void SNDDMA_Submit(void) {
    if (!s_wave) return;
    /* retire the blocks that have finished playing */
    while (s_completed != s_sent && (s_hdr[s_completed & WAV_MASK].dwFlags & WHDR_DONE))
        s_completed++;
    /* keep a few blocks queued ahead */
    while (s_sent - s_completed < 4) {
        WAVEHDR *h = &s_hdr[s_sent & WAV_MASK];
        s_sent++;
        waveOutWrite(s_wave, h, sizeof *h);
    }
}

void SNDDMA_Shutdown(void) {
    int i;
    if (!s_wave) return;
    waveOutReset(s_wave);
    for (i = 0; i < WAV_BUFFERS; i++) waveOutUnprepareHeader(s_wave, &s_hdr[i], sizeof s_hdr[i]);
    waveOutClose(s_wave);
    s_wave = NULL;
    free(s_buffer);
    s_buffer = NULL;
    shm = NULL;
}
