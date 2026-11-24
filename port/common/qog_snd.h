/* QuakeOG (Agent W): the split game's sound.
 *
 * The server half (main chip) runs id's mixer for the client half's sound
 * calls (engine/snd_link.c, engine/net_link.c) and sends the samples it
 * writes to its DMA buffer, id's 8-bit mono format at 11025 Hz, back over
 * the link. The client half (display chip) plays them: on the board through
 * the I2S amplifier (port/rp2040/snd_rp2040.c), on the PC silently, counting
 * game time as snd_pc.c's silent mode does (port/pc/snd_link_pc.c).
 *
 * Both halves keep a ring of QOG_SND_RING samples. A sample for time t sits
 * at t % QOG_SND_RING in both, and the client's play position is the time
 * id's mixer mixes ahead of (SNDDMA_GetSamples on the server half).
 */
#ifndef QOG_SND_H
#define QOG_SND_H

#define QOG_SND_RATE 11025
#ifndef QOG_SND_RING
#define QOG_SND_RING 2048       /* samples, a power of two: 186 ms */
#endif
#if QOG_SND_RING & (QOG_SND_RING - 1)
#error QOG_SND_RING must be a power of two
#endif

/* ---- the client half's output, supplied by the port ---- */
int      qog_snd_out_init(void);            /* 0: no output, sound stays off */
void     qog_snd_out_shutdown(void);
unsigned qog_snd_out_samples(void);         /* samples played so far, never wraps */
void     qog_snd_out_submitted(void);       /* the server mixed for an update (the PC's silent clock) */
void     qog_snd_out_write(unsigned t, const unsigned char *pcm, int n);  /* id's 8-bit samples from time t */
void     qog_snd_out_clear(void);           /* silence what is buffered (S_ClearBuffer) */
void     qog_snd_out_idle(void);            /* from the link's waits */

/* ---- the server half (port/common/qog_snd_host.c) ---- */
extern int qog_snd_host_submitted;          /* SNDDMA_Submit ran */
void     qog_snd_host_setpos(unsigned samples);   /* the client's play position, with each update */

#endif
