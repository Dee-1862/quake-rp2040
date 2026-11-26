# Speed plan, 30 to 40 fps in timedemo demo3

Written 2026-11-24 after the board measured 13.6 fps (Fast mode, 250 MHz, flash 125 MHz). Numbers marked "est." are estimates, everything else is measured on the board.

## Where a demo3 frame goes today (board, ms)

| Part | ms |
| --- | ---: |
| Spans (both cores already) | 16.9 |
| Lightmaps with dynamic lights (lmap) | 13.9 |
| BSP walk and edge emission (world) | 10.4 |
| Edge scan | 7.2 |
| Brush models (doors, plats, item boxes) | 6.9 |
| Monsters, view weapon, particles | 9.7 |
| Gradients | 3.1 |
| Outside the view (link, race, input) | about 8 |
| **Frame** | **80.9** (13.6 fps) |

30 fps needs a 33 ms frame, 40 fps needs 25 ms.

## What the newest ports did (research, Nov 2026)

| Port | Key steps | Source |
| --- | --- | --- |
| Quake-ESP32S3 (2026), 24 to 42 fps | register-resident lit span loop (+37%), spans split by row parity on two cores (+23%), MG24's 32-pixel perspective steps | github.com/recrof/Quake-ESP32S3 commit df3e6e5 |
| MG24Quake (2024), 29 fps | integer dynamic lights with stepped distances and a split-plane reject, textures streamed to RAM | github.com/next-hack/MG24Quake |
| frank-quake2 (2026, RP2350), 14 to 36 fps | per-core state by SIO CPUID, edge scan split into a sorter core and a span core through a ring, BSP walk on one core handing faces to the other | github.com/rh1tech/frank-quake2 commits c019cfe, f486273, 3cd7d34, 030e9cf |
| pico-quake2 (2026, RP2350) | dynamic lights caused 92% of surface rebuilds, clock gains are sublinear when memory bound (504 MHz only 7 to 9% over 378) | github.com/PicoPlus-devel/pico-quake2 TECHNICAL.md |
| rp2040-doom (2022) | RP2040 at 270 MHz and 1.30 V with flash at 135 MHz, both cores pulling from a shared queue | kilograham.github.io/rp2040-doom |

Our own findings that shape the plan: the spans are compute bound, not flash bound (Agent AH's XIP model, about 0.5 ms of misses a view), and core 1 idles about 40 ms of every demo frame.

## The plan, in order

| Step | What | Phase | Est. saving | Picture | Needs | Est. demo3 fps after |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Integer dynamic lights, loops clipped to the light's reach, split-plane reject (Agent AL, running) | lmap | 11 to 12 ms | exact | nothing | about 16 to 17 |
| 2 | Core 1 through the whole frame: lightmaps and gradients of the next surface, edge scan ring, BSP walk and face hand-off, alias front end and particle physics (Agent AM, running) | world, edgescan, models | 10 to 14 ms | exact | nothing | about 20 to 23 |
| 3 | Board PC-sampling profiler, then integer versions of the remaining float hot spots in brush models, gradients and models | bmodels, grad, models | 5 to 8 ms | Fast only, under 0.5% of pixels | nothing | about 22 to 25 |
| 4 | Hide the scanout race wait (front-end work before the frame buffer is touched, smaller cache rows) | outside view | 3 to 5 ms | exact | nothing | about 24 to 27 |
| 5 | Clock 300 MHz at 1.25 to 1.30 V (or 266 MHz with flash exactly at its 133 MHz rating) | everything | 6 to 8 ms (266 MHz about half that) | exact | **user approval** | about 27 to 30 |
| 6 | Turbo mode: 3D view drawn at 160x152 and doubled sideways by the existing scanout, which also frees about 48 KB of RAM and removes the race | spans, edge scan | 7 to 9 ms | **visible**, blocky like Doom's low detail, a separate menu choice | **user approval** | about 30 to 35 |

## Honest outlook

- Without any picture change: about 23 to 28 fps, and the top of that range needs the overclock.
- 30 fps: reachable with steps 1 to 6 together, including both approvals.
- 40 fps in demo3: unlikely at a picture people would accept. It needs about a quarter of the pixels and the non-pixel work alone is about 18 to 20 ms even at 300 MHz. Normal play is lighter than demo3 (E1M1 already runs at 18 fps), so 30 to 40 fps in ordinary play is more realistic than in the benchmark.
- Every step is measured on the board before the next one is planned, and the plan changes with the numbers.
