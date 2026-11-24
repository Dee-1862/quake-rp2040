/* QuakeOG (Agent AC): the split server's deepest stack in the board's bytes
 * (PC measurement build only, pc/CMakeLists.txt QOG_SV_STACKPROBE).
 *
 * quakeog_sv is compiled with /Gh and /GH (every function calls _penter
 * before its prologue and _pexit before its epilogue) and /Ob0 (no
 * inlining, so every C function of the engine and of the compiled QuakeC is
 * a call). They keep a shadow call stack, checked against the real stack
 * pointer: an entry whose stack pointer is not above a new call's has
 * returned (this also follows Host_Error's longjmp). Each entry
 * weighs what the same function's frame is on the board, read from the ARM
 * build's -fstack-usage files (tools/stack_chain.py --su-table), so the
 * deepest chain of a run is measured in board bytes, recursion included,
 * which the static chain of tools/stack_chain.py cannot see. A function the
 * board build inlined has no frame of its own there and weighs 0 (its caller's
 * frame holds it), a PC-only function (link_pc.c, the C runtime) weighs 0
 * and is listed.
 *
 *   quakeog_sv -stackprobe <table> ...   prints "stackprobe:" lines
 *
 * Also the deepest chain that runs the mixer (S_Update and friends), which on
 * the board must leave the stack's lowest QOG_SND_SCRATCH bytes free. */
#ifdef QOG_SV_STACKPROBE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXDEPTH	1024
#define FNHASH		(1 << 16)
#define NAMEHASH	(1 << 14)

typedef struct { unsigned addr; int frame; int flags; } fninfo_t;
#define F_MIX		1		/* the mixer: S_Update_ and below */
#define F_UNKNOWN	2		/* no frame in the board's table */

static fninfo_t	s_fn[FNHASH];
static char		*s_names[NAMEHASH];
static int		s_frames[NAMEHASH];

static unsigned	s_sp[MAXDEPTH];
static fninfo_t	*s_chain[MAXDEPTH];
static int		s_arm[MAXDEPTH], s_mix[MAXDEPTH];
static int		s_depth, s_ready, s_busy;
static DWORD	s_thread;

static int		s_best, s_bestn, s_bestmix, s_bestmixn, s_dirty;
static fninfo_t	*s_bestchain[MAXDEPTH], *s_bestmixchain[MAXDEPTH];
static int		s_bestarm[MAXDEPTH], s_bestmixarm[MAXDEPTH];
static unsigned	s_pcdeepest = 0xFFFFFFFFu, s_pctop;

static unsigned name_hash(const char *s) {
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

static int name_frame(const char *name) {
	unsigned i = name_hash(name) & (NAMEHASH - 1);
	while (s_names[i]) {
		if (!strcmp(s_names[i], name)) return s_frames[i];
		i = (i + 1) & (NAMEHASH - 1);
	}
	return -1;
}

static void load_table(const char *path) {
	FILE *f = fopen(path, "r");
	char name[512];
	int frame, n = 0;
	if (!f) { printf("stackprobe: cannot read %s\n", path); return; }
	while (fscanf(f, "%511s %d", name, &frame) == 2) {
		unsigned i = name_hash(name) & (NAMEHASH - 1);
		while (s_names[i]) i = (i + 1) & (NAMEHASH - 1);
		s_names[i] = _strdup(name);
		s_frames[i] = frame;
		n++;
	}
	fclose(f);
	printf("stackprobe: %d board frames from %s\n", n, path);
}

static char *fn_name(unsigned addr, char *buf, int len) {
	char symbuf[sizeof(SYMBOL_INFO) + 256];
	SYMBOL_INFO *sym = (SYMBOL_INFO *)symbuf;
	DWORD64 disp = 0;
	memset(symbuf, 0, sizeof symbuf);
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen = 255;
	if (SymFromAddr(GetCurrentProcess(), addr, &disp, sym)) {
		strncpy(buf, sym->Name, len - 1);
		buf[len - 1] = 0;
	} else
		_snprintf(buf, len, "?%08x", addr);
	return buf;
}

static fninfo_t *fn_lookup(unsigned addr) {
	unsigned i = (addr * 2654435761u) >> 16;
	char name[256];
	while (s_fn[i].addr && s_fn[i].addr != addr) i = (i + 1) & (FNHASH - 1);
	if (s_fn[i].addr) return &s_fn[i];
	s_fn[i].addr = addr;
	fn_name(addr, name, sizeof name);
	s_fn[i].frame = name_frame(name);
	if (s_fn[i].frame < 0) { s_fn[i].frame = 0; s_fn[i].flags |= F_UNKNOWN; }
	if (!strncmp(name, "S_Update", 8) || !strncmp(name, "S_Paint", 7) || !strncmp(name, "SND_Paint", 9)
		|| !strncmp(name, "S_MixChannel", 12) || !strncmp(name, "S_TransferPaint", 15))
		s_fn[i].flags |= F_MIX;
	return &s_fn[i];
}

static void __cdecl probe_enter(unsigned ret, unsigned sp) {
	fninfo_t *fn;
	int d;
	if (!s_ready || s_busy || GetCurrentThreadId() != s_thread) return;
	s_busy = 1;
	if (sp < s_pcdeepest) s_pcdeepest = sp;
	while (s_depth > 0 && s_sp[s_depth - 1] <= sp) s_depth--;
	if (s_depth < MAXDEPTH) {
		fn = fn_lookup(ret - 5);	/* the call to _penter is the function's first instruction */
		d = s_depth;
		s_sp[d] = sp;
		s_chain[d] = fn;
		s_arm[d] = (d ? s_arm[d - 1] : 0) + fn->frame;
		s_mix[d] = (d ? s_mix[d - 1] : 0) | (fn->flags & F_MIX);
		s_depth++;
		if (s_arm[d] > s_best) {
			s_best = s_arm[d];
			s_bestn = s_depth;
			memcpy(s_bestchain, s_chain, s_depth * sizeof s_chain[0]);
			memcpy(s_bestarm, s_arm, s_depth * sizeof s_arm[0]);
			s_dirty = 1;
		}
		if (s_mix[d] && s_arm[d] > s_bestmix) {
			s_bestmix = s_arm[d];
			s_bestmixn = s_depth;
			memcpy(s_bestmixchain, s_chain, s_depth * sizeof s_chain[0]);
			memcpy(s_bestmixarm, s_arm, s_depth * sizeof s_arm[0]);
			s_dirty = 1;
		}
	}
	s_busy = 0;
}

/* /GH: every function calls _pexit before its epilogue, at the bottom of its
   frame. Its callees, which were entered below that, have returned, and the
   entry just above it is the function itself. Without this a sibling call
   that pushed fewer arguments would look like an ancestor. */
static void __cdecl probe_exit_fn(unsigned sp) {
	if (!s_ready || s_busy || GetCurrentThreadId() != s_thread) return;
	while (s_depth > 0 && s_sp[s_depth - 1] < sp) s_depth--;
	if (s_depth > 0 && s_sp[s_depth - 1] - sp < 0x10000u) s_depth--;
}

void __declspec(naked) __cdecl _pexit(void) {
	__asm {
		push ebp
		mov ebp, esp
		pushad
		pushfd
		lea ecx, [ebp + 8]		; the function's stack pointer before it called _pexit
		push ecx
		call probe_exit_fn
		add esp, 4
		popfd
		popad
		pop ebp
		ret
	}
}

void __declspec(naked) __cdecl _penter(void) {
	__asm {
		push ebp
		mov ebp, esp
		pushad
		pushfd
		lea ecx, [ebp + 8]		; the stack pointer of the caller of _penter at its entry
		push ecx
		push dword ptr [ebp + 4]	; the return address, inside that function
		call probe_enter
		add esp, 8
		popfd
		popad
		pop ebp
		ret
	}
}

/* SV_PushMove's lists from alloca (QOG_PUSHMOVE_ALLOCA): the same bytes on
   the board's stack, added to the function that called alloca, the top of
   the shadow stack */
void qog_stackprobe_alloca(int bytes) {
	int d = s_depth - 1;
	if (!s_ready || s_busy || d < 0 || GetCurrentThreadId() != s_thread) return;
	s_arm[d] += bytes;
	if (s_arm[d] > s_best) {
		s_best = s_arm[d];
		s_bestn = s_depth;
		memcpy(s_bestchain, s_chain, s_depth * sizeof s_chain[0]);
		memcpy(s_bestarm, s_arm, s_depth * sizeof s_arm[0]);
		s_dirty = 1;
	}
}

static void print_chain(const char *what, int total, int n, fninfo_t **chain, int *arm) {
	char name[256];
	int i, unknown = 0;
	printf("stackprobe: %s %d board bytes in %d calls\n", what, total, n);
	for (i = 0; i < n; i++) {
		fn_name(chain[i]->addr, name, sizeof name);
		printf("stackprobe:   %6d %5d %s%s\n", arm[i], chain[i]->frame, name,
			(chain[i]->flags & F_UNKNOWN) ? "  (no board frame)" : "");
		if (chain[i]->flags & F_UNKNOWN) unknown++;
	}
	printf("stackprobe: %s end, %d calls without a board frame\n", what, unknown);
}

void qog_stackprobe_report(void) {
	if (!s_ready || !s_dirty) return;
	s_dirty = 0;
	printf("stackprobe: PC stack used %u bytes\n", s_pctop - s_pcdeepest);
	print_chain("deepest", s_best, s_bestn, s_bestchain, s_bestarm);
	print_chain("deepest with the mixer", s_bestmix, s_bestmixn, s_bestmixchain, s_bestmixarm);
}

/* called by the server loop (port/pc/sys_pc.c) */
void qog_stackprobe_frame(void) {
	static int frames;
	if (++frames % 400 == 0) qog_stackprobe_report();
}

static void probe_exit(void) {
	s_dirty = 1;
	qog_stackprobe_report();
}

void qog_stackprobe_init(int argc, char **argv) {
	int i;
	unsigned here;
	s_pctop = (unsigned)(size_t)&here;
	for (i = 1; i < argc - 1; i++)
		if (!strcmp(argv[i], "-stackprobe")) load_table(argv[i + 1]);
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	if (!SymInitialize(GetCurrentProcess(), NULL, TRUE))
		printf("stackprobe: no symbols (%lu)\n", GetLastError());
	s_thread = GetCurrentThreadId();
	atexit(probe_exit);
	s_ready = 1;
}
#endif
