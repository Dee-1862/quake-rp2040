# Keeping the structure lean

The danger in a port like this is growth: importing a whole engine, adding a layer for every problem, and ending up with a tree nobody can fit in their head, or in 256 KB. These rules keep the size flat. Each one has a check.

## 1. Three layers, one direction

```
apps/qog/display   (game loop glue, ~one file)
      │
engine/            (Quake code, trimmed; includes ONLY qog_*.h)
      │
platform/          (qog_video, qog_input, qog_data, later qog_sound, qog_save)
      │
third_party/wiliOGbsp  (BSP, untouched)
```

- The engine never includes `fwog_*.h` or Pico SDK headers. That keeps the BSP glue small and separable, which the licence question needs (GPL engine, BSP under the FreeWili Hardware License), and it lets the same engine build on a PC.
- **Check:** `grep -rn "fwog_\|pico/" engine/` returns nothing.

## 2. Build on the PC first, then on the board

Add a `platform/pc/` backend with the same `qog_*.h` API: SDL2 or a raw window for video, the keyboard for input, the QOGD image loaded from a file. Port and trim the engine on the PC, where you get a debugger, sanitisers and instant rebuilds. Flash the board only to measure speed. MG24Quake's authors used a Windows build first for the same reason.

- **Check:** every engine change builds and runs on the PC before it goes to the board.

## 3. Delete before you add

Start from a full Quake source, then remove whole subsystems before optimising anything. Candidates: GLQuake and the GL renderer, QuakeWorld, networking beyond a loopback, CD audio, the console's command history, the demo recorder, the PCX screenshot code, video mode lists, and the `cvar` features we don't use.

- **Check:** keep `docs/engine-files.md` with every engine file marked `kept`, `trimmed` or `deleted`, plus its origin (id GPL, MG24Quake GPL-3.0 or ours). The licence checklist needs this anyway.

## 4. One copy of every number

- The flash map lives in `platform/qog_flashmap.h`. `tools/check_app_size.py` reads it, and `tools/mkdata.py` must match it (both say so).
- The data format lives in `platform/qog_data.h`, mirrored by `tools/mkdata.py` with a test.
- Resolution, buffer sizes and cache sizes go in one `engine/qog_config.h`, not scattered `#define`s.

## 5. Budgets are enforced by the build, not by memory

- **Flash:** `check_app_size.py` already fails the build at overlap. Add a warning at 80%.
- **RAM:** add a post-build step that runs `arm-none-eabi-size` and fails if `bss + data` goes over the budget set in `qog_config.h`. Leave headroom for stack and heap.
- **Speed:** the `qog` console line reports `render/scan/wait` every second. Keep that format when the engine replaces the test pattern, so every phase's numbers are comparable.

## 6. Data lives in flash, read in place

- Never copy a lump into RAM just to read it. `qog_data_find()` returns an XIP pointer.
- Do conversions on the PC (`tools/`), not on the board: pre-swizzled textures, precomputed tables, QuakeC compiled to C. The device should only read.
- **Check:** at boot, the engine has no `malloc` of a size that depends on a lump's size.

## 7. One app, flags not forks

- Keep a single `apps/qog`. Variants such as resolution, test pattern and profiling are compile-time flags (`QOG_TEST_W`, ...), not copied apps.
- The Duke 3D and Battlezone fallbacks would be new folders under `engine/`, reusing `platform/` unchanged. That reuse is the point of the layer.

## 8. Third-party code is cloned, not committed

- `third_party/` is git-ignored. README lists the exact clone commands, and should pin commits once we depend on specific ones.
- Anything copied out of MG24Quake into `engine/` gets its origin noted in `docs/engine-files.md`.
