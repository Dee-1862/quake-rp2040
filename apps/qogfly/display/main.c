/* QuakeOG E1M1 fly-through on the display CPU.
 *
 * The engine's own renderer draws the real E1M1 from the board's data image
 * (tools/mkboard.py) with a camera moving along a path through the level
 * (port/common/qog_fly.c). No game logic, monsters or sound. Each frame goes
 * to the LCD through qog_video (core 1 scanout), and every 2 s the USB
 * console gets
 *
 *   [qogfly] fps 7.9  ms render 112.3 present 14.1  pt 12/79  free 3120  hunk 97536/102400
 *
 * fps over the last 2 s, the mean time per frame spent drawing (R_RenderView
 * plus the text) and in VID_Update (waiting for the previous scanout and
 * starting this one), the route point the camera last passed and the route
 * length, the RAM no one has claimed (heap left), and the hunk in use of its
 * size.
 *
 * BSP rules (docs/lessons.md): no watchdog on this CPU, fwog_power_poll
 * every loop iteration, backlight on (qog_video_init does it), scanout on
 * core 1 through shared memory.
 *
 * Stack: id's R_EdgeDrawing keeps its edge and surface tables on the stack
 * (about 85 KB with the current flags), far more than the SDK's 4 KB
 * scratch stack. main() moves the stack into a QOG_FLY_STACK_KB array in
 * main RAM before anything else runs, so the link counts it. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "quakedef.h"
#include "qog_fly.h"
#include "pico/stdlib.h"
#include "fwog_display.h"

#ifndef QOG_FLY_STACK_KB
#define QOG_FLY_STACK_KB 94
#endif
#ifndef QOG_FLY_MAP
#define QOG_FLY_MAP "e1m1"
#endif

void *qog_rp2040_hunk (int *size);		/* port/rp2040/sys_rp2040.c */
extern int	hunk_low_used, hunk_high_used, hunk_size;
extern char	__HeapLimit;				/* Pico SDK linker script */

static uint32_t	fly_stack[QOG_FLY_STACK_KB * 256] __attribute__((aligned(8)));

static unsigned free_ram (void)
{
	char	*brk = sbrk (0);

	return brk == (char *)-1 ? 0 : (unsigned)(&__HeapLimit - brk);
}

static void __attribute__((noreturn)) fly_main (void)
{
	static char		*argv[] = { "qogfly", "-nosound", "-zone", "3" };
	quakeparms_t	parms;
	vec3_t			feet, ang;
	uint64_t		t0, last, render_us = 0, present_us = 0;
	int				frames = 0, pt = 0;
	char			line1[40], line2[40];
	float			fps = 0;

	memset (&parms, 0, sizeof parms);
	COM_InitArgv (4, argv);
	parms.argc = com_argc;
	parms.argv = com_argv;
	parms.basedir = ".";
	parms.membase = qog_rp2040_hunk (&parms.memsize);

	qog_fly_init_host (&parms);		/* VID_Init starts the LCD and core 1 */
	qog_fly_load (QOG_FLY_MAP);
	DIAG ("[qogfly] %s loaded, %d route points, %.1f s loop, hunk %d/%d\n", QOG_FLY_MAP,
		  qog_fly_numpoints (), qog_fly_loop_time (), hunk_low_used + hunk_high_used, hunk_size);

	t0 = last = time_us_64 ();
	line1[0] = line2[0] = 0;
	for (;;)
	{
		uint64_t	a, b, c;

		fwog_power_poll (to_ms_since_boot (get_absolute_time ()));	/* every iteration, BSP rule */

		a = time_us_64 ();
		pt = qog_fly_camera ((a - t0) * 1e-6, feet, ang);
		qog_fly_render (feet, ang, (a - t0) * 1e-6);
		qog_fly_overlay (line1, line2);
		b = time_us_64 ();
		VID_Update (NULL);		/* present, then wait: one frame buffer */
		c = time_us_64 ();
		render_us += b - a;
		present_us += c - b;
		frames++;

		if (c - last >= 2000000)
		{
			float	secs = (c - last) * 1e-6f;

			fps = frames / secs;
			DIAG ("[qogfly] fps %.1f  ms render %.1f present %.1f  pt %d/%d  free %u  hunk %d/%d\n",
				  fps, render_us * 1e-3f / frames, present_us * 1e-3f / frames, pt,
				  qog_fly_numpoints (), free_ram (), hunk_low_used + hunk_high_used, hunk_size);
			snprintf (line1, sizeof line1, "E1M1 fly-through  %.1f fps", fps);
			snprintf (line2, sizeof line2, "render %.0f ms  point %d/%d", render_us * 1e-3f / frames,
					  pt, qog_fly_numpoints ());
			frames = 0;
			render_us = present_us = 0;
			last = c;
		}
	}
}

/* Moves the stack pointer to the top of fly_stack and jumps to fly_main.
   Thread mode runs on MSP, so interrupts use the same, larger stack. */
static void __attribute__((naked, noreturn, noinline)) run_on_stack (void *top, void (*fn) (void))
{
	__asm volatile ("mov sp, r0\n\tbx r1\n");
}

int main (void)
{
	board_init ();
	run_on_stack (fly_stack + sizeof fly_stack / sizeof fly_stack[0], fly_main);
}
