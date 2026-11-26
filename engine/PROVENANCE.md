# Engine provenance

The baseline commit of this folder is an **unmodified** copy of id Software's GPL Quake source. Every later change shows up as a git diff against it.

| | |
| --- | --- |
| Source | https://github.com/id-Software/Quake |
| Commit | `bf4ac42` (Jan 31 2012 GitHub upload of the 1999 GPL release) |
| Subtree | `WinQuake/`, the software-rendered single-player engine |
| Licence | GPL v2 or (at your option) any later version, see `COPYING` and each file header |

## What was taken

- The portable engine files from id's own software-renderer build list (`SQUAKE_OBJS` in `Makefile.linuxi386`), minus the Linux platform files and the x86 assembly.
- id's own stubs, used instead of real platform code where we don't need it yet. `cd_null.c`, `snd_null.c`, `in_null.c`, `net_none.c` (loopback only, no networking).
- The sound mixer (`snd_dma.c`, `snd_mem.c`, `snd_mix.c`) for later.
- Every header from `WinQuake/`. Unused ones get deleted in their own commits.

## Not taken

GLQuake (`gl_*`), QuakeWorld (`QW/`), all `.s` assembly, every DOS, Linux, Sun and Win32 platform file, networking drivers, and the SciTech MGL and DirectX SDK folders.

## Rules for changing anything here

- One purpose per commit, with a message that says what and why.
- Code copied from MG24Quake (GPL-3.0) or RP2350-Quake (GPL-3.0) gets a comment naming the source file, and a line in `docs/engine-files.md`. Once any such code lands, the combined work is GPL-3.0.
- Platform code does not go here. It lives in `port/` and implements id's own interfaces (`Sys_*`, `VID_*`, `IN_*`, `SNDDMA_*`).
