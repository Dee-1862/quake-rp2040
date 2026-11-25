# FPS scoreboard: timedemo demo3 on the board

Kept by Agent AS, the benchmark referee. Goal: 35 fps in the board's Benchmark (timedemo demo3), a 28.5 ms frame. Board numbers are measured on hardware. Model numbers come from `tools/boardbench.ps1` (docs/handoff.md), which plays demo3 on the PC with the board client's render flags, counts the work of every code path per frame on both cores, and turns the counts into board milliseconds with `tools/boardmodel.py`, calibrated on the board's own run of de58316.

## 1. Measured on the board

| Commit | Mode | Clock, flash, LCD | demo3 fps | frame ms | Notes |
| --- | --- | --- | ---: | ---: | --- |
| before the speed round | Fast | 250, 125, SPI 62.5 | 9.3 | 107.5 | view 75.7 ms (Agent AK's figures) |
| de58316 | Fast | 250, 125, SPI 62.5 | **13.6** | **73.3** | 1090 frames in 79.9 s. Phases below. E1M1 standing 18.0 fps (frame 55.2, view 53.2, race 6.0, lmap 2.0) |

de58316 phases, ms per view: spans 16.9, lmap 13.9, world 10.4, edgescan 7.2, bmodels 6.9, models 5.1, vmodel 3.4, grad 3.1, parts 1.2, total view 72.2.

Not measured yet: anything after de58316. The next board log should give `race`, `scanwait` and the core 1 line (`pipe`, `wait`, `c0wait`, `early`) of `[time cl]` during demo3, and `last scan ... us for 152 rows`.

## 2. Model predictions per commit (demo3, board fps, 10 to 90% range)

| Commit | What it adds | Fast 300 MHz | Turbo 300 MHz | LCD |
| --- | --- | ---: | ---: | --- |
| de58316 | calibration run (13.6 at 250 MHz by construction) | 15.3 (15.0 to 15.8) | | SPI 50 |
| cd1e749 | AL integer lights and Fast gradients, AN 300 MHz, AO Turbo | 18.0 (17.2 to 18.9) | 20.6 (19.2 to 22.2) | SPI 50 |
| 768c8a6 + agent-as | AM two-core surfaces, AP PIO scanout and Turbo text list, AQ exact float cuts | **22.6 (21.4 to 23.4)** | **26.5 (24.5 to 27.7)** | PIO 60 |
| the same, `-DQOG_LCD_PIO=OFF` | | 21.2 | 24.6 | SPI 50 |

What the last three merges are worth by the model (300 MHz, ms per frame, Fast / Turbo): AM's pipeline 3.9 / 3.6 (what-if `nopipe`), AP's PIO scanout 2.8 / 3.0, AQ's cuts about 4 (bmodels 5.7 to 2.7, edgescan 6.0 to 4.8 before the pipeline). The agents estimated AM 9 to 12 ms at 250 MHz, AP about 4 ms and AQ 2.8 to 5.6 ms.

768c8a6 + agent-as at 300 MHz, LCD by PIO at 60 MHz, ms per view:

| Phase | de58316 board (250 MHz) | Fast | Turbo | Confidence |
| --- | ---: | ---: | ---: | --- |
| world (with its race wait) | 10.4 | 8.3 | 7.9 | high |
| bmodels | 6.9 | 2.7 (2.0 to 3.3) | 2.7 | medium: AQ's cuts priced from AQ's cycle reads |
| edgescan | 7.2 | 4.8 (4.0 to 5.6) | 4.8 | medium (AQ) |
| lmap, work of both cores | 13.9 | 3.2 (2.4 to 3.9) | 2.9 | medium |
| grad, work of both cores | 3.1 | 1.0 (0.6 to 2.0) | 0.9 | medium |
| spans, work of both cores (one core's whole surfaces) | 16.9 (core 0's half) | 25.0 | 16.3 (14.9 to 18.0) | Fast high, Turbo medium |
| surfaces after the scan, on core 0's clock | | 13.4 | 8.6 | **low to medium**: core 1's speed beside core 0 (0.8 to 1.0) and the early share are modelled, not measured |
| core 0's wait for core 1's last surface | | 0.4 | 0.3 | low |
| models | 5.1 | 4.3 | 3.6 | high |
| vmodel | 3.4 | 2.8 | 2.1 | high |
| parts | 1.2 | 1.0 | 1.0 | high |
| scanout waits: before the world, after it, notify frames | about 1 to 4 (not split out) | 3.1, 0.4, 7.0 | 1.0, 1.8, 7.7 | **low**: see section 3 |
| frame | 73.3 | 44.3 (42.6 to 46.6) | 37.7 (35.9 to 40.3) | |

The pipeline in the model: core 1 draws whole lit world surfaces while core 0 scans edges, up to the pixels handed on early (the PC counts 49% of the pipelined pixels early in Fast, 61% in Turbo), then both share the rest after core 0 has drawn the sky, water and brush surfaces, and core 0 waits on average half of core 1's last surface.

## 3. How well the model reproduces de58316

- Per phase: exact by construction (a residual per phase). The two factors underneath (float helper calls x 1.52, integer cycles x 4.10 against the a priori weights) explain the nine phases with a 17% rms error: bmodels 27% under, edgescan 20% over, parts 25% over, the others within 11%.
- E1M1 standing (another scene, same build): 53.6 ms against the board's 55.2 (3% under), row race 4.8 ms plus the vertex cache wait against the board's race 6.0.
- The demo3 frame: the scanout waits the code implies give 79.5 ms against the measured 73.3 (8% over), because notify text (35% of demo3's frames in board time at 13.6 fps) and status bar frames wait for the whole scanout while the board's own view total leaves about 1 ms outside the view. Three reconciliations (a constant offset, every wait x 0.31, every phase x 0.90) are averaged and their spread is in every range. The `race` and `scanwait` fields of the next demo3 log settle it.
- The reference log is now the merged tree with Agent AL's six flags, Agent AQ's eight and `QOG_DRAW_CORE1` taken out: every count equals the first reference's (de58316 equivalent) within 1%.

## 4. Remaining gap to 35 fps (28.5 ms)

Turbo at 300 MHz with the PIO scanout is 37.7 ms, 9.2 ms to go. A budget that adds up to 28.5 ms:

| Item on core 0's clock | Turbo now | budget at 35 fps | gap |
| --- | ---: | ---: | ---: |
| scanout waits and the time around the view (central) | 6.5 | 3.5 | 3.0 |
| world | 7.9 | 5.0 | 2.9 |
| surfaces after the scan and the last-surface wait | 8.9 | 7.5 | 1.4 |
| models + vmodel | 5.7 | 4.5 | 1.2 |
| edgescan | 4.8 | 4.8 | 0 |
| bmodels | 2.7 | 2.2 | 0.5 |
| parts, other | 1.2 | 1.0 | 0.2 |
| frame | 37.7 | 28.5 | 9.2 |

Fast is 44.3 ms, 15.8 to go: the same items plus about 5 ms more of surfaces (twice the pixels).

Combined cases (model what-ifs, Turbo / Fast fps): the race package with world x 0.5 and spans x 0.7 gives 34.8 / 28.8, adding models x 0.7 gives **37.0 / 30.6**. So Turbo can reach 35 fps with the exact race package, a cheaper world front end, faster span pixels and alias models on core 1. Fast needs about 6 ms more than that.

## 5. Next attacks, ranked by expected ms saved per unit of risk

Savings are the model's what-ifs at 300 MHz with the PIO scanout, central, ms per frame (`powershell -File tools\boardbench.ps1 -WhatIf "label=item,item;..."`). Risk: 1 low (exact, small code), 2 medium, 3 high (RAM, hardware or picture).

| Rank | Attack | Fast ms | Turbo ms | Risk | ms per risk (Turbo) | Why |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 1 | **Race package, exact**: notify rows cleared after a VID_QogScanFree of those rows only (not VID_QogScanDone), the edge table's status bar wait made a ScanFree as edges reach those rows, R_DrawBEntitiesOnList before the wait for the scanout's end | 2.9 | 4.9 | 1 | 4.9 | notify frames (about half of them now, the frames got faster) and status bar frames wait for the whole scanout |
| 2 | World front end x 0.5 (BSP walk handed to core 1, which idles through the world after its scanout, or the edge scan split as frank-quake2 does) | 3.0 (2.7 more after 1) | 2.6 (1.6 more after 1) | 2 | 1.3 | the biggest single core 0 phase left. Agent AQ found it mostly integer and flash reads now |
| 3 | Span pixels x 0.7 (both cores' drawer: the per span start, the interpolator loop) | 3.8 | 2.4 | 2 | 1.2 | the pipeline's critical share |
| 4 | Turbo with no scanout wait at all (two packed views alternating, a home for the Turbo z-buffer) | n/a (no RAM) | 3.2 after 1 (8.1 alone) | 3 | 1.1 | |
| 5 | Alias front end and particle physics on core 1 while core 0 draws its share (models and vmodel x 0.7) | 2.0 | 1.6 | 2 | 0.8 | core 1 idles through models, vmodel and parts |
| 6 | Brush models x 0.5 more (Fast integer clip points, box test) | 1.3 | 1.2 | 1.5 | 0.8 | AQ took most of the float work |
| 7 | Edge scan x 0.7 (R_LeadingEdge's first test into the loop, about 0.2 ms by AQ) | 0.8 | 0.8 | 2 | 0.4 | |
| 8 | lmap x 0.5 more | 0.8 | 0.7 | 2 | 0.35 | mostly gone |

The ranking changed: the brush models (rank 2 before) mostly went with Agent AQ, the PIO scanout (rank 5 before) is in, and the world front end now pays without the race package first, because the faster scanout leaves less to hide behind (Agent AP's point).

Rule for every claim: run `tools\boardbench.ps1` on the branch before and after, and quote its fps and range. The agents' own estimates ran about 2x optimistic, and the model is calibrated on the board, so it is the number to beat.

## 6. History of this scoreboard

| Date | Who | Change |
| --- | --- | --- |
| 2026-11-25 | Agent AS | First version: model calibrated on de58316, main cd1e749 predicted |
| 2026-11-25 | Agent AS | main 768c8a6 merged: Agent AM's pipeline, Agent AP's PIO scanout and Turbo text, Agent AQ's paths in the model, new reference log, attacks re-ranked |
