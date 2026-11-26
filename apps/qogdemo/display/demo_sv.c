/* The server state the demo player links instead of sv_main.c's
 * (apps/qogdemo, see demo_wrap.c).
 *
 * The client and renderer read sv.active, the first field of server_t, to
 * know whether a local server runs (it never does here). Every other read
 * of sv in the linked objects is behind sv.active or in a console command
 * nothing runs here (r_part.c's pointfile reads sv.name).
 * id's server_t is 11,360 bytes on the board. This file does not include
 * quakedef.h, so its sv can be just server_t's first fields, in its order
 * and types, the compiler checks nothing across files, and nothing of the
 * server itself is linked. */
typedef int qboolean_t;		/* qboolean, a 4-byte enum in the board engine */

struct
{
	qboolean_t	active;		/* false, no server */
	qboolean_t	paused;
	qboolean_t	loadgame;
	double		time;
} sv;
