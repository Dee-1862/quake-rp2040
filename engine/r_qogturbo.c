/*
QuakeOG, the Turbo render mode, r_fast 2.

The 3D view is drawn at half width (160x152 for the board's 320x152 view)
with Fast mode's shortcuts, and the LCD scanout doubles each of its pixels
sideways (platform/qog_video.c), blocky like Doom's low detail. The 2D parts
(status bar, menus, console, text) stay at full width and sharp.

Frame buffer layout while a Turbo view is in it (P rows of view, W = 320).
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
a view of half the width with the same field of view, pixelAspect doubles,
yscale stays, xscale halves. The edge, span, alias, particle and sprite code
then see an ordinary 160x152 view.

The world's edge cache and vertex cache live in frame buffer rows that the
view draws over (QOG_EDGECACHE_FRAME). In a Turbo view they go after the
packed pixels, into the bytes the z-buffer takes later, which a Turbo
frame's scanout never reads, the view starts at once instead of waiting for
the last scanout to pass those rows (QOG_VID_SCANRACE).
*/
#include "quakedef.h"
#include "r_local.h"
#include "d_local.h"

#ifdef QOG_RTURBO

#if defined(_WIN32) && !defined(QOG_TURBO_TEXT_STATS)
#define QOG_TURBO_TEXT_STATS	// Earlier work, the PC counts the listed characters (vid_pc.c)
#endif

int		r_qog_turboview;		// vid.h
int		r_qog_turbopacked;		// vid.h
byte	*r_qog_turbocachelo, *r_qog_turbocachehi;	// r_draw.c, room for the vertex cache, hi NULL outside a Turbo view

extern vrect_t	scr_vrect;		// screen.c

#ifdef QOG_VID_SCANRACE
#define R_QogTurboNeed(lo, hi)	VID_QogScanNeed (lo, hi)	// rows the scanout in flight still has to send
#else
#define R_QogTurboNeed(lo, hi)	0
#endif

static byte				r_qog_turbolast;	// the last view was a Turbo one
static unsigned short	r_qog_turbow;		// vid.width outside the view
static float			r_qog_turboaspect;	// vid.aspect outside the view
static short			r_qog_turboscrw;	// scr_vrect.width outside the view
static d_zval_t			*r_qog_turbozbuf;	// d_pzbuffer outside the view
static unsigned short	*r_qog_turboedgecache;	// r_qog_edgecache outside the view

// Earlier work, characters over the packed view (vid.h R_QogTurboText), two
// lists so the one the scanout in flight reads stays as it is
static r_qogtext_t		r_qog_turbotext[2];
static r_qogtext_t		*r_qog_text = &r_qog_turbotext[0];	// the frame's
#ifdef QOG_TURBO_TEXT_STATS
int		r_qog_turbotextmax, r_qog_turbotextover, r_qog_turbotextframes;	// PC measurement
#endif

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
	r_qog_text->numruns = r_qog_text->numchars = 0;	// Earlier work, and over any character listed before it
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
frame buffer (r_qog_edgecache == vid.buffer), places the edge cache and
the vertex cache for this view. The view owns the frame's first P full
rows (P * W bytes), its packed pixels fill the first half, its z-buffer
or nothing the second. A Turbo frame's scanout never reads the second
half, a normal frame's reads it last. Three ways.
 a. both caches in the second half, on into the z-buffer memory up to the
    edge table when a big map needs it (the status bar rows there are
    repainted after the view, R_HudRepaint)
 b. the edge cache there and the smallest vertex cache at the frame's
    start, which a Turbo frame's scanout sends in its first 15 rows
 c. both at the frame's start, as in the other modes
The one the scanout in flight lets go of soonest is taken.
==============
*/
#define QOG_TURBO_VERTMIN	(64 * 32 + 512)		// the smallest vertex cache and the Fast header (r_draw.c)
#define QOG_TURBO_VERTFAST	(128 * 32 + 512)	// the Fast one

void R_QogTurboCaches (void)
{
	byte	*base = vid.buffer, *a, *end, *lim;
	int		bytes = r_qog_edgecachebytes, need, n;

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
	// c
	r_qog_edgecache = (unsigned short *)base;
	r_qog_turbocachelo = base + bytes;
	r_qog_turbocachehi = end;
	need = R_QogTurboNeed (base, base + bytes + QOG_TURBO_VERTFAST);
	if (a + bytes > lim)
		return;
	// b
	n = R_QogTurboNeed (a, a + bytes);
	if (n < R_QogTurboNeed (base, base + QOG_TURBO_VERTMIN))
		n = R_QogTurboNeed (base, base + QOG_TURBO_VERTMIN);
	if (n <= need)
	{
		need = n;
		r_qog_edgecache = (unsigned short *)a;
		r_qog_turbocachelo = base;
		r_qog_turbocachehi = base + QOG_TURBO_VERTMIN;
	}
	// a
	if (a + bytes + QOG_TURBO_VERTMIN <= lim && R_QogTurboNeed (a, a + bytes + QOG_TURBO_VERTMIN) <= need)
	{
		r_qog_edgecache = (unsigned short *)a;
		r_qog_turbocachelo = a + bytes;
		r_qog_turbocachehi = lim;
	}
}
#endif

/*
==============
R_QogTurboUnpack

Spreads the packed rows of a Turbo view to full width in place, each pixel
twice, from the last row to the first, row r's full width bytes
[r * W, r * W + W) only cover packed rows 2r and 2r + 1, which are done
by then, and within row 0 each word is read before it is written over.
==============
*/
void R_QogTurboUnpack (void)
{
	int			rows = r_qog_turbopacked, half = vid.rowbytes >> 1, r, i, n;
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
	// Earlier work, the characters listed so far go into the frame now, in
	// their order, before whatever spread the view draws over them
	n = r_qog_text->numruns;
	r_qog_text->numruns = r_qog_text->numchars = 0;
	for (r = 0 ; r < n ; r++)
	{
		w = r_qog_text->runs[r];
		for (i = 0 ; i < (int)((w >> 17) & 63) ; i++)
			Draw_Character ((w & 511) + 8 * i, (w >> 9) & 255, r_qog_text->chars[(w >> 23) + i]);
	}
}

/*
==============
R_QogTurboText

Earlier work, Draw_Character over a packed Turbo view. A character wholly
inside the packed rows is listed instead of drawn (vid.h), when the list
has room. The scanout draws it over the doubled pixels as Draw_Character
would draw it over the spread view, a pixel of index 0 is see-through.
==============
*/
int R_QogTurboText (int x, int y, int num)
{
	r_qogtext_t	*t = r_qog_text;
	unsigned	*last;

	if (!r_qog_turbopacked || y < 0 || y + 8 > r_qog_turbopacked || x < 0 || x + 8 > (int)vid.width
		|| vid.conbuffer != vid.buffer || vid.conrowbytes != vid.rowbytes)
		return 0;
	last = t->numruns ? &t->runs[t->numruns - 1] : NULL;
	if (t->numchars < QOG_TURBO_CHARS)
	{
		// the next character of the last run (Draw_String, Con_DrawNotify)
		if (last && (int)((*last >> 9) & 255) == y && ((*last >> 17) & 63) < 63
			&& (int)(*last & 511) + 8 * (int)((*last >> 17) & 63) == x)
		{
			*last += 1u << 17;
			t->chars[t->numchars++] = (byte)num;
			return 1;
		}
		if (t->numruns < QOG_TURBO_RUNS)
		{
			t->runs[t->numruns++] = (unsigned)x | ((unsigned)y << 9) | (1u << 17) | ((unsigned)t->numchars << 23);
			t->chars[t->numchars++] = (byte)num;
			return 1;
		}
	}
#ifdef QOG_TURBO_TEXT_STATS
	r_qog_turbotextover++;
#endif
	return 0;		// Draw_Character spreads the view and draws the list and this one
}

/*
==============
R_QogTurboTextTake

Earlier work, VID_Update hands the frame's list to the scanout and the next
frame lists into the other one. Empty when the frame holds no packed view.
==============
*/
const r_qogtext_t *R_QogTurboTextTake (void)
{
	r_qogtext_t	*t = r_qog_text;

	if (!r_qog_turbopacked)
		t->numruns = t->numchars = 0;	// a spread frame drew them already
#ifdef QOG_TURBO_TEXT_STATS
	if (t->numruns)
		r_qog_turbotextframes++;
	if (t->numchars > r_qog_turbotextmax)
		r_qog_turbotextmax = t->numchars;
#endif
	r_qog_text = t == &r_qog_turbotext[0] ? &r_qog_turbotext[1] : &r_qog_turbotext[0];
	r_qog_text->numruns = r_qog_text->numchars = 0;
	return t;
}

/*
==============
R_QogTurboChar

The crosshair over a packed Turbo view, each lit pixel of the character
lands on the packed pixel under it, so the frame stays packed (no spread
to full width every frame). Anywhere else Draw_Character draws it.
==============
*/
void R_QogTurboChar (int x, int y, int num)
{
	extern byte	*draw_chars;	// draw.c
	byte		*src, *dst;
	int			r, c, half = vid.rowbytes >> 1;

	if (y < 0 || y + 8 > r_qog_turbopacked || x < 0 || x + 8 > (int)vid.width
		|| r_qog_text->numruns)	// Earlier work, over listed characters it must stay above them
	{
		Draw_Character (x, y, num);
		return;
	}
#ifdef QOG_VID_SCANRACE
	VID_QogScanDone ();
#elif defined(QOG_VID_LOCKWAIT)
	VID_LockBuffer ();
#endif
	num &= 255;
	src = draw_chars + ((num >> 4) << 10) + ((num & 15) << 3);
	for (r = 0 ; r < 8 ; r++, src += 128)
	{
		dst = vid.buffer + (y + r) * half;
		for (c = 0 ; c < 8 ; c++)
			if (src[c])
				dst[(x + c) >> 1] = src[c];
	}
}

#endif	// QOG_RTURBO
