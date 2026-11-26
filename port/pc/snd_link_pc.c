/* QuakeOG, the split client's sound output on the PC
 * (QOG_SND_REMOTE, port/common/qog_snd.h). Never opens an audio device.
 *
 * It stands for the board's I2S output in tests, the play position advances
 * by game time after every update the server half mixed, exactly as
 * snd_pc.c's silent mode (-hidden) advances id's DMA position after every
 * SNDDMA_Submit, so the server half mixes what the single-process game
 * mixes for the same script. The samples that arrive are hashed in arrival
 * order (qog_sndrx), to compare with the server half's qog_sndhash.
 */
#include "quakedef.h"

#ifdef QOG_SND_REMOTE
#include "qog_snd.h"
#include "net_link.h"

static double   s_pos;          /* samples played, as snd_pc.c's s_silentpos with one channel */
static unsigned s_hash = 0x811C9DC5u, s_count, s_clears;

static void cmd_sndrx(void) {
    Con_Printf("qog_sndrx: %u samples %08x, %u clears, play position %u, %u queued (%u starts)\n",
               s_count, s_hash, s_clears, (unsigned)(int)s_pos, link_sndasync, link_sndasync_start);
}

int qog_snd_out_init(void) {
    static qboolean registered;
    if (!registered) { Cmd_AddCommand("qog_sndrx", cmd_sndrx); registered = true; }
    s_pos = 0;
    return 1;
}

void qog_snd_out_shutdown(void) {}

unsigned qog_snd_out_samples(void) {
    return (unsigned)(int)s_pos;
}

void qog_snd_out_submitted(void) {
    s_pos += host_frametime * QOG_SND_RATE;
}

void qog_snd_out_write(unsigned t, const unsigned char *pcm, int n) {
    (void)t;
    for (int i = 0; i < n; i++) {
        s_hash = (s_hash ^ pcm[i]) * 16777619u;   /* snd_mix.c's S_QogHash of one byte */
        for (int k = 0; k < 3; k++) s_hash = s_hash * 16777619u;
        s_count++;
    }
}

void qog_snd_out_clear(void) { s_clears++; }

void qog_snd_out_idle(void) {}

#endif
