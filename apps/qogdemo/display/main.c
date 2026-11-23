/* QuakeOG demo playback on the display CPU.
 *
 * id's client and renderer play the recorded demos from the board's data
 * image (tools/mkboard.py --demos) with no server, QuakeC, console or sound
 * (port/common/qog_demo.c): monsters, weapons, particles, explosions and the
 * status bar, as the game draws them. Each frame goes to the LCD through
 * qog_video (core 1 scanout), and every 2 s the USB console gets
 *
 *   [qogdemo] demo1 t 23.4  fps 6.1  ms frame 150.2 wait 12.0  free 2100  hunk 80000/81920
 *
 * the demo playing and its time (cl.time), frames per second over the last
 * 2 s, the mean time per frame (qog_demo_frame: messages, R_RenderView, the
 * status bar and VID_Update) and of it the time spent waiting for the
 * previous scanout, the RAM no one has claimed (heap left), and the hunk in
 * use of its size. The frame time is real time, which id clamps to 0.1 s,
 * so below 10 fps a demo plays slower than recorded.
 *
 * Gray skips to the next demo. Red stays the BSP's 6 s power-off.
 *
 * BSP rules (docs/lessons.md): no watchdog on this CPU, fwog_power_poll
 * every loop iteration (also between demo messages is not needed: a frame
 * is well under a second), backlight on (qog_video_init does it), scanout
 * on core 1 through shared memory.
 *
 * Stack: id's R_EdgeDrawing keeps its surface table on the stack, far more
 * than the SDK's 4 KB scratch stack. main() moves the stack into a
 * QOG_DEMO_STACK_KB array in main RAM before anything else runs, so the
 * link counts it. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "quakedef.h"
#include "qog_demo.h"
#include "qog_video.h"
#include "pico/stdlib.h"
#include "fwog_display.h"

#ifndef QOG_DEMO_STACK_KB
#define QOG_DEMO_STACK_KB 34
#endif
#ifndef QOG_DEMO_ZONE_KB
#define QOG_DEMO_ZONE_KB "4"
#endif

void *qog_rp2040_hunk (int *size);		/* port/rp2040/sys_rp2040.c */
extern int	hunk_low_used, hunk_high_used, hunk_size;
extern char	__HeapLimit;				/* Pico SDK linker script */

static uint32_t	demo_stack[QOG_DEMO_STACK_KB * 256] __attribute__((aligned(8)));

static unsigned free_ram (void)
{
	char	*brk = sbrk (0);

	return brk == (char *)-1 ? 0 : (unsigned)(&__HeapLimit - brk);
}

/* The lowest stack address written so far: the array starts filled with a
   pattern, and the first word that lost it is the deepest the stack went. */
#define STACK_FILL	0x51F0A55Au

static unsigned stack_used (void)
{
	unsigned	i;

	for (i = 0 ; i < sizeof demo_stack / sizeof demo_stack[0] ; i++)
		if (demo_stack[i] != STACK_FILL)
			break;
	return (unsigned)(sizeof demo_stack - i * sizeof demo_stack[0]);
}

static void __attribute__((noreturn)) demo_main (void)
{
	static char		*argv[] = { "qogdemo", "-nosound", "-zone", QOG_DEMO_ZONE_KB };
	static const char	*demos[] = { "demo1", "demo2", "demo3" };
	quakeparms_t	parms;
	uint64_t		last, prev, frame_us = 0, wait_us = 0;
	int				frames = 0;

	memset (&parms, 0, sizeof parms);
	COM_InitArgv (4, argv);
	parms.argc = com_argc;
	parms.argv = com_argv;
	parms.basedir = ".";
	parms.membase = qog_rp2040_hunk (&parms.memsize);

	qog_demo_init_host (&parms);		/* VID_Init starts the LCD and core 1 */
	DIAG ("[qogdemo] engine started, hunk %d/%d, free %u\n", hunk_low_used + hunk_high_used,
		  hunk_size, free_ram ());
	qog_demo_start (demos, 3, 0);

	last = prev = time_us_64 ();
	for (;;)
	{
		uint64_t		a, b;
		fwog_power_t	pw;
		int				drawn;

		pw = fwog_power_poll (to_ms_since_boot (get_absolute_time ()));	/* every iteration, BSP rule */
		if (pw.buttons.pressed & FWOG_BTN_BIT (FWOG_BTN_GRAY))
			qog_demo_next ();

		a = time_us_64 ();
		drawn = qog_demo_frame ((a - prev) * 1e-6, false);
		prev = a;
		b = time_us_64 ();
		if (drawn > 0)
		{
			frame_us += b - a;
			wait_us += qog_video_stats ().last_wait_us;
			frames++;
		}

		if (b - last >= 2000000)
		{
			float	secs = (b - last) * 1e-6f;
			int		n = frames ? frames : 1;

			DIAG ("[qogdemo] %s t %.1f  fps %.1f  ms frame %.1f wait %.1f  free %u  stack %u  hunk %d/%d\n",
				  qog_demo_name ()[0] ? qog_demo_name () : "-", cl.time, frames / secs,
				  frame_us * 1e-3f / n, wait_us * 1e-3f / n, free_ram (), stack_used (),
				  hunk_low_used + hunk_high_used, hunk_size);
			frames = 0;
			frame_us = wait_us = 0;
			last = b;
		}
	}
}

/* Moves the stack pointer to the top of demo_stack and jumps to demo_main.
   Thread mode runs on MSP, so interrupts use the same, larger stack. */
static void __attribute__((naked, noreturn, noinline)) run_on_stack (void *top, void (*fn) (void))
{
	__asm volatile ("mov sp, r0\n\tbx r1\n");
}

int main (void)
{
	unsigned	i;

	board_init ();
	for (i = 0 ; i < sizeof demo_stack / sizeof demo_stack[0] ; i++)	/* main runs on the SDK's stack */
		demo_stack[i] = STACK_FILL;
	run_on_stack (demo_stack + sizeof demo_stack / sizeof demo_stack[0], demo_main);
}
