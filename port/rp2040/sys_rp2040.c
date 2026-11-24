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
 * (docs/fit-plan.md).
 *
 * QuakeOG (Agent R): built with QOG_SYS_MAIN it is the main CPU's layer for
 * the split game's server half (apps/quake/main): the pack and the baked
 * image are the server's copy in main's own flash (QOG_MAIN_DATA_XIP_ADDR),
 * main() boots the display CPU first, and every wait kicks the watchdog.
 * Saves go to main's FAT volume (Agent T, "saves on main's FAT volume"
 * below); tools/mkboard.py --main writes the server's data region. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "quakedef.h"
#include "pico/stdlib.h"
#ifdef QOG_SYS_MAIN
#include "fwog_main.h"
#else
#include "fwog_display.h"
#include "qog_aim.h"
#endif
#include "qog_flashmap.h"

#ifdef QOG_SYS_MAIN
#define QOG_SYS_DATA_ADDR	QOG_MAIN_DATA_XIP_ADDR
#define QOG_SYS_DATA_MAX	QOG_MAIN_DATA_MAX_SIZE
#else
#define QOG_SYS_DATA_ADDR	QOG_DATA_XIP_ADDR
#define QOG_SYS_DATA_MAX	QOG_DATA_MAX_SIZE
#endif

#ifndef QOG_HUNK_KB
#define QOG_HUNK_KB 64
#endif

qboolean isDedicated = false;
static byte s_hunk[QOG_HUNK_KB * 1024] __attribute__((aligned(16)));

/* QuakeOG: the hunk, for a main() in another file (apps/qogfly, built with
   QOG_SYS_NO_MAIN). */
void *qog_rp2040_hunk(int *size) { *size = (int)sizeof s_hunk; return s_hunk; }

#ifdef QOG_SYS_MAIN
/* BSP contract rule 7: the main loop kicks the watchdog (and so does every
   wait of the link, port/rp2040/link_rp2040.c). */
FWOG_WATCHDOG_DEFAULT();
#else
/* BSP contract rule 2: red held 6 s powers the board off. The main loop
   calls fwog_power_poll every iteration (and Sys_Error keeps doing so). */
FWOG_POWER_DEFAULT();
#endif

/* newlib's remove() wants this; there is no filesystem to delete from. */
int _unlink(const char *path) { (void)path; return -1; }

#ifdef QOG_SYS_MAIN
/* ---- saves on main's FAT volume ------------------------------------------
 *
 * QuakeOG (Agent T): the server half saves and loads with id's stdio calls
 * (fopen, fprintf, fscanf, fgetc in host_cmd.c and pr_edict.c, and the
 * client menus' save list in engine/net_link.c). newlib turns them into the
 * system calls below, which keep the file in the folder "quake" of main's
 * FAT volume through the BSP's API (bsp/main_cpu/fs/fwog_fs.h): the engine's
 * "./id1/s0.sav" is "quake/s0.sav". One file at a time, as the BSP allows.
 *
 * The BSP's flash write hazards: core1 never runs on main in this app (no
 * multicore launch anywhere in quake_main), and the BSP's disk driver kicks
 * the watchdog after every chunk of at most 8 sectors it writes (FatFs
 * writes one 4 KB sector at a time here), so a long save cannot outlive the
 * 8.3 s window. The volume is mounted at the first open and never formatted
 * from here: without a volume a save fails with id's "couldn't open". */
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "fs/fwog_fs.h"

#define FAT_FD  3
#define FAT_DIR "quake"
static int s_fat_state;     /* 0 not tried, 1 mounted, -1 no volume */
static int s_fat_open;

static int fat_ready(void) {
    if (!s_fat_state) {
        s_fat_state = fwog_fs_mount() ? 1 : -1;
        if (s_fat_state > 0 && !fwog_fs_exists(FAT_DIR) && !fwog_fs_mkdir(FAT_DIR))
            DIAG("[quake_main] cannot make the folder %s on the FAT volume\n", FAT_DIR);
        DIAG("[quake_main] FAT volume %s\n", s_fat_state > 0 ? "mounted, saves in quake/" : "missing, no saves");
    }
    return s_fat_state > 0;
}

int _open(const char *path, int flags, ...) {
    char name[64];
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!*base || strlen(base) + sizeof FAT_DIR + 1 > sizeof name || s_fat_open || !fat_ready()) {
        errno = ENOENT;
        return -1;
    }
    snprintf(name, sizeof name, FAT_DIR "/%s", base);
    if (!fwog_fs_open(name, (flags & O_ACCMODE) != O_RDONLY, (flags & O_APPEND) != 0)) {
        errno = ENOENT;
        return -1;
    }
    s_fat_open = 1;
    return FAT_FD;
}

int _close(int fd) {
    if (fd != FAT_FD || !s_fat_open) return -1;
    s_fat_open = 0;
    return fwog_fs_close() ? 0 : -1;
}

int _read(int fd, char *buf, int len) {
    if (fd == 0) return stdio_get_until(buf, len, at_the_end_of_time);   /* as the SDK's */
    if (fd != FAT_FD || !s_fat_open) return -1;
    size_t n = (size_t)len;
    return fwog_fs_read(buf, &n) ? (int)n : -1;
}

int _write(int fd, char *buf, int len) {
    if (fd == 1 || fd == 2) {   /* as the SDK's */
        stdio_put_string(buf, len, false, true);
        return len;
    }
    if (fd != FAT_FD || !s_fat_open) return -1;
    return fwog_fs_write(buf, (size_t)len) ? len : -1;
}

off_t _lseek(int fd, off_t pos, int whence) {
    if (fd != FAT_FD || !s_fat_open) return -1;
    if (whence == SEEK_CUR) pos += (off_t)fwog_fs_tell();
    else if (whence == SEEK_END) pos += (off_t)fwog_fs_size();
    if (pos < 0 || !fwog_fs_seek((uint32_t)pos)) return -1;
    return pos;
}

int _fstat(int fd, struct stat *st) {
    if (fd != FAT_FD || !s_fat_open) return -1;
    memset(st, 0, sizeof *st);
    st->st_mode = S_IFREG;
    st->st_size = (off_t)fwog_fs_size();
    st->st_blksize = 512;   /* newlib's buffer for the file (BUFSIZ 1024 otherwise) */
    return 0;
}

int _isatty(int fd) { return fd >= 0 && fd <= 2; }
#endif	/* QOG_SYS_MAIN */

/* ---- the pack in flash ------------------------------------------------ */

#define PAK_HANDLE 1
static const byte *const s_pak = (const byte *)QOG_SYS_DATA_ADDR;
static int s_pakpos;

static int pak_length(void) {
    if (memcmp(s_pak, "PACK", 4)) return -1;
    int dirofs = *(const int *)(s_pak + 4), dirlen = *(const int *)(s_pak + 8);
    if (dirofs < 12 || dirlen < 0 || dirofs + dirlen > (int)QOG_SYS_DATA_MAX) return -1;
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
    for (unsigned ofs = ((unsigned)pak + 0xFFFFu) & ~0xFFFFu; ofs + 64 <= QOG_SYS_DATA_MAX; ofs += 0x10000u) {
        const unsigned *h = (const unsigned *)(s_pak + ofs);
        if (h[0] != 0x42474F51u) continue;          /* "QOGB", QOGB_MAGIC in engine/model.h */
        if (h[4] < 64 || h[4] > QOG_SYS_DATA_MAX - ofs) break;
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
#ifdef QOG_SYS_MAIN
    /* Report for 20 s, then stop kicking: the watchdog resets main, and
       main resets the display through GUI_NRESET, a clean restart of both. */
    for (int k = 0; k < 20; k++) {
        DIAG("Sys_Error: %s\n", text);
        board_watchdog_kick();
        sleep_ms(1000);
    }
    for (;;)
        tight_loop_contents();
#else
    /* Keep the red-button power-off alive while reporting (BSP rule): the
       display CPU has no watchdog and must stay switch-off-able. */
    for (;;) {
        DIAG("Sys_Error: %s\n", text);
        for (int i = 0; i < 500; i++) {
            fwog_power_poll(to_ms_since_boot(get_absolute_time()));
            sleep_ms(2);
        }
    }
#endif
}

void Sys_Quit(void) { Sys_Error("quit"); }

double Sys_FloatTime(void) { return (double)time_us_64() * 1e-6; }

#ifdef QOG_SYS_MAIN
/* QuakeOG (Agent R): the server half on the main CPU. Main is the CPU that
   boots the display (fwog_display_update_run, which also brings the link up
   and releases GUI_NRESET), then each Host_Frame waits for the client half's
   frame on the link, kicking the watchdog while it waits. */
#ifdef QOG_STACK_KB
static uint32_t s_stack[QOG_STACK_KB * 256] __attribute__((aligned(8)));
static void __attribute__((naked, noreturn, noinline)) run_on_stack(void *top, void (*fn)(void)) {
    __asm volatile ("mov sp, r0\n\tbx r1\n");
}
#endif
static void __attribute__((noreturn)) server_main(void) {
    static char *argv[] = { "quake", "-nosound" };
    quakeparms_t parms;

    memset(&parms, 0, sizeof parms);
    COM_InitArgv(2, argv);
    parms.argc = com_argc;
    parms.argv = com_argv;
    parms.basedir = ".";
    parms.membase = s_hunk;
    parms.memsize = sizeof s_hunk;
    Host_Init(&parms);          /* waits for the display's HELLO (qog_link_start) */
    for (;;) {
        board_watchdog_kick();
        Host_Frame(0.0f);       /* the client half sets the frame time */
    }
}

int main(void) {
    board_init();
    fwog_display_result_t d = fwog_display_update_run();
    DIAG("[quake_main] display: %s\n", fwog_display_result_text(d));
#ifdef QOG_STACK_KB
    run_on_stack(s_stack + sizeof s_stack / sizeof s_stack[0], server_main);
#else
    server_main();
#endif
}
#endif	/* QOG_SYS_MAIN */

/* Buttons for Sys_SendKeyEvents (vid_rp2040.c): the main loop polls them
   through fwog_power_poll, the only poll allowed (docs/lessons.md L5). */
uint8_t qog_rp2040_buttons;

#if !defined(QOG_SYS_NO_MAIN) && !defined(QOG_SYS_MAIN)
#ifdef QOG_STACK_KB
/* QuakeOG: the game's stack, in main RAM so the link counts it (the SDK's
   default is 2 KB in a scratch bank). Sized from the deepest call chain
   that tools/stack_chain.py finds (apps/quake/CMakeLists.txt). Thread mode
   runs on MSP, so interrupts use this stack too, as in apps/qogfly. */
static uint32_t s_stack[QOG_STACK_KB * 256] __attribute__((aligned(8)));

static void __attribute__((naked, noreturn, noinline)) run_on_stack(void *top, void (*fn)(void)) {
    __asm volatile ("mov sp, r0\n\tbx r1\n");
}

static void __attribute__((noreturn)) game_main(void);

int main(void) {
    board_init();
    run_on_stack(s_stack + sizeof s_stack / sizeof s_stack[0], game_main);
}

static void __attribute__((noreturn)) game_main(void) {
#else
int main(void) {
#endif
    static char *argv[] = { "quake", "-nosound" };
    quakeparms_t parms;

#ifndef QOG_STACK_KB
    board_init();
#endif
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
