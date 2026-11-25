# FPS scoreboard: timedemo demo3 on the board

Kept by Agent AS, the benchmark referee. Goal: 35 fps in the board's Benchmark (timedemo demo3), a 28.5 ms frame. Board numbers are measured on hardware. Model numbers come from `tools/boardbench.ps1` (docs/handoff.md), which plays demo3 on the PC with the board client's render flags, counts the work of every code path per frame, and turns the counts into board milliseconds with `tools/boardmodel.py`, calibrated on the board's own run of de58316.

## 1. Measured on the board

| Commit | Mode | Clock, flash, LCD SPI | demo3 fps | frame ms | Notes |
| --- | --- | --- | ---: | ---: | --- |
| before the speed round | Fast | 250, 125, 62.5 | 9.3 | 107.5 | view 75.7 ms (Agent AK's figures) |
| de58316 | Fast | 250, 125, 62.5 | **13.6** | **73.3** | 1090 frames in 79.9 s. Phases below. E1M1 standing 18.0 fps (frame 55.2, view 53.2, race 6.0, lmap 2.0) |

de58316 phases, ms per view: spans 16.9, lmap 13.9, world 10.4, edgescan 7.2, bmodels 6.9, models 5.1, vmodel 3.4, grad 3.1, parts 1.2, total view 72.2.

Not measured yet: anything after de58316 (Agent AL's integer lights, Agent AN's 300 MHz, Agent AO's Turbo).

## 2. Model predictions per commit (demo3, board fps, 10 to 90% range)

| Commit | What it adds | Fast 250 MHz, SPI 62.5 | Fast 300 MHz, SPI 50 | Turbo 250 MHz | Turbo 300 MHz, SPI 50 |
| --- | --- | ---: | ---: | ---: | ---: |
| de58316 | calibration run | 13.6 (by construction) | 15.3 (15.0 to 15.8) | | |
| cd1e749 (main) | AL integer lights and Fast gradients, AN 300 MHz, AO Turbo | 16.2 (15.8 to 16.4) | **18.0 (17.2 to 18.9)** | 18.9 (18.1 to 19.4) | **20.6 (19.2 to 22.2)** |

main at 300 MHz, SPI 50 MHz, ms per view (world includes its race wait at the vertex cache, as the board's timer does):

| Phase | de58316 board | main Fast | main Turbo | Confidence |
| --- | ---: | ---: | ---: | --- |
| spans | 16.9 | 14.1 | 9.3 (8.2 to 10.3) | Fast high (same work, clock only). Turbo medium: the pixel against span split of the cost is not measured |
| lmap | 13.9 | 3.1 (2.3 to 4.5) | 2.8 (2.1 to 4.0) | medium: the integer path's cost comes from the weights, split by E1M1's 2.0 ms of base grids |
| world | 10.4 | 8.9 | 8.2 | high |
| edgescan | 7.2 | 6.0 | 5.9 | high |
| bmodels | 6.9 | 5.7 | 5.7 | high |
| models | 5.1 | 4.3 | 3.6 | high (Turbo medium) |
| vmodel | 3.4 | 2.8 | 2.1 | high (Turbo medium) |
| grad | 3.1 | 0.9 (0.5 to 2.0) | 0.9 (0.5 to 1.8) | medium |
| parts | 1.2 | 1.0 | 1.0 | high |
| other (setup, sky, turb) | | 0.3 | 0.2 | low, small |
| scanout waits | about 1 to 4 (not split out on the board) | 4.7 before the world, 0.9 after, 7.9 notify frames | 1.5, 3.8, 8.4 | **low**: see section 3 |
| frame | 73.3 | 55.5 (52.6 to 58.1) | 48.5 (45.0 to 51.9) | |

"High" means the work counts did not change and only the clock moved (250 to 300 MHz, flash 125 to 150, the same ratio): those are within about 5%. Where the counts moved to new code paths, the range is the 10 to 90% spread when every a priori weight is scaled at random by up to 1.65 either way and refitted.

What the three merges are worth, by the model: AL about 9 ms at 250 MHz (lmap 13.9 to 3.7, grad 3.1 to 1.1), AN about 11 ms of compute, of which the 50 MHz scanout takes about 3 back (de58316 alone at 300 MHz: 15.3 fps), AO about 7 ms in Turbo (spans 14.1 to 9.3, models and vmodel 1.4, but the race wait moves behind R_RenderWorld instead of going away).

## 3. How well the model reproduces de58316

- Per phase: exact by construction (one residual per phase). The honest measure is the two-factor fit underneath: one factor for float helper calls (kf 1.61, so about 105 cycles a call against the 65 assumed) and one for integer cycles (ki 3.87) explain the nine board phases with a 20% rms error. Worst: bmodels 33% under (missing work, now counted: brush polygons clipped to the world tree), parts 25% over, edgescan 18% over, all others within 12%.
- E1M1 standing (a different scene, same build, board 55.2 ms, race 6.0): the model gives 56.3 ms (2% over) with 4.9 ms of row race plus the vertex cache wait. E1M1's lmap (2.0 ms) is used to split demo3's lmap into base grids (weights x 0.36) and dynamic light work (x 1.11).
- The frame: the scanout waits the code implies come to more than the board shows. With the board's own phases, the model's demo3 frame is 79.6 ms against the measured 73.3 (8% over): the board's view total (72.2) and timedemo frame (73.3) leave about 1 ms outside the view, but notify text (34% of demo3's frames in board time) makes the whole view wait for the scanout, and the status bar rows overlap the edge table, so the code says about 10 ms of waits a frame. Three reconciliations are kept and averaged: a constant offset, all waits x 0.31, or all phases x 0.90. Their spread is part of every range above.
- **One board number would settle it**: the `race` and `scanwait` fields of the `[time cl]` line during demo3, and `status bar rows sent in <n> of <m> frames`. Please read them from the next demo3 log.

## 4. Remaining gap to 35 fps (28.5 ms)

Turbo at 300 MHz is the closer start (48.5 ms, 20 ms to go). A budget that adds up to 28.5 ms, and what each phase still has to lose:

| Phase | main Turbo now | budget at 35 fps | gap |
| --- | ---: | ---: | ---: |
| scanout waits (central) | 7.7 | 0 | 7.7 |
| spans | 9.3 | 7.0 | 2.3 |
| world | 8.2 | 5.0 | 3.2 |
| bmodels | 5.7 | 2.5 | 3.2 |
| edgescan | 5.9 | 4.0 | 1.9 |
| models + vmodel | 5.7 | 4.0 | 1.7 |
| lmap + grad | 3.7 | 3.2 | 0.5 |
| parts | 1.0 | 0.7 | 0.3 |
| before and after the view, other | 1.3 | 1.3 | 0 |
| frame | 48.5 | 28.5 | 20.0 |

Fast at 300 MHz is 55.5 ms, 27 ms to go: the same list plus spans 14.1 (7 more).

The model's combined case: the race package below plus world x 0.5, bmodels x 0.5, spans x 0.7, edgescan x 0.7 and models x 0.7 gives Turbo 30.0 fps (33.4 ms), Fast 25.4. Only with no scanout waits at all on top does Turbo reach 39.9 fps (25.1 ms) and Fast 33.9. **35 fps needs the scanout waits gone, not only cheaper phases.**

## 5. Next attacks, ranked by expected ms saved per unit of risk

Savings are the model's what-if at 300 MHz, SPI 50, central (`powershell -File tools\boardbench.ps1 -WhatIf ...`). Risk: 1 low (exact, small code), 2 medium, 3 high (RAM, hardware or picture).

| Rank | Attack | Fast ms | Turbo ms | Risk | ms per risk (Turbo) | Why |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 1 | **Race package, exact**: SCR_SetUpToDrawConsole clears notify rows after VID_QogScanFree of those rows only (not VID_QogScanDone); the edge table's wait at the status bar rows becomes a ScanFree as the edge pointer reaches them (or the table stays clear of those rows); R_DrawBEntitiesOnList runs before the wait for the scanout's end | 3.6 | 6.2 | 1 | 6.2 | notify frames and status bar frames wait for the whole scanout today, and bmodels can overlap it |
| 2 | Brush models in Fast integers: R_RecursiveClipBPoly, R_BmodelCheckBBox, R_SplitEntityOnNode2 (106 polygon clips, 415 edges, 21 boxes a view, 67% float) | 2.7 | 2.7 | 1.5 | 1.8 | the most float-bound phase left |
| 3 | Turbo with no scanout wait at all (the packed view alternating between two halves of the frame, needs a home for the Turbo z-buffer) | n/a (no RAM) | 4.5 after 1 (10.7 alone) | 3 | 1.5 | Turbo's race wait moved behind the world, 3.8 ms |
| 4 | Spans x 0.7 (core balance, the busier core's share is assumed 0.55, and the per span start) | 4.0 | 2.6 | 2 | 1.3 | the biggest phase in Fast |
| 5 | LCD scanout by PIO at clk_sys / 5 = 60 MHz (the SPI peripheral can only give 50 at 300 MHz) | 2.0 | 2.1 | 2 | 1.05 | every wait shrinks with the scanout, also after 1 |
| 6 | Agent AM's core 1 for world and edge scan (world x 0.5) | 2.8 alone, 2.8 more after 1 | 2.4, 1.6 more after 1 | 2.5 | 1.0 | in Fast at 50 MHz the world is hidden behind the scanout, so it pays only once the waits go |
| 7 | Edge scan x 0.7 | 1.7 | 1.7 | 2 | 0.85 | |
| 8 | Models and view model x 0.7 (R_LightPoint's float trace, 60 float calls a model) | 2.0 | 1.6 | 2 | 0.8 | |
| 9 | lmap x 0.5 more | 1.5 | 1.3 | 2 | 0.65 | most of it already went |

Rule for every claim: run `tools\boardbench.ps1` on the branch before and after, and quote its fps and range. The agents' own estimates ran about 2x optimistic, and the model is calibrated on the board, so it is the number to beat.

## 6. History of this scoreboard

| Date | Who | Change |
| --- | --- | --- |
| 2026-11-25 | Agent AS | First version: model calibrated on de58316, main cd1e749 predicted |
