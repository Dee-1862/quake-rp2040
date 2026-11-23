/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2026 QuakeOG contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
/* RP2040 (FREE-WILi OG display CPU) system layer: id's sys.h.
 *
 * There is no filesystem. The game data region (platform/qog_flashmap.h)
 * holds one pack file, built on the PC by tools/mkpak.py; it appears to the
 * engine as <basedir>/id1/pak0.pak, read with memcpy and mapped in place
 * with Sys_FlashMap. Nothing is written (saves come later, to the save
 * slots at the end of flash).
 *
 * QuakeOG: a baked model image (engine/model.c, "QOGB") may follow the pack
 * at a 64 KB boundary (tools/mkboard.py lays both out). It appears as
 * <basedir>/id1/qogbake.bin and is mapped in place the same way. The engine
 * checks it was built for the address it is found at.
 *
 * Hunk size: QOG_HUNK_KB. The engine does not fit in RAM yet; this layer
 * is built to measure code size, static RAM and stack frames on the target
 * (docs/fit-plan.md). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "quakedef.h"
#include "pico/stdlib.h"
#include "fwog_display.h"
#include "qog_flashmap.h"
#include "qog_aim.h"

#ifndef QOG_HUNK_KB
#define QOG_HUNK_KB 64
#endif

qboolean isDedicated = false;
static byte s_hunk[QOG_HUNK_KB * 1024] __attribute__((aligned(16)));

/* QuakeOG: the hunk, for a main() in another file (apps/qogfly, built with
   QOG_SYS_NO_MAIN). */
void *qog_rp2040_hunk(int *size) { *size = (int)sizeof s_hunk; return s_hunk; }

/* BSP contract rule 2: red held 6 s powers the board off. The main loop
   calls fwog_power_poll every iteration (and Sys_Error keeps doing so). */
FWOG_POWER_DEFAULT();

/* newlib's remove() wants this; there is no filesystem to delete from. */
int _unlink(const char *path) { (void)path; return -1; }

/* ---- the pack in flash ------------------------------------------------ */

#define PAK_HANDLE 1
static const byte *const s_pak = (const byte *)QOG_DATA_XIP_ADDR;
static int s_pakpos;

static int pak_length(void) {
    if (memcmp(s_pak, "PACK", 4)) return -1;
    int dirofs = *(const int *)(s_pak + 4), dirlen = *(const int *)(s_pak + 8);
    if (dirofs < 12 || dirlen < 0 || dirofs + dirlen > (int)QOG_DATA_MAX_SIZE) return -1;
    return dirofs + dirlen;
}

static qboolean is_pak_path(const char *path) {
    size_t n = strlen(path);
    return n >= 12 && !strcmp(path + n - 12, "id1/pak0.pak");
}

/* ---- the baked model image after it ----------------------------------- */

#define BAKE_HANDLE 2
static const byte *s_bake;
static int s_bakelen = -1;     /* -1: not looked for yet, 0: none */

static qboolean is_bake_path(const char *path) {
    size_t n = strlen(path);
    return n >= 15 && !strcmp(path + n - 15, "id1/qogbake.bin");
}

/* First 64 KB boundary after the pack holding "QOGB" with a size that fits
   the data region (the same rule as the PC's Sys_FlashNext, and where
   tools/mkboard.py puts it, also with its --bake-base option). */
static int bake_length(void) {
    if (s_bakelen >= 0) return s_bakelen;
    s_bakelen = 0;
    int pak = pak_length();
    if (pak < 0) return 0;
    for (unsigned ofs = ((unsigned)pak + 0xFFFFu) & ~0xFFFFu; ofs + 64 <= QOG_DATA_MAX_SIZE; ofs += 0x10000u) {
        const unsigned *h = (const unsigned *)(s_pak + ofs);
        if (h[0] != 0x42474F51u) continue;          /* "QOGB", QOGB_MAGIC in engine/model.h */
        if (h[4] < 64 || h[4] > QOG_DATA_MAX_SIZE - ofs) break;
        s_bake = s_pak + ofs;
        s_bakelen = (int)h[4];                      /* size, header included */
        break;
    }
    return s_bakelen;
}

static int s_bakepos;

int Sys_FileOpenRead(char *path, int *hndl) {
    if (is_bake_path(path) && bake_length() > 0) {
        *hndl = BAKE_HANDLE;
        s_bakepos = 0;
        return s_bakelen;
    }
    int len = is_pak_path(path) ? pak_length() : -1;
    if (len < 0) { *hndl = -1; return -1; }
    *hndl = PAK_HANDLE;
    s_pakpos = 0;
    return len;
}

int Sys_FileOpenWrite(char *path) { Sys_Error("Sys_FileOpenWrite: no writable storage (%s)", path); return -1; }
void Sys_FileClose(int handle) { (void)handle; }
void Sys_FileSeek(int handle, int position) {
    if (handle == BAKE_HANDLE) s_bakepos = position;
    else s_pakpos = position;
}

int Sys_FileRead(int handle, void *dest, int count) {
    if (handle == BAKE_HANDLE) {
        memcpy(dest, s_bake + s_bakepos, (size_t)count);
        s_bakepos += count;
        return count;
    }
    memcpy(dest, s_pak + s_pakpos, (size_t)count);
    s_pakpos += count;
    return count;
}

int Sys_FileWrite(int handle, void *data, int count) { (void)handle; (void)data; return count; }
int Sys_FileTime(char *path) {
    if (is_bake_path(path)) return bake_length() > 0 ? 1 : -1;
    return is_pak_path(path) && pak_length() > 0 ? 1 : -1;
}
void Sys_mkdir(char *path) { (void)path; }

void *Sys_FlashMap(char *path, int length) {
    if (is_bake_path(path))
        return (bake_length() > 0 && length == s_bakelen) ? (void *)s_bake : NULL;
    return (is_pak_path(path) && pak_length() > 0) ? (void *)s_pak : NULL;
}

/* ---- system ----------------------------------------------------------- */

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length) { (void)startaddr; (void)length; }
void Sys_LowFPPrecision(void) {}
void Sys_HighFPPrecision(void) {}
void Sys_SetFPCW(void) {}
void Sys_Sleep(void) {}
char *Sys_ConsoleInput(void) { return NULL; }
void Sys_DebugLog(char *file, char *fmt, ...) { (void)file; (void)fmt; }

void Sys_Printf(char *fmt, ...) {
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    DIAG("%s", text);
}

void Sys_Error(char *error, ...) {
    char text[256];
    va_list ap;
    va_start(ap, error);
    vsnprintf(text, sizeof text, error, ap);
    va_end(ap);
    /* Keep the red-button power-off alive while reporting (BSP rule): the
       display CPU has no watchdog and must stay switch-off-able. */
    for (;;) {
        DIAG("Sys_Error: %s\n", text);
        for (int i = 0; i < 500; i++) {
            fwog_power_poll(to_ms_since_boot(get_absolute_time()));
            sleep_ms(2);
        }
    }
}

void Sys_Quit(void) { Sys_Error("quit"); }

double Sys_FloatTime(void) { return (double)time_us_64() * 1e-6; }

/* Buttons for Sys_SendKeyEvents (vid_rp2040.c): the main loop polls them
   through fwog_power_poll, the only poll allowed (docs/lessons.md L5). */
uint8_t qog_rp2040_buttons;

#ifndef QOG_SYS_NO_MAIN
int main(void) {
    static char *argv[] = { "quake", "-nosound" };
    quakeparms_t parms;

    board_init();
    memset(&parms, 0, sizeof parms);
    COM_InitArgv(2, argv);
    parms.argc = com_argc;
    parms.argv = com_argv;
    parms.basedir = ".";
    parms.membase = s_hunk;
    parms.memsize = sizeof s_hunk;
    Host_Init(&parms);

    int frame = 0;
    double oldtime = Sys_FloatTime();
    for (;;) {
        fwog_power_t pw = fwog_power_poll(to_ms_since_boot(get_absolute_time()));
        qog_rp2040_buttons = pw.buttons.down;
        double newtime = Sys_FloatTime();
        double dt = newtime - oldtime;
        if (dt < 1.0 / 72.0) continue;   /* see port/pc/sys_pc.c: id calls rand() per call */
        qog_aim_update(newtime);
        Host_Frame((float)dt);
        if (!frame && host_framecount >= 1) { frame = 1; qog_aim_apply_defaults(); }
        oldtime = newtime;
    }
}
#endif
