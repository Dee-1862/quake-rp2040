/*
QuakeOG (Agent AO): the Turbo render mode, r_fast 2.

The 3D view is drawn at half width (160x152 for the board's 320x152 view)
with Fast mode's shortcuts, and the LCD scanout doubles each of its pixels
sideways (platform/qog_video.c), blocky like Doom's low detail. The 2D parts
(status bar, menus, console, text) stay at full width and sharp.

Frame buffer layout while a Turbo view is in it (P rows of view, W = 320):
  [0, P * W/2)         the view's pixels, row r at r * W/2 (packed)
  [P * W/2, P * W)     its one-byte z-buffer while it is drawn (else free)
  [P * W, ...)         the status bar rows as always
The scanout reads a packed row and sends it twice as wide, so the board
spends no CPU on the doubling. A 2D drawing that reaches the view's rows
first spreads the packed rows to full width in place (R_QogTurboUnpack,
about 0.3 ms on the board) and the frame then goes out as a normal one.

While R_RenderView_ draws a Turbo view, vid says the screen is half as wide
(vid.width and vid.rowbytes W/2, vid.aspect twice id's, as id's lcd_x mode
does with the rows), so R_SetupFrame, R_ViewChanged and D_ViewChanged make
a view of half the width with the same field of view: pixelAspect doubles,
yscale stays, xscale halves. The edge, span, alias, particle and sprite code
then see an ordinary 160x152 view.

The world's edge cache and vertex cache live in frame buffer rows that the
view draws over (QOG_EDGECACHE_FRAME). In a Turbo view they go after the
packed pixels, into the bytes the z-buffer takes later, which a Turbo
frame's scanout never reads: the view starts at once instead of waiting for
the last scanout to pass those rows (QOG_VID_SCANRACE).
*/
#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"

#ifdef QOG_RTURBO

int		r_qog_turboview;		// vid.h
int		r_qog_turbopacked;		// vid.h
byte	*r_qog_turbocachehi;	// r_draw.c: room for the vertex cache ends here, NULL outside a Turbo view

extern vrect_t	scr_vrect;		// screen.c

static byte				r_qog_turbolast;	// the last view was a Turbo one
static unsigned short	r_qog_turbow;		// vid.width outside the view
static float			r_qog_turboaspect;	// vid.aspect outside the view
static short			r_qog_turboscrw;	// scr_vrect.width outside the view
static d_zval_t			*r_qog_turbozbuf;	// d_pzbuffer outside the view
static unsigned short	*r_qog_turboedgecache;	// r_qog_edgecache outside the view

/*
==============
R_QogTurboBegin

R_RenderView calls it before R_RenderView_. It decides whether this view
is a Turbo one and, if so, makes vid the half width screen.
==============
*/
void R_QogTurboBegin (void)
{
	int		turbo;

	turbo = r_fast.value >= 2 && !r_qog_turboview && !lcd_x.value
		&& scr_vrect.x == 0 && scr_vrect.y == 0 && scr_vrect.width == (int)vid.width
		&& vid.rowbytes == vid.width && !(vid.width & 15);
	if (turbo != r_qog_turbolast)
	{
		r_viewchanged = true;	// R_SetupFrame sets the view up again for the other width
		r_qog_turbolast = turbo;
	}
	r_qog_turbopacked = 0;		// this view draws over the rows (a Turbo view packs them again)
	if (!turbo)
		return;

	r_qog_turboview = 1;
	r_qog_turbow = vid.width;
	r_qog_turboaspect = vid.aspect;
	r_qog_turboscrw = scr_vrect.width;
	r_qog_turbozbuf = d_pzbuffer;
	r_qog_turboedgecache = r_qog_edgecache;

	vid.width >>= 1;
	vid.rowbytes = vid.width;
	vid.aspect *= 2;			// pixels twice as wide as they are high
	scr_vrect.width >>= 1;		// the in-place underwater warp works in view pixels
	if (sizeof(d_zval_t) == 1)	// the one-byte z-buffer after the packed pixels
		d_pzbuffer = (d_zval_t *)(vid.buffer + scr_vrect.height * vid.rowbytes);
}

/*
==============
R_QogTurboEnd

After R_RenderView_: vid is the full screen again, and the frame holds a
packed Turbo view.
==============
*/
void R_QogTurboEnd (void)
{
	if (!r_qog_turboview)
		return;
	vid.width = r_qog_turbow;
	vid.rowbytes = r_qog_turbow;
	vid.aspect = r_qog_turboaspect;
	scr_vrect.width = r_qog_turboscrw;
	d_pzbuffer = r_qog_turbozbuf;
	r_qog_edgecache = r_qog_turboedgecache;
	r_qog_turbocachehi = NULL;
	r_qog_turboview = 0;
	r_qog_turbopacked = scr_vrect.height;
}

#ifdef QOG_EDGECACHE_FRAME
/*
==============
R_QogTurboCaches

R_EdgeDrawing in a Turbo view, when the world's edge cache lives in the
frame buffer (r_qog_edgecache == vid.buffer): places the edge cache and
the vertex cache after it for this view. The view owns the frame's first
P full rows (P * W bytes): its packed pixels fill the first half, its
z-buffer or nothing the second. Two places:
 a. the second half, on into the z-buffer memory up to the edge table when
    a big map needs it (the status bar rows there are repainted after the
    view, R_HudRepaint). A Turbo frame's scanout never reads the second
    half, so the view starts at once after a Turbo frame.
 b. the start of the frame, as in the other modes, which the last scanout
    reads first when it sent a normal frame.
The one the scanout in flight lets go of sooner is taken.
==============
*/
#define QOG_TURBO_VERTMIN	(64 * 32 + 512)		// the smallest vertex cache and the Fast header (r_draw.c)
#define QOG_TURBO_VERTFAST	(128 * 32 + 512)	// the Fast one

void R_QogTurboCaches (void)
{
	byte	*base = vid.buffer, *a, *end, *lim;
	int		bytes = r_qog_edgecachebytes;
#ifdef QOG_VID_SCANRACE
	int		needa, needb;
#endif

	r_qog_turbocachehi = NULL;
	if (!r_qog_turboview || (byte *)r_qog_turboedgecache != base)
		return;
	end = base + r_refdef.vrect.height * r_qog_turbow;
	a = base + r_refdef.vrect.height * vid.rowbytes;
	lim = end;
#ifdef QOG_EDGES_IN_ZBUFFER
	if (a + bytes + QOG_TURBO_VERTMIN > lim && end == r_ztemp_base && (byte *)r_edgesentinels > end)
		lim = (byte *)r_edgesentinels;
#endif
	r_qog_edgecache = (unsigned short *)base;
	r_qog_turbocachehi = end;
	if (a + bytes + QOG_TURBO_VERTMIN > lim)
		return;		// b
#ifdef QOG_VID_SCANRACE
	needa = VID_QogScanNeed (a, a + bytes + QOG_TURBO_VERTMIN);
	needb = VID_QogScanNeed (base, base + bytes + QOG_TURBO_VERTFAST);
	if (needa > needb)
		return;		// b
#endif
	r_qog_edgecache = (unsigned short *)a;
	r_qog_turbocachehi = lim;
}
#endif

/*
==============
R_QogTurboUnpack

Spreads the packed rows of a Turbo view to full width in place, each pixel
twice, from the last row to the first: row r's full width bytes
[r * W, r * W + W) only cover packed rows 2r and 2r + 1, which are done
by then, and within row 0 each word is read before it is written over.
==============
*/
void R_QogTurboUnpack (void)
{
	int			rows = r_qog_turbopacked, half = vid.rowbytes >> 1, r, i;
	unsigned	*src, *dst, w, lo, hi;

	if (!rows)
		return;
#ifdef QOG_VID_SCANRACE
	VID_QogScanDone ();		// the last scanout reads these rows packed
#elif defined(QOG_VID_LOCKWAIT)
	VID_LockBuffer ();
#endif
	r_qog_turbopacked = 0;
	for (r = rows - 1 ; r >= 0 ; r--)
	{
		src = (unsigned *)(vid.buffer + r * half);
		dst = (unsigned *)(vid.buffer + r * vid.rowbytes);
		for (i = (half >> 2) - 1 ; i >= 0 ; i--)
		{
			w = src[i];		// four pixels, little endian
			lo = (w & 0xFF) | ((w & 0xFF00) << 8);
			hi = ((w >> 16) & 0xFF) | ((w >> 8) & 0xFF0000);
			dst[2 * i] = lo * 0x101;
			dst[2 * i + 1] = hi * 0x101;
		}
	}
}

#endif	// QOG_RTURBO
