/* Asset manifests (PC development tool).
 *
 * Quake only allows precaching while a map loads, so once a map has loaded,
 * sv.model_precache and sv.sound_precache list every model and sound its
 * QuakeC can ever use. Add the files the client opens on its own (status bar
 * and menu graphics, ambient sounds, effects) and you have the complete set
 * of files a map needs. tools/mkpak.py builds the board's trimmed flash
 * image from these manifests.
 *
 *   qog_manifest_reset         forget the files opened so far
 *   qog_manifest <file>        write: map, precached models and sounds, and
 *                              every pack file opened since the last reset
 *
 * File names given to console commands must be relative: Quake's tokenizer
 * splits on ':' (docs/lessons.md L32). */
#include <stdio.h>
#include <string.h>
#include "quakedef.h"

#define MAX_OPENED 2048
static char s_opened[MAX_OPENED][MAX_QPATH];
static int  s_numopened;

static void record_open(char *path) {
    for (int i = 0; i < s_numopened; i++)
        if (!strcmp(s_opened[i], path)) return;
    if (s_numopened < MAX_OPENED) {
        strncpy(s_opened[s_numopened], path, MAX_QPATH - 1);
        s_opened[s_numopened][MAX_QPATH - 1] = 0;
        s_numopened++;
    }
}

static void cmd_reset(void) { s_numopened = 0; }

static void cmd_manifest(void) {
    if (Cmd_Argc() < 2) { Con_Printf("usage: qog_manifest <file>\n"); return; }
    FILE *f = fopen(Cmd_Argv(1), "w");
    if (!f) { Con_Printf("qog_manifest: cannot write %s\n", Cmd_Argv(1)); return; }
    fprintf(f, "# QuakeOG asset manifest\n");
    if (sv.active) {
        fprintf(f, "map %s\n", sv.name);
        for (int i = 1; i < MAX_MODELS && sv.model_precache[i]; i++) {
            const char *m = sv.model_precache[i];
            if (m[0] && m[0] != '*') fprintf(f, "model %s\n", m);   /* '*N' are inline brush models */
        }
        for (int i = 1; i < MAX_SOUNDS && sv.sound_precache[i]; i++)
            if (sv.sound_precache[i][0]) fprintf(f, "sound sound/%s\n", sv.sound_precache[i]);
    }
    for (int i = 0; i < s_numopened; i++) fprintf(f, "file %s\n", s_opened[i]);
    fclose(f);
    Con_Printf("qog_manifest: wrote %s (%d files opened)\n", Cmd_Argv(1), s_numopened);
}

void qog_manifest_register(void) {
    com_fileopened = record_open;
    Cmd_AddCommand("qog_manifest", cmd_manifest);
    Cmd_AddCommand("qog_manifest_reset", cmd_reset);
}
