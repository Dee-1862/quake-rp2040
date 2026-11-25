/* QuakeOG (Agent AI): shared by the alias model and particle drawers
 * (r_alias.c, d_polyse.c, r_aclip.c, d_part.c, r_part.c).
 *
 * QOG_ALIAS_STATS (PC measurement only): work counts of the alias model and
 * particle drawers, printed every 100 views as "qog_aliasstats" lines, the
 * sums since the level started. Set 0 is every entity but the view model,
 * set 1 the view model. */
#ifndef R_QOGALIAS_H
#define R_QOGALIAS_H

#if defined(QOG_ALIAS_FAST) && !defined(QOG_RFAST)
#error "QOG_ALIAS_FAST is a part of the Fast render mode, it needs QOG_RFAST"
#endif

#ifdef QOG_ALIAS_STATS
enum
{
	QAS_CHECK,		// R_AliasCheckBBox calls
	QAS_DRAWN,		// R_AliasDrawModel calls
	QAS_TRIVIAL,	// of them unclipped (trivial accept)
	QAS_SUBDIVM,	// of them drawn by recursive subdivision
	QAS_VERTS,		// vertices transformed
	QAS_TRIS,		// triangles looked at by the drawers
	QAS_BACK,		// of them facing away
	QAS_SMOOTH,		// triangles rasterized with gradients
	QAS_ZERO,		// of them with no pixel
	QAS_TINY,		// of them with 1 to 4 pixels
	QAS_SPANS,		// span rows with pixels
	QAS_PIX,		// span pixels
	QAS_PIXW,		// span pixels written (z test passed)
	QAS_SUBTRI,		// triangles drawn by subdivision
	QAS_SUBCALL,	// recursive subdivision calls
	QAS_SUBPIX,		// subdivision points written
	QAS_CLIPTRI,	// triangles through R_AliasClipTriangle
	QAS_PARTS,		// D_DrawParticle calls
	QAS_PARTVIS,	// particles on the screen
	QAS_PARTPIX,	// particle pixels tested
	QAS_TINYTRI,	// Turbo triangles drawn without gradients (Agent AR, d_polyse.c)
	QAS_SUBLEAF,	// subdivision leaves tested inline, no call (Agent AQ, QOG_SUBDIV_LEAF)
	QAS_SMALLTRI,	// triangles of two lines or less with no pixel or one, without gradients (Agent AQ, QOG_ALIAS_SMALLTRI)
	QAS_N
};
extern unsigned	qog_astats[2][QAS_N];
extern int		qog_astat_vm;
#define QOG_AST(i, n)	(qog_astats[qog_astat_vm][i] += (n))
void R_QogAliasStatsView (void);
#else
#define QOG_AST(i, n)	((void)0)
#endif

#ifdef QOG_ALIAS_FAST
// QuakeOG (Agent AI): (int)(f * 2^frac), truncated, from the float's bits
// with no float arithmetic, for |f * 2^frac| below 2^30 (larger values give
// +-0x3fffffff). Fast mode only.
static int R_QogFix (float f, int frac)
{
	union
	{
		float	f;
		int		i;
	} b;
	int		e, m;

	b.f = f;
	e = ((b.i >> 23) & 0xff) - (127 + 23) + frac;
	if (e <= -24)
		return 0;
	if (e > 6)
		return b.i < 0 ? -0x3fffffff : 0x3fffffff;
	m = (b.i & 0x7fffff) | 0x800000;
	m = e >= 0 ? m << e : m >> -e;
	return b.i < 0 ? -m : m;
}

// QuakeOG (Agent AI): the view's constants of the Fast mode particle drawer
// (d_part.c), made by R_DrawParticles once a view
typedef struct
{
	int		org[3];				// r_origin, 24.8
	int		right[3], up[3], fwd[3];	// vright, vup and vpn, 2.14
	int		xs, ys;				// xscaleshrink and yscaleshrink, 28.4
	int		xc, yc;				// xcenter and ycenter plus a half, 28.4
} d_qogpview_t;

void D_QogParticleView (d_qogpview_t *pv);
void D_QogDrawParticleFast (particle_t *pparticle, const d_qogpview_t *pv);
#endif

#endif
