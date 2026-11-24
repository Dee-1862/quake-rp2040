/* QuakeOG (Agent W): id's SNDDMA calls on the split game's server half
 * (QOG_SND_HOST), where the mixer runs for the client half (qog_snd.h).
 *
 * The "DMA buffer" is a ring of QOG_SND_RING 8-bit mono samples, as id's
 * 8-bit output writes them. Its play position is the client's output's, sent
 * with every update (net_link.c), and counts samples without wrapping, so
 * GetSoundtime takes id's own path for such outputs (QOG_SND_GETSAMPLES).
 * Submit sends what the mixer wrote since the last one to the client.
 *
 * On the main chip the ring lives in the SCRATCH_Y bank (QOG_SND_RING_SCRATCH):
 * the server runs on its own stack array, so the bank holds only the boot
 * stack, which main() leaves for good before the game starts.
 */
#include "quakedef.h"
#include "net_link.h"
#include "qog_snd.h"

#ifdef QOG_SND_HOST

#ifdef QOG_SND_RING_SCRATCH
#define SND_RING_PLACE __attribute__((section(".scratch_y.qog_snd")))
#else
#define SND_RING_PLACE
#endif

extern int	soundtime;		/* snd_dma.c */

static unsigned char	s_ring[QOG_SND_RING] SND_RING_PLACE;
static unsigned		s_pos;		/* the client's play position */
static int			s_sent;		/* the mixer's time up to which samples were sent */
int					qog_snd_host_submitted;

void qog_snd_host_setpos(unsigned samples)
{
	s_pos = samples;
}

qboolean SNDDMA_Init(void)
{
	memset((void *)&sn, 0, sizeof sn);
	shm = &sn;
	shm->channels = 1;
	shm->samplebits = 8;
	shm->speed = QOG_SND_RATE;
	shm->samples = QOG_SND_RING;
	shm->submission_chunk = 1;
	shm->soundalive = true;
	shm->gamealive = true;
	shm->buffer = s_ring;
	s_pos = 0;
	s_sent = 0;
	return true;
}

int SNDDMA_GetDMAPos(void)
{
	return (int)(s_pos & (QOG_SND_RING - 1));
}

int SNDDMA_GetSamples(void)
{
	return (int)s_pos;
}

void SNDDMA_Submit(void)
{
	int		from = s_sent, to = paintedtime, at, an;

	qog_snd_host_submitted = 1;
	if (from < soundtime)
		from = soundtime;		/* played already */
	if (to - from > QOG_SND_RING)
		from = to - QOG_SND_RING;
	s_sent = to;
	if (from >= to)
		return;
	at = from & (QOG_SND_RING - 1);
	an = QOG_SND_RING - at;
	if (an > to - from)
		an = to - from;
	Link_ServerSoundPcm((unsigned)from, s_ring + at, an, s_ring, to - from - an);
}

void SNDDMA_Shutdown(void)
{
}

#endif
