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

    int heap_kb = DEFAULT_HEAP_KB;
    int i = COM_CheckParm("-heapsize");
    if (i && i + 1 < com_argc) heap_kb = Q_atoi(com_argv[i + 1]);
    parms.memsize = heap_kb * 1024;
    parms.membase = malloc(parms.memsize);
    if (!parms.membase) Sys_Error("could not allocate %d KB", heap_kb);

    i = COM_CheckParm("-basedir");
    parms.basedir = (i && i + 1 < com_argc) ? com_argv[i + 1] : QOG_DEFAULT_BASEDIR;
    parms.cachedir = NULL;

    printf("QuakeOG PC: basedir %s, hunk %d KB\n", parms.basedir, heap_kb);
    Host_Init(&parms);   /* qog_mem is registered from VID_Init: commands must exist before init ends */

    int frame = 0;
    double oldtime = Sys_FloatTime();
    for (;;) {
        double newtime = Sys_FloatTime();
        double dt = newtime - oldtime;
        if (dt < 0.001) { Sleep(1); continue; }
        qog_aim_update(newtime);
        Host_Frame((float)dt);
        /* host_framecount only advances on frames that really ran (Host_Frame
           skips calls to cap the rate), and the first real one runs quake.rc
           and config.cfg. A plain call counter fired too early. */
        if (!frame && host_framecount >= 1) { frame = 1; qog_aim_apply_defaults(); }
        oldtime = newtime;
    }
}
