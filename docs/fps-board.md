# FPS scoreboard, timedemo demo3 on the board

Kept by earlier work, the benchmark referee. Goal, 35 fps in the board's Benchmark (timedemo demo3), a 28.5 ms frame. Board numbers are measured on hardware. Model numbers come from `tools/boardbench.ps1` (docs/handoff.md), which plays demo3 on the PC with the board client's render flags, counts the work of every code path per frame on both cores, and turns the counts into board milliseconds with `tools/boardmodel.py`. Since the d31520e board run the model is calibrated on that run (section 3).

## 1. Measured on the board

| Commit | Mode | Clock, flash, LCD | demo3 fps | frame ms | Notes |
| --- | --- | --- | ---: | ---: | --- |
| before the speed round | Fast | 250, 125, SPI 62.5 | 9.3 | 107.5 | view 75.7 ms (the earlier figures) |
| b585365 | Fast | 250, 125, SPI 62.5 | 13.6 | 73.3 | 1090 frames in 79.9 s. E1M1 standing 18.0 fps (frame 55.2, view 53.2, race 6.0, lmap 2.0) |
| **d31520e** | **Fast** | 300, 150, PIO 60 | **20.0** | **49.9** | 1090 frames in 54.4 s |
| **d31520e** | **Turbo** | 300, 150, PIO 60 | **22.3** | **44.8** | 1090 frames in 48.8 s |
| f4dfb75 | Fast | 300, 150, PIO 60 | 22.0 | 45.5 | 1090 frames in 49.6 s, log `run10_8.txt` |
| f4dfb75 | Turbo | 300, 150, PIO 60 | 26.0 | 38.4 | 41.9 s, log `run10_8.txt` |
| 5dc4518 + HOTTEXT | Turbo | 300, 150, PIO 60 | 27.1 | 36.9 | 40.2 s, log `run11_8.txt` |
| f96f85f + HOTTEXT | Turbo | 300, 150, PIO 60 | 27.8 | 36.0 | 39.2 s, log `run12_8.txt` |
| a95562b + HOTTEXT | Exact | 300, 150, PIO 60 | 12.8 | 78.3 | 85.3 s, log `run13_8.txt` |
| **a95562b + HOTTEXT** | **Fast** | 300, 150, PIO 60 | **24.8** | **40.3** | 43.9 s, log `run13_8.txt` |
| **a95562b + HOTTEXT** | **Turbo** | 300, 150, PIO 60 | **27.8** | **36.1** | 39.3 s, log `run13_8.txt` |
| a95562b + HOTTEXT + XIPSTATS | Fast | 300, 150, PIO 60 | 24.3 | 41.2 | cache counting build, log `run14_8.txt` |
| **ebc51eb + HOTTEXT** | **Fast** | 300, 150, PIO 60 | **25.5** | **39.2** | one shared copy of the drawing code, 42.7 s, log `run15_8.txt` |
| **ebc51eb + HOTTEXT** | **Turbo** | 300, 150, PIO 60 | **28.5** | **35.1** | same build, 38.3 s, log `run15_8.txt` |
| **c2b913a + HOTTEXT + SCANSIZE** | **Fast** | 300, 150, PIO 60 | **25.8** | **38.7** | cheaper brush models (3.4 to 2.9 ms), edge scan for size made no difference (6.9 and 7.0 ms), 42.2 s, log `run16_8.txt` |
| **c2b913a + HOTTEXT + SCANSIZE** | **Turbo** | 300, 150, PIO 60 | **28.8** | **34.8** | same build, 37.9 s, log `run16_8.txt` |
| **1765d6b + HOTTEXT + SCANSIZE + GUNIMAGES** | **Fast** | 300, 150, PIO 60 | **25.8** | **38.8** | data images built with `--gun-images`, Fast does not use them, 42.3 s, log `run17_8.txt` |
| **1765d6b + HOTTEXT + SCANSIZE + GUNIMAGES** | **Turbo** | 300, 150, PIO 60 | **30.6** | **32.7** | flat view weapon (vmodel 2.3 to 0.3 ms), 35.6 s, log `run17_8.txt` |
| **9264faf + HOTTEXT + SCANSIZE + GUNIMAGES + RAMSCAN** | **Fast** | 300, 150, PIO 60 | **26.4** | **37.8** | fewer float calls and the hottest edge scan code in RAM, 41.2 s, log `run19_8.txt` |
| **9264faf + HOTTEXT + SCANSIZE + GUNIMAGES + RAMSCAN** | **Turbo** | 300, 150, PIO 60 | **31.6** | **31.7** | same build, 34.5 s, log `run19_8.txt` |

Every run is `timedemo demo3`, 1090 frames, read from the display chip's serial log. The logs themselves are not in the repository. Commit IDs are those of this repository.

9264faf against 1765d6b, frame weighted means of the `[time cl] views` lines in each run, ms per view. The two changes were measured together, so the split between them is inferred. Edge scan went from 6.8 to 6.0 in Turbo and 7.1 to 6.3 in Fast, models from 3.5 to 3.3 and 4.0 to 3.7. Spans rose by 0.3 in Turbo and 0.2 in Fast. The client stack had 4,324 B free at its lowest, 5,368 B before.

b585365 phases, ms per view, spans 16.9, lmap 13.9, world 10.4, edgescan 7.2, bmodels 6.9, models 5.1, vmodel 3.4, grad 3.1, parts 1.2, total view 72.2.

d31520e, frame weighted means of the `[time cl]` lines during the demo (the window with the load left out, log `run9_8.txt`). lmap, grad and spans are core 0's share of the surfaces, `pipe` is core 1's drawing.

| | Fast | Turbo |
| --- | ---: | ---: |
| frame | 49.5 | 44.3 |
| input, link, read, sound | 2.6 | 2.5 |
| view (V_RenderView) | 41.5 | 35.9 |
| scanwait (whole scanout waits) | 6.2 | 7.3 |
| race (row waits) | 1.7 | 0.0 |
| world | 9.08 | 8.65 |
| bmodels | 3.35 | 3.38 |
| edgescan (with c0wait) | 6.48 | 6.13 |
| lmap + grad + spans, core 0 | 1.53 + 1.15 + 8.14 | 1.13 + 0.89 + 5.28 |
| models, vmodel, parts | 3.99, 2.69, 0.90 | 3.50, 2.40, 0.90 |
| setup, sky, turb, zspans | 0.77 | 0.61 |
| core 1, pipe, wait, c0wait | 15.5, 2.3, 0.5 | 11.8, 1.8, 0.2 |
| status bar rows sent | 22% of frames | 22% of frames |
| scan, 152 rows / 200 rows | 13.05 / 17.15 ms | the same |

What drives scanwait, windows with notify text (a pickup message in the last 3 s) show 9 to 13 ms of scanwait a frame, windows with status bar frames and no notify text 0 to 1 ms. The late windows with every frame's status bar resent also had "You got the silver runekey" on screen.

### Phases of the shared drawing build and cache misses

Frame weighted means over the benchmark, ms per frame, from the `[time cl]` lines of log `run15_8.txt`.

| Mode | frame | view | link | scanwait | race |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fast | 38.8 | 36.1 | 0.6 | 0.0 | 1.1 |
| Turbo | 34.6 | 31.9 | 0.5 | 0.7 | 0.4 |

| Mode | world | bmodels | edgescan | lmap | grad | spans | models | vmodel | parts |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Fast | 7.7 | 3.4 | 6.9 | 1.2 | 0.8 | 6.8 | 3.9 | 2.6 | 0.6 |
| Turbo | 7.5 | 3.5 | 6.7 | 0.9 | 0.6 | 4.3 | 3.5 | 2.4 | 0.7 |

XIP cache misses per view in Fast, from the counting build made before the shared drawing code (log `run14_8.txt`). Both cores are counted together.

| world | bmodels | edgescan | lmap | grad | spans | models | vmodel | parts | total |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 11172 | 8260 | 12363 | 5146 | 3444 | 17806 | 10160 | 6408 | 501 | 78795 |

### Profile of the Turbo benchmark

Board PC sampling (`QOG_PROF`, 4 kHz a core, 7 windows of 5 s, log `run18_8.txt`), the build of the 30.6 fps run plus the profiler, which ran at 30.3 fps. Share of all samples on that core, waits included.

| Core 0 | share |
| --- | ---: |
| boot ROM float routines | 11.6% |
| not identified | 7.1% |
| R_GenerateSpans | 6.9% |
| litq_span_asm | 5.3% |
| R_QogClipDistFx | 4.5% |
| R_RenderFace | 4.1% |
| qog_video_wait | 3.1% |
| R_QogProjectFast | 2.8% |
| litq_zruns | 2.7% |

Largest callers of the float routines on core 0 are R_QogZFrontC (1.4%), R_QogClipEdgeFast (0.8%) and RecursiveLightPoint (0.7%), with 4.0% from callers not identified. Core 1 spent 63% in `core1_main`, which holds both the screen send and its waiting, so the samples do not say how much of that is free.

## 2. Model predictions per commit (demo3, board fps at 300 MHz)

| Commit | Calibration | Fast | Turbo | Board |
| --- | --- | ---: | ---: | --- |
| dd0f819 | b585365 | 18.0 | 20.6 | not measured |
| ff5bd24 + an earlier branch | b585365 | 22.6 | 26.5 | not measured |
| d31520e | b585365 | 26.5 (24.9 to 27.4) | 30.4 (27.6 to 31.6) | **20.0 / 22.3, the model was 33% and 36% high** |
| d31520e | d31520e Fast (now) | 20.0 (by construction) | **22.8** (board 22.3, 2% high) | |

d31520e with the new calibration, ms per view (world with its race wait).

| Phase | Fast board | Fast model | Turbo board | Turbo model |
| --- | ---: | ---: | ---: | ---: |
| world | 9.08 | 9.0 | 8.65 | 8.6 |
| bmodels | 3.35 | 3.4 | 3.38 | 3.4 |
| edgescan | 6.48 | 6.2 + 0.3 c0wait | 6.13 | 6.2 + 0.2 |
| surfaces on core 0 | 10.82 | 10.8 | 7.30 | 6.9 |
| models, vmodel, parts | 3.99, 2.69, 0.90 | 4.0, 2.7, 0.9 | 3.50, 2.40, 0.90 | 3.3, 2.0, 0.9 |
| scanout waits | 8.0 (race + scanwait) | 8.3 | 7.3 | 8.6 |
| frame | 49.9 | 49.9 | 44.8 | 43.9 |

The Turbo column is a check, only the Fast run was used to calibrate.

## 3. What the board run changed in the model

- The waits were right, the compute was too cheap. The scanout waits the code implies (row waits, the edge table at the status bar rows, the whole scanout after R_RenderWorld, notify frames) need a scale of only 1.05 to match the board's frame, so the three reconciliations of the b585365 calibration (waits x 0.31 and the others) are gone. The compute was underpriced for the later exact paths, residuals bmodels x 2.28, edgescan x 1.35, surface work x 1.30, world x 1.10, models x 1.12 against the b585365 fit, setup, sky and turb x 2.5. Core 1 draws at 0.98 of core 0's speed.
- The board's split of the waits is like the model's, scanwait holds notify frames, the edge table's wait at the status bar rows and the wait after the world, race only partial row waits.
- The time around the view is measured now (input, link, read, sound 2.6 ms, present and status bar 0.5 ms), not fitted.
- Ranges for d31520e itself are narrow by construction. For a branch they come from its changed work (weights scaled at random and refitted on both board runs).
- `python tools/boardmodel.py calibrate-board --calib tools/boardmodel_calib_de58316.json --log tools/boardmodel_f7d9450_fast.csv.gz -o tools/boardmodel_calib.json` makes the calibration again (`BOARD_F7` in the script holds the board numbers).

## 4. Remaining gap to 35 fps (28.5 ms) in Turbo

Turbo is 44.8 ms on the board, 16.3 ms to go (the model's 43.9, 15.4 to go).

| Item on core 0's clock | Turbo now | after the scanwait fix (race package with the status bar cap) | budget at 35 fps |
| --- | ---: | ---: | ---: |
| scanout waits | 8.6 | about 1.9 | 1.5 |
| input, link, read, sound, present, status bar | 3.0 | 3.0 | 3.0 |
| world | 8.6 | 8.6 | 5.5 |
| edgescan and c0wait | 6.4 | 6.4 | 4.3 |
| surfaces on core 0 | 6.9 | 6.9 | 6.0 |
| bmodels | 3.4 | 3.4 | 2.2 |
| models + vmodel | 5.3 | 5.3 | 4.5 |
| parts, other | 1.5 | 1.5 | 1.5 |
| frame | 43.9 | 37.2 | 28.5 |

After the scanwait is fixed about 8.7 ms of core 0's compute remains to go, world and the edge scan are the two biggest items (15 ms together), then the surfaces core 0 draws, models and brush models.

## 5. Next attacks for 35 fps in Turbo, ranked by expected ms saved per unit of risk

Model what-ifs on the d31520e calibration, 300 MHz, PIO 60 MHz, ms per frame (`tools\boardbench.ps1 -WhatIf "label=item,item;..."`). Risk, 1 low (exact, small code), 2 medium, 3 high.

| Rank | Attack | Turbo ms | Fast ms | Risk | ms per risk |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | **Exact race package**, SCR_SetUpToDrawConsole clears the notify rows after a ScanFree of those rows only (not the whole scanout), the status bar cap, the edge table's status bar wait as a row wait, R_DrawBEntitiesOnList before the scanout-end wait | 6.7 (notify alone 1.9, notify with the cap 4.2) | 4.4 | 1 | 6.7 |
| 2 | World front end x 0.5 (BSP walk on core 1, which idles after its scanout) | 2.6 (2.4 more after 1) | 3.4 | 2 | 1.2 |
| 3 | Brush models x 0.5 | 1.6 | 1.6 | 1.5 | 1.1 |
| 4 | Edge scan x 0.5 (the scan split over both cores, a sorter and a span maker a line behind) | 1.8 (1.9 more after 1 and 2) | 1.9 | 2 | 0.95 |
| 5 | Span pixels x 0.7 | 1.9 (2.1 more after 1, 2, 4) | 3.0 | 2 | 0.95 |
| 6 | Turbo status bar cap at 10 a second alone (approved, earlier work building it) | 0.9 (5 a second 1.2) | 0 | 1 | 0.9 |
| 7 | Alias front end and particles on core 1 (models + vmodel x 0.7) | 1.5 | 1.9 | 2 | 0.75 |
| 8 | No scanout waits at all (two packed views, a home for the Turbo z-buffer) | 1.9 after 1 (8.6 alone) | n/a | 3 | 0.6 |
| 9 | lmap + grad x 0.5 | 1.1 | 1.3 | 2 | 0.55 |

Combined, Turbo / Fast fps, 1 alone 26.9 / 22.0. 1 + 2, 28.7 / 23.6. 1 + 2 + 4, 30.4 / 24.8. 1 + 2 + 4 + 5, 32.5 / 26.9. 1, 2, 3, 4, 5 and 7, **34.9 / 29.4**. With no waits at all on top, 42.1 / 36.4. So 35 fps in Turbo needs the race package and about 8.7 ms of compute from world, edge scan, spans, brush models and models together, or a second packed buffer on top of fewer of them.

The status bar cap alone is small because status bar frames mostly coincide with notify frames (a pickup prints and flashes the status bar together), and a notify frame waits for the whole scanout either way. Its value comes with the notify fix, notify alone 1.9 ms, the cap alone 0.9, both 4.2.

Rule for every claim, run `tools\boardbench.ps1` on the branch before and after, and quote its fps and range.

## 6. History of this scoreboard

| Date | Who | Change |
| --- | --- | --- |
| 2026-11-25 | earlier work | First version, model calibrated on b585365, main dd0f819 predicted |
| 2026-11-25 | earlier work | main ff5bd24 merged, the earlier pipeline, the earlier PIO scanout and Turbo text, the earlier paths in the model, attacks re-ranked |
| 2026-11-25 | earlier work | Board run of d31520e (20.0 and 22.3 fps against the model's 26.5 and 30.4), model recalibrated on it, status bar cap what-if, attacks re-ranked for Turbo |
| 2026-11-25 | earlier work | an earlier branch row race and edge cache tail, Fast 21.9 to 24.0, Turbo 26.6 to 27.5 predicted (board 22.0 and 26.0 on f4dfb75), the model keeps core 1 busy with the rest of the scanout |
