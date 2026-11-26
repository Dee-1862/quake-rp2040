#include "qog_mem.h"
#include "quakedef.h"
#include "qog_rand.h"

/* QuakeOG, the split game's halves have no server (QOG_SPLIT_CLIENT)
   or no renderer (QOG_SPLIT_SERVER), so their reports leave those parts out. */
#ifdef QOG_SPLIT_CLIENT
#undef QOG_EDICT_STATS
#undef QOG_EDICT_SPARSE
#undef QOG_SV_TABLES
#define QOG_MEM_SERVER 0
#else
#define QOG_MEM_SERVER 1
#endif
#ifdef QOG_SPLIT_SERVER
#define QOG_MEM_RENDER 0
#else
#define QOG_MEM_RENDER 1
#endif

/* Private to zone.c, mirrored here so zone.c stays unmodified. If zone.c's
   structs ever change, change these with them. */
typedef struct memblock_s {
    int size, tag, id;
    struct memblock_s *next, *prev;
    int pad;
} memblock_t;
typedef struct {
    int size;
    memblock_t blocklist;
    memblock_t *rover;
} memzone_t;
typedef struct cache_system_s {
    int size;
    cache_user_t *user;
    char name[16];
    struct cache_system_s *prev, *next;
    struct cache_system_s *lru_prev, *lru_next;
} cache_system_t;

extern int hunk_size, hunk_low_used, hunk_high_used;
extern memzone_t *mainzone;
extern cache_system_t cache_head;

static qog_mem_t s_m;

static int cache_bytes(void) {
    int n = 0;
    for (cache_system_t *c = cache_head.next; c && c != &cache_head; c = c->next) n += c->size;
    return n;
}

/* id never sets memzone_t.size, so capacity is the sum of every block. */
static int zone_bytes(int *capacity) {
    int used = 0, all = 0;
    if (mainzone)
        for (memblock_t *b = mainzone->blocklist.next; b && b != &mainzone->blocklist; b = b->next) {
            all += b->size;
            if (b->tag) used += b->size;
        }
    if (capacity) *capacity = all;
    return used;
}

static int s_video_bytes;
void qog_mem_set_video(int bytes) { s_video_bytes = bytes; }

void Hunk_Print(qboolean all);
void Cache_Print(void);

#define PEAK(p, v) do { if ((v) > (p)) (p) = (v); } while (0)

const qog_mem_t *qog_mem_sample(void) {
    s_m.hunk_size = hunk_size;
    s_m.hunk_low = hunk_low_used;
    s_m.hunk_high = hunk_high_used;
    s_m.cache = cache_bytes();
    s_m.zone_used = zone_bytes(&s_m.zone_size);
    s_m.video = s_video_bytes;
    /* The zone is carved out of hunk low, so it is already in hunk_low. */
    s_m.total = s_m.hunk_low + s_m.hunk_high + s_m.cache + s_m.video;
    PEAK(s_m.peak_low, s_m.hunk_low);
    PEAK(s_m.peak_high, s_m.hunk_high);
    PEAK(s_m.peak_cache, s_m.cache);
    PEAK(s_m.peak_zone, s_m.zone_used);
    PEAK(s_m.peak_total, s_m.total);
    return &s_m;
}

#ifdef QOG_EDICT_STATS
/* Edict use, sampled after every server frame (end of SV_Physics), live
   (not free) edicts, the slot high-water mark, and field words that are not
   zero, in total and per field. A sparse edict layout pays only for the
   non-zero words, so these peaks size it. */
#define ES_MAXFIELDS 1024
static int es_live, es_hwm, es_words, es_maxone;
double qog_sv_seconds;   /* time in SV_Physics (QuakeC and physics), never reset */
static int es_fieldpeak[ES_MAXFIELDS], es_fieldnow[ES_MAXFIELDS];

void qog_edict_sample(void) {
    int live = 0, words = 0, n, i, nf;
    if (!sv.active || !progs) return;
    nf = progs->entityfields < ES_MAXFIELDS ? progs->entityfields : ES_MAXFIELDS;
    memset(es_fieldnow, 0, nf * sizeof(int));
    for (n = 0; n < sv.num_edicts; n++) {
        edict_t *ed = EDICT_NUM(n);
        int k = 0;
        if (ed->free) continue;
        live++;
        for (i = 0; i < nf; i++)
#ifdef QOG_EDICT_SPARSE
            if (ED_FieldGet(ed, i)) { k++; es_fieldnow[i]++; }
#else
            if (((const int *)&ed->v)[i]) { k++; es_fieldnow[i]++; }
#endif
        words += k;
        PEAK(es_maxone, k);
    }
    for (i = 0; i < nf; i++) PEAK(es_fieldpeak[i], es_fieldnow[i]);
    PEAK(es_live, live);
    PEAK(es_hwm, sv.num_edicts);
    PEAK(es_words, words);
}

static void es_print_fields(void) {
    int i, j;
    if (!progs) return;
    for (i = 0; i < progs->entityfields && i < ES_MAXFIELDS; i++) {
        const char *name = "?";
        for (j = 1; j < progs->numfielddefs; j++)
            if (pr_fielddefs[j].ofs == i) { name = pr_strings + pr_fielddefs[j].s_name; break; }
        Con_Printf("  field %3d %-16s peak %d\n", i, name, es_fieldpeak[i]);
    }
}
#endif

void qog_mem_reset_peaks(void) {
    extern int r_qog_peakedges, r_qog_peaksurfs, r_qog_peakbverts, r_qog_peakbedges;
#ifdef QOG_EDICT_STATS
    es_live = es_hwm = es_words = es_maxone = 0;
    memset(es_fieldpeak, 0, sizeof es_fieldpeak);
#endif
    s_m.peak_low = s_m.peak_high = s_m.peak_cache = s_m.peak_zone = s_m.peak_total = 0;
#if QOG_MEM_RENDER
    r_qog_peakedges = r_qog_peaksurfs = r_qog_peakbverts = r_qog_peakbedges = 0;
#endif
#ifdef QOG_CL_PEAKS
    {   /* QuakeOG, client entity and temp entity peaks (cl_main.c, cl_tent.c) */
        extern int cl_qog_peaktemp, cl_qog_tempdrops, cl_qog_peakbeams;
        cl_qog_peaktemp = cl_qog_tempdrops = cl_qog_peakbeams = 0;
#ifdef QOG_CL_ENTSLOTS
        extern int cl_qog_peakslots, cl_qog_peaknum;
        cl_qog_peakslots = cl_qog_peaknum = 0;
#endif
    }
#endif
}

static void kb_line(const char *what, int now, int peak) {
    Con_Printf("  %-10s %7.1f KB   peak %7.1f KB\n", what, now / 1024.0, peak / 1024.0);
}

static void cmd_qog_mem(void) {
    if (Cmd_Argc() > 1 && !Q_strcmp(Cmd_Argv(1), "reset")) {
        qog_mem_reset_peaks();
        Con_Printf("qog_mem: peaks cleared\n");
        return;
    }
    if (Cmd_Argc() > 1 && !Q_strcmp(Cmd_Argv(1), "hunk")) { Hunk_Print(true); return; }
    if (Cmd_Argc() > 1 && !Q_strcmp(Cmd_Argv(1), "cache")) { Cache_Print(); return; }
#ifdef QOG_EDICT_STATS
    if (Cmd_Argc() > 1 && !Q_strcmp(Cmd_Argv(1), "fields")) { es_print_fields(); return; }
#endif
    const qog_mem_t *m = qog_mem_sample();
    Con_Printf("Dynamic RAM (hunk given %d KB):\n", m->hunk_size / 1024);
    kb_line("hunk low", m->hunk_low, m->peak_low);
    kb_line("hunk high", m->hunk_high, m->peak_high);
    kb_line("cache", m->cache, m->peak_cache);
    kb_line("zone used", m->zone_used, m->peak_zone);
    Con_Printf("  zone size  %7.1f KB (inside hunk low)\n", m->zone_size / 1024.0);
    Con_Printf("  video      %7.1f KB (frame, z-buffer, surface cache)\n", m->video / 1024.0);
    kb_line("TOTAL", m->total, m->peak_total);
    {
        int known, refd, peakmod;
        Mod_KnownStats(&known, &refd, &peakmod);   /* QuakeOG, model.c owns the table layout */
#if QOG_MEM_SERVER
        Con_Printf("  models known %d, referenced by this level %d, edicts %d of %d\n", known, refd,
                   sv.active ? sv.num_edicts : 0, sv.active ? sv.max_edicts : 0);
#else
        Con_Printf("  models known %d, referenced by this level %d\n", known, refd);
#endif
#ifdef QOG_MOD_FLASH
        Con_Printf("  models in one level, most so far %d\n", peakmod);
#endif
#if QOG_MEM_RENDER
        extern int r_qog_peakedges, r_qog_peaksurfs;
        extern int r_qog_peakbverts, r_qog_peakbedges;
        Con_Printf("  render peaks: edges %d, surfaces %d, bmodel verts %d, bmodel edges %d\n",
                   r_qog_peakedges, r_qog_peaksurfs, r_qog_peakbverts, r_qog_peakbedges);
#endif
#ifdef QOG_CL_PEAKS
        {   /* QuakeOG, client entity and temp entity peaks (cl_main.c, cl_tent.c) */
            extern int cl_qog_peaktemp, cl_qog_tempdrops, cl_qog_peakbeams;
#ifdef QOG_CL_ENTSLOTS
            extern int cl_qog_peakslots, cl_qog_peaknum;
            Con_Printf("  client peaks: entity slots %d of %d, highest number %d of %d\n",
                       cl_qog_peakslots, QOG_CL_ENTSLOTS, cl_qog_peaknum, cl_max_edicts);
#endif
            Con_Printf("  client peaks: temp entities %d of %d, dropped %d, beams %d of %d\n",
                       cl_qog_peaktemp, MAX_TEMP_ENTITIES, cl_qog_tempdrops, cl_qog_peakbeams, MAX_BEAMS);
        }
#endif
#if QOG_MEM_SERVER
        if (sv.active)
            Con_Printf("  map %s, signon %d of %d bytes\n", sv.name, sv.signon.cursize, sv.signon.maxsize);
#endif
#ifdef QOG_SV_TABLES
            if (sv.active) {   /* QuakeOG, precache list use against the sizes in server.h */
                int nm = 0, ns = 0;
                while (nm < SV_MAX_MODELS && sv.model_precache[nm]) nm++;
                while (ns < SV_MAX_SOUNDS && sv.sound_precache[ns]) ns++;
                Con_Printf("  precache models %d of %d, sounds %d of %d\n", nm, SV_PRECACHE_MODELS, ns, SV_PRECACHE_SOUNDS);
            }
#endif
#ifdef QOG_EDICT_STATS
        if (sv.active)
            Con_Printf("  edict peaks: live %d, high water %d of %d, non-zero field words %d, most in one edict %d\n",
                       es_live, es_hwm, sv.max_edicts, es_words, es_maxone);
        Con_Printf("  server frames so far: %.3f s\n", qog_sv_seconds);
#endif
#ifdef QOG_EDICT_SPARSE
        if (sv.active) {
            extern int ed_coldpeak, ed_coldcompactions, ed_coldlivepeak;
            Con_Printf("  edict size %d bytes, field lists peak %d words, arena top peak %d words, %d compactions\n",
                       pr_edict_size, ed_coldlivepeak, ed_coldpeak, ed_coldcompactions);
        }
#endif
    }
    Con_Printf("More: qog_mem hunk | qog_mem cache | qog_mem reset\n");
    Con_Printf("Static RAM: see build/ram-report.md (tools/ram_meter.py)\n");
}

#if QOG_MEM_SERVER
/* `qog_statehash`, FNV-1a over every edict and the QuakeC globals. Two runs
   with the same fixed frame time (host_framerate) must print the same value,
   which is how compiled QuakeC is checked against the interpreter (-noaot). */
static unsigned fnv(unsigned h, const void *data, int n) {
    const byte *p = data;
    for (int i = 0; i < n; i++) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

/* Hash by meaning, not bytes, using QuakeC's own type information, strings
   by their text (their offsets move when progs.dat moves to flash), entity
   references relative to sv.edicts, numbers and functions by value. Engine
   link pointers are skipped. So two runs that behave the same hash the same
   even when memory is laid out differently. */
static unsigned hash_value(unsigned h, int type, const int *p) {
    type &= ~DEF_SAVEGLOBAL;
    if (type == ev_string) {
        const char *s = pr_strings + *p;
        return fnv(h, s, (int)strlen(s) + 1);
    }
    if (type == ev_entity) {
        /* An entity reference is the edict's byte offset. Hash it as id's
           offset (edict number times id's edict size) so a smaller edict
           layout (QOG_EDICT_SPARSE, QOG_BASELINE_POOL) still hashes the same. */
        int id = (*p / pr_edict_size) * (progs->entityfields * 4 + ED_ID_HEADERSIZE);
        return fnv(h, &id, 4);
    }
    return fnv(h, p, type == ev_vector ? 12 : 4);
}

/* Up to three words of an edict field, by QuakeC field number. */
static const int *field_words(edict_t *ed, int ofs, int *copy) {
#ifdef QOG_EDICT_SPARSE
    for (int k = 0; k < 3 && ofs + k < progs->entityfields; k++) copy[k] = ED_FieldGet(ed, ofs + k);
    return copy;
#else
    (void)copy;
    return (const int *)&ed->v + ofs;
#endif
}

static void cmd_qog_statehash(void) {
    if (!sv.active || !progs) { Con_Printf("qog_statehash: no server running\n"); return; }
    unsigned h = 0x811C9DC5u, e = 0x811C9DC5u;
    for (int i = 0; i < progs->numglobaldefs; i++) {
        const ddef_t *d = &pr_globaldefs[i];
        int copy[3];   /* PR_GlobalPtr, QOG_AOT_GLOBALS packs the RAM globals */
        for (int k = 0; k < 3 && d->ofs + k < progs->numglobals; k++) copy[k] = *PR_GlobalPtr(d->ofs + k);
        h = hash_value(h, d->type, copy);
    }
    for (int n = 0; n < sv.num_edicts; n++) {
        edict_t *ed = EDICT_NUM(n);
        e = fnv(e, &ed->free, sizeof ed->free);
        if (ed->free) continue;
        for (int i = 1; i < progs->numfielddefs; i++) {
            const ddef_t *d = &pr_fielddefs[i];
            int copy[3];
            e = hash_value(e, d->type, field_words(ed, d->ofs, copy));
        }
    }
    Con_Printf("qog_statehash: time %.2f edicts %d globals %08x edicts %08x rand calls %u\n",
               sv.time, sv.num_edicts, h, e, qog_rand_calls);
}

/* `qog_statedump <file>`, raw globals then edicts, for diffing two runs. */
static void cmd_qog_statedump(void) {
    if (!sv.active || !progs || Cmd_Argc() < 2) { Con_Printf("usage: qog_statedump <file>\n"); return; }
    FILE *f = fopen(Cmd_Argv(1), "wb");
    if (!f) { Con_Printf("qog_statedump: cannot write %s\n", Cmd_Argv(1)); return; }
#ifdef QOG_EDICT_SPARSE
    /* id's layout, a header of id's size that holds only the free flag,
       then every field word */
    static byte head[ED_ID_HEADERSIZE];
    int hdr[3] = { progs->numglobals, sv.num_edicts, ED_ID_HEADERSIZE + progs->entityfields * 4 };
    fwrite(hdr, sizeof hdr, 1, f);
    for (int i = 0; i < progs->numglobals; i++) fwrite(PR_GlobalPtr(i), 4, 1, f);
    for (int n = 0; n < sv.num_edicts; n++) {
        edict_t *ed = EDICT_NUM(n);
        memcpy(head, &ed->free, sizeof ed->free);
        fwrite(head, sizeof head, 1, f);
        for (int i = 0; i < progs->entityfields; i++) {
            int w = ED_FieldGet(ed, i);
            fwrite(&w, 4, 1, f);
        }
    }
#else
    int hdr[3] = { progs->numglobals, sv.num_edicts, pr_edict_size };
    fwrite(hdr, sizeof hdr, 1, f);
    for (int i = 0; i < progs->numglobals; i++) fwrite(PR_GlobalPtr(i), 4, 1, f);
    fwrite(sv.edicts, pr_edict_size, sv.num_edicts, f);
#endif
    fclose(f);
    Con_Printf("qog_statedump: wrote %s\n", Cmd_Argv(1));
}

#endif  /* QOG_MEM_SERVER */

/* Scripted runs. `quit` can open the "are you sure" menu and wait forever. */
static void cmd_qog_quit(void) { Sys_Quit(); }

void qog_mem_register(void) {
    Cmd_AddCommand("qog_mem", cmd_qog_mem);
    Cmd_AddCommand("qog_quit", cmd_qog_quit);
#if QOG_MEM_SERVER
    Cmd_AddCommand("qog_statehash", cmd_qog_statehash);
    Cmd_AddCommand("qog_statedump", cmd_qog_statedump);
#endif
}
