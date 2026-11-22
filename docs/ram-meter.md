# The RAM meter

Fitting Quake into the RP2040 is mostly a RAM problem, so every change should move one number, visibly. The meter has two halves because Quake's memory comes in two kinds.

```
   all of Quake's RAM  =  static (globals, fixed arrays)      <- half 1, build time
                        + dynamic (hunk + cache + zone)        <- half 2, runtime
                        + stack
   must fit in about 245 KB on the display RP2040
```

## Half 1: static RAM, `tools/ram_meter.py`

```powershell
python tools\ram_meter.py            # writes build\ram-report.md
python tools\ram_meter.py --top 60   # longer hit list
```

It compiles every engine file with the **real RP2040 compiler** (the Pico extension's `arm-none-eabi-gcc`, for a Cortex-M0+). Then it reads each global's size from the object files. The sizes are what the board would really use, including its struct padding. No linking or hardware is needed.

**First reading, unmodified id engine: 435.3 KB** (the budget is 245 KB for everything).

| KB | Symbol | What it is |
| ---: | --- | --- |
| 107.8 | `cl_entities` | Client copy of all 600 entities |
| 97.0 | `mod_known` | Table of up to 256 loaded models, full structs |
| 32.0 | `newsky` | Sky texture work buffer |
| 23.0 | `cl_static_entities` | Torches and other static entities |
| 16.4 | `bottomsky`, `bottommask` | More sky buffers |
| 13.5 | `sv` | Server state |
| 11.5 | `cl_temp_entities` | Beams and explosions |
| 10.0 | `cl_efrags` | Entity-to-leaf links |

These are the first targets, and MG24Quake already shows how to shrink most of them (see `phase1-mg24quake-notes.md`).

## Half 2: dynamic RAM, the PC build

Run `build-pc\Release\quakeog_pc.exe`. The **title bar** shows it live:

```
dyn 1234 KB (peak 2345)  low 900 high 120 cache 200 zone 14/48 KB
```

| Field | Meaning |
| --- | --- |
| low | Hunk, bottom up: the level's BSP, models, `progs.dat`, edicts |
| high | Hunk, top down: per-level and temporary buffers |
| cache | Sounds, model skins and so on, in the gap between low and high |
| zone | Small heap for strings and small structs (fixed 48 KB) |
| peak | Highest total seen. Load a map, play, then read it |

Open the console with the backtick key and type `qog_mem` for a full report, or `qog_mem reset` to clear the peaks (for example right after a map loads).

**Finding the real minimum:** start with `-heapsize 1024` (KB) and lower it until the map fails to load. The smallest working value is the honest dynamic number for that map. The PC build uses 32-bit pointers on purpose, so these numbers carry over to the board.

## How to use it

1. Before a change: run both halves and note the numbers.
2. Make one change, for example shrinking `cl_entities`.
3. Run both again. Put the before and after in the commit description and in `docs/changes.md`.

Don't trust a reduction that hasn't been measured.
