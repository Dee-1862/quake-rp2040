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

## Baseline: unmodified id engine, E1M1 (measured 2026-11-22)

Run with `quakeog_pc.exe +map e1m1 +wait +wait +wait +qog_mem +"qog_mem hunk" +qog_quit`, PC build at 320×200 with 32-bit pointers.

| Part | KB | Biggest pieces | How it shrinks (reference) |
| --- | ---: | --- | --- |
| Hunk low | 3,234 | **e1m1 map 1,772**, **edicts 513**, **progs 404**, gfx.wad 110, particles 88, net sockets 81, zone 48 | Map read in place from flash. Edicts sized per entity class. QuakeC compiled to C so `progs.dat` never loads. Fewer particles. No network sockets. (MG24Quake) |
| Cache | 1,419 | Sounds, models, skins | Read in place from flash, streamed (MG24Quake) |
| Video | 788 | **Surface cache 600**, z-buffer 125, frame 63 | No surface cache: light per pixel at draw time. 240×150 buffers. Z-buffer shared with edge tables (MG24Quake) |
| Hunk high | 10 | | |
| Static | 435 | `cl_entities` 108, `mod_known` 97, sky 65 | Smaller tables, 16-bit indices, sky prerendered in flash |
| **Total** | **≈ 5,886** | | **Target: under 245** |

The total is about **24× over budget**, which is roughly where MG24Quake started too. Nearly 70% of it (the map, the cache, the surface cache and `progs.dat`) is data, which becomes flash reads rather than RAM.

## Progress

| Step | Total KB | Change | Proof it still plays the same |
| --- | ---: | ---: | --- |
| Baseline, unmodified id engine | ≈ 5,886 | | |
| Level data read in place from flash | 5,091 | −795 | E1M1 loads. Read-only flash pages caught no writes |
| No surface cache, per-pixel lighting | 4,492 | −599 | Same frame as id: 96.3% of pixels identical, the rest within 5/255 |
| QuakeC compiled to C, `progs.dat` and aligned map arrays in flash | **≈ 3,923** | −569 | State hashes identical to id's interpreter after 21 s of E1M1, monsters included |

Now (E1M1): hunk low 1,848 KB (map structures 781, edicts 513, gfx.wad 110, particles 88, net sockets 81, zone 48, QuakeC globals 45), cache 1,419 KB (models and sounds), video 189 KB, static 435 KB. **One third of the total gone so far, with gameplay proven unchanged.**

Next targets, biggest first: the model and sound cache (1,419 KB → read from flash), edicts (513 KB → per-class sizes), static tables (435 KB), and the map structures (781 KB → split into a read-only part in flash and a small mutable part in RAM).

## How to use it

1. Before a change: run both halves and note the numbers.
2. Make one change, for example shrinking `cl_entities`.
3. Run both again. Put the before and after in the commit description and in `docs/changes.md`.

Don't trust a reduction that hasn't been measured.
