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
/* Windows system layer for the PC development build.
 * File I/O follows id's sys_null.c. Everything here implements id's sys.h,
 * so the engine is unchanged.
 *
 *   quakeog_pc.exe [-basedir DIR] [-heapsize KB] [+map e1m1] ...
 *
 * -heapsize is the knob the RAM work turns: find the smallest hunk a map
 * runs in, then make it smaller. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <stdio.h>
#include "quakedef.h"
#include "qog_mem.h"
#include "qog_aim.h"

#ifndef QOG_DEFAULT_BASEDIR
#define QOG_DEFAULT_BASEDIR "."
#endif
#define DEFAULT_HEAP_KB (8 * 1024)
#define QOG_PC_FLASH_ADDR 0x10400000u   /* same address as the board's game data region */
#define QOG_PC_HUNK_ADDR  0x30000000u

qboolean isDedicated = false;

/* ---- file I/O (as sys_null.c) ---------------------------------------- */

#define MAX_HANDLES 10
static FILE *sys_handles[MAX_HANDLES];

static int findhandle(void) {
    for (int i = 1; i < MAX_HANDLES; i++)
        if (!sys_handles[i]) return i;
    Sys_Error("out of handles");
    return -1;
}

static int filelength(FILE *f) {
    int pos = ftell(f);
    fseek(f, 0, SEEK_END);
    int end = ftell(f);
    fseek(f, pos, SEEK_SET);
    return end;
}

int Sys_FileOpenRead(char *path, int *hndl) {
    int i = findhandle();
    FILE *f = fopen(path, "rb");
    if (!f) { *hndl = -1; return -1; }
    sys_handles[i] = f;
    *hndl = i;
    return filelength(f);
}

int Sys_FileOpenWrite(char *path) {
    int i = findhandle();
    FILE *f = fopen(path, "wb");
    if (!f) Sys_Error("Error opening %s: %s", path, strerror(errno));
    sys_handles[i] = f;
    return i;
}

void Sys_FileClose(int handle) { fclose(sys_handles[handle]); sys_handles[handle] = NULL; }
void Sys_FileSeek(int handle, int position) { fseek(sys_handles[handle], position, SEEK_SET); }
int Sys_FileRead(int handle, void *dest, int count) { return (int)fread(dest, 1, count, sys_handles[handle]); }
int Sys_FileWrite(int handle, void *data, int count) { return (int)fwrite(data, 1, count, sys_handles[handle]); }

int Sys_FileTime(char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return -1;
}

void Sys_mkdir(char *path) { _mkdir(path); }

/* Pack files place lumps at any byte offset (id's pak0 has maps and progs at
   odd addresses), and the RP2040 cannot read unaligned words. The board's
   converter will realign; this does the same for the PC image: every file
   starts on a 16-byte boundary and the directory is rewritten to match.
   Returns the new image size, or 0 if `src` is not a pack. */
static int realign_pack(const byte *src, int len, byte *dst) {
    int dirofs, dirlen, n, pos, i;
    if (len < 12 || memcmp(src, "PACK", 4)) return 0;
    dirofs = *(const int *)(src + 4);
    dirlen = *(const int *)(src + 8);
    if (dirofs < 12 || dirlen < 0 || dirofs + dirlen > len || dirlen % 64) return 0;
    n = dirlen / 64;
    pos = 16;
    for (i = 0; i < n; i++) {
        const byte *e = src + dirofs + 64 * i;
        int fpos = *(const int *)(e + 56), flen = *(const int *)(e + 60);
        if (fpos < 0 || flen < 0 || fpos + flen > len) return 0;
        if (dst) memcpy(dst + pos, src + fpos, (size_t)flen);
        pos = (pos + flen + 15) & ~15;
    }
    if (dst) {
        memcpy(dst, "PACK", 4);
        *(int *)(dst + 4) = pos;
        *(int *)(dst + 8) = dirlen;
        int fp = 16;
        for (i = 0; i < n; i++) {
            const byte *e = src + dirofs + 64 * i;
            byte *o = dst + pos + 64 * i;
            int flen = *(const int *)(e + 60);
            memcpy(o, e, 56);
            *(int *)(o + 56) = fp;
            *(int *)(o + 60) = flen;
            fp = (fp + flen + 15) & ~15;
        }
    }
    return pos + dirlen;
}

/* Flash emulation: the pack, realigned like the board's converter will lay
   it out, in its own pages at the board's data-region address, then marked
   read only so any write into "flash" faults on the PC instead of
   corrupting the board. Not counted as RAM. -noflash turns it off.
   A baked map image (engine/model.c, "QOGB") is mapped as it is, at the
   next free 64 KB boundary after the packs, the address it was built for. */
static byte *s_flash_next = (byte *)QOG_PC_FLASH_ADDR;   /* like the board's XIP data region */

void *Sys_FlashNext(void) { return s_flash_next; }

void *Sys_FlashMap(char *path, int length) {
    byte *next = s_flash_next;
    if (COM_CheckParm("-noflash") || length <= 0) return NULL;

    byte *raw = malloc((size_t)length);
    FILE *f = fopen(path, "rb");
    if (!raw || !f || fread(raw, 1, (size_t)length, f) != (size_t)length) {
        if (f) fclose(f);
        free(raw);
        return NULL;
    }
    fclose(f);
    int size = realign_pack(raw, length, NULL);
    const int image = !size && length >= 4 && !memcmp(raw, "QOGB", 4);
    if (image) size = length;
    if (!size) { free(raw); return NULL; }   /* not a pack or an image: load it the normal way */
    /* QuakeOG: an image built for a later address (qog_bake -bakebase, tools/mkboard.py
       --bake-base) goes there, so the PC can run the board's data layout */
    if (image && length >= 16) {
        unsigned base = *(unsigned *)(raw + 12);
        if (base > (unsigned)(size_t)next && !(base & 0xFFFF)) next = (byte *)(size_t)base;
    }

    byte *p = VirtualAlloc(next, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) p = VirtualAlloc(NULL, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) { free(raw); return NULL; }
    if (p == next) s_flash_next = next + ((size + 0xFFFF) & ~0xFFFF);
    if (image) memcpy(p, raw, (size_t)length);
    else realign_pack(raw, length, p);
    free(raw);
    DWORD old;
    VirtualProtect(p, (SIZE_T)size, PAGE_READONLY, &old);
    printf("Flash: %s mapped read only at %p, %d KB%s\n", path, (void *)p, size / 1024,
           image ? ", baked map image" : ", files 16-byte aligned");
    return p;
}

/* ---- system ---------------------------------------------------------- */

void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length) { (void)startaddr; (void)length; }
void Sys_LowFPPrecision(void) {}
void Sys_HighFPPrecision(void) {}
void Sys_SetFPCW(void) {}
void Sys_Sleep(void) { Sleep(1); }
char *Sys_ConsoleInput(void) { return NULL; }

void Sys_DebugLog(char *file, char *fmt, ...) {
    va_list ap;
    FILE *f = fopen(file, "a");
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

void Sys_Printf(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

void Sys_Error(char *error, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, error);
    vsnprintf(text, sizeof text, error, ap);
    va_end(ap);
    fprintf(stderr, "Sys_Error: %s\n", text);
    if (!COM_CheckParm("-nomsgbox"))   /* scripted runs must not block on a dialog */
        MessageBoxA(NULL, text, "QuakeOG PC: Sys_Error", MB_OK | MB_ICONERROR);
    exit(1);
}

void Sys_Quit(void) {
    Host_Shutdown();
    exit(0);
}

double Sys_FloatTime(void) {
    static LARGE_INTEGER freq, start;
    LARGE_INTEGER now;
    if (!freq.QuadPart) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

/* Sys_SendKeyEvents lives in vid_pc.c with the window. */

int main(int argc, char **argv) {
    quakeparms_t parms;
    memset(&parms, 0, sizeof parms);
    setvbuf(stdout, NULL, _IONBF, 0);   /* logs survive a killed or crashed run */

    COM_InitArgv(argc, argv);
    parms.argc = com_argc;
    parms.argv = com_argv;

    /* QuakeOG (Agent T): -flashbase <address> maps the packs from another
       address, 0x10300000 for the main chip's server data region
       (platform/qog_flashmap.h, tools/mkboard.py --main) */
    int i = COM_CheckParm("-flashbase");
    if (i && i + 1 < com_argc)
        s_flash_next = (byte *)(size_t)strtoul(com_argv[i + 1], NULL, 0);

    int heap_kb = DEFAULT_HEAP_KB;
    i = COM_CheckParm("-heapsize");
    if (i && i + 1 < com_argc) heap_kb = Q_atoi(com_argv[i + 1]);
    parms.memsize = heap_kb * 1024;
    /* Fixed address (and a fixed-base exe, see pc/CMakeLists.txt) so every
       run lays memory out the same way: qog_statehash is then repeatable,
       and pointer bugs reproduce. Falls back to anywhere if it is taken. */
    parms.membase = VirtualAlloc((void *)QOG_PC_HUNK_ADDR, (SIZE_T)parms.memsize,
                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!parms.membase) {
        printf("QuakeOG PC: fixed hunk address unavailable, layout will vary per run\n");
        parms.membase = malloc(parms.memsize);
    }
    if (!parms.membase) Sys_Error("could not allocate %d KB", heap_kb);

    i = COM_CheckParm("-basedir");
    parms.basedir = (i && i + 1 < com_argc) ? com_argv[i + 1] : QOG_DEFAULT_BASEDIR;
    parms.cachedir = NULL;

    printf("QuakeOG PC: basedir %s, hunk %d KB\n", parms.basedir, heap_kb);
#ifdef QOG_SPLIT_SERVER
    /* The split game's server half (engine/net_link.c): each Host_Frame waits
       for the client half's frame, so it runs as fast as the client asks. */
    Host_Init(&parms);
    for (;;)
        Host_Frame(0.0f);
#else
    if (COM_CheckParm("-qogfly")) {   /* renderer-only fly-through (port/pc/fly_pc.c), never returns */
        extern void qog_fly_pc_main(quakeparms_t *parms);
        qog_fly_pc_main(&parms);
    }
    Host_Init(&parms);   /* qog_mem is registered from VID_Init: commands must exist before init ends */

    int frame = 0;
    double oldtime = Sys_FloatTime();
    for (;;) {
        double newtime = Sys_FloatTime();
        double dt = newtime - oldtime;
        /* Call Host_Frame only when a frame is due (id caps at 72 fps).
           _Host_Frame calls rand() on every call, even ones Host_FilterTime
           then skips, so calling it every millisecond made the random
           sequence depend on PC speed. Now it depends on frames only. */
        if (dt < 1.0 / 72.0) { Sleep(1); continue; }
        qog_aim_update(newtime);
        Host_Frame((float)dt);
        {
            extern void qog_tour_frame(void);
            qog_tour_frame();   /* PC test tool, idle unless qog_tour runs */
            extern void qog_fly_frame(void);
            qog_fly_frame();    /* PC test tool, idle unless qog_fly_ref runs */
        }
        /* host_framecount only advances on frames that really ran (Host_Frame
           skips calls to cap the rate), and the first real one runs quake.rc
           and config.cfg. A plain call counter fired too early. */
        if (!frame && host_framecount >= 1) { frame = 1; qog_aim_apply_defaults(); }
        oldtime = newtime;
    }
#endif
}
