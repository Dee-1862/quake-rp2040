/* QuakeOG, id's SNDDMA calls on the split game's server half
 * (QOG_SND_HOST), where the mixer runs for the client half (qog_snd.h).
 *
 * The "DMA buffer" is a ring of QOG_SND_RING 8-bit mono samples, as id's
 * 8-bit output writes them. Its play position is the client's output's, sent
 * with every update (net_link.c), and counts samples without wrapping, so
 * GetSoundtime takes id's own path for such outputs (QOG_SND_GETSAMPLES).
 * Submit sends what the mixer wrote since the last one to the client.
 *
 * Memory (QOG_SND_HOSTMEM), the paint buffer and the ring hold samples only
 * while one S_Update_ runs (S_PaintChannels clears the paint buffer before
 * it paints, and Submit sends everything the ring got), so nothing in them
 * has to last. On the main chip (QOG_SND_BOARDMEM) they are the lowest
 * QOG_SND_SCRATCH bytes of the server's stack array, the mixer runs only
 * from the link's items, between server frames, where the stack is a few KB
 * deep, and interrupts have their own stack. known_sfx, which lasts, takes
 * the free half of the SCRATCH_Y bank (the other half is the interrupt
 * stack).
 */
#include "quakedef.h"
#include "net_link.h"
#include "qog_snd.h"

#ifdef QOG_SND_HOST

extern int	soundtime;		/* snd_dma.c */

#ifdef QOG_SND_BOARDMEM
extern unsigned char	*qog_stack_bottom;		/* port/rp2040/sys_rp2040.c */
#define s_paint	((void *)qog_stack_bottom)
#define s_ring	(qog_stack_bottom + QOG_SND_PAINT)
static sfx_t			s_sfx[QOG_MAX_SFX] __attribute__((section(".scratch_y.qog_snd")));
#else
static int				s_paintmem[QOG_SND_PAINT / sizeof(int)];
#define s_paint	((void *)s_paintmem)
static unsigned char	s_ring[QOG_SND_RING];
#ifdef QOG_SND_HOSTMEM
static sfx_t			s_sfx[QOG_MAX_SFX];
#endif
#endif

#ifdef QOG_SND_HOSTMEM
void *qog_snd_host_paintbuffer(void) { return s_paint; }
sfx_t *qog_snd_host_sfxtable(void) { return s_sfx; }
#endif

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
