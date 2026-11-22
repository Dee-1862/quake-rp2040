# Research: Quake in the web browser, and what transfers to quake-2040

Date: 2026-11-22. Scope: how Quake 1 (and, for asset streaming, Quake 3) was ported to browsers, and what
an RP2040 port (Cortex-M0+, no FPU, ~245 KB SRAM, 16 MB XIP flash, 320x240 SPI LCD) can borrow.

Verification legend: **[verified]** = read in the project's own source or README during this research.
**[secondary]** = taken from a third-party article or a search summary, not checked in source.
**[unverified]** = could not be found or confirmed.

---

## 1. The ports

| Port | URL | Engine / technique | Licence | Alive? |
|---|---|---|---|---|
| WebQuake (Triang3l) | https://github.com/Triang3l/WebQuake | Hand-written JavaScript port of WinQuake/GLQuake, WebGL | GPL (repo ships GNU.md) | Last push 2026-04 (low activity; compat notes date from 2013) |
| lrusso/Quake1 | https://github.com/lrusso/quake1 | Repackaged WebQuake (commit 2019-04-21) | GPL (inherited) | Dormant since 2019 |
| Qwasm, ex "Quake-WASM" (GMH-Code) | https://github.com/GMH-Code/Qwasm (old URL github.com/GMH-Code/Quake-WASM redirects), live demo https://qwasm.m-h.org.uk/ | id WinQuake + SDL2, Emscripten to WASM; software renderer and GLQuake via GL4ES/WebGL | GPL (ships gnu.txt; GitHub shows no SPDX tag) | Last push 2024-12 |
| initialed85/Quake-WASM | https://github.com/initialed85/Quake-WASM | Fork of Qwasm that adds WebSocket multiplayer | GPL | Last push 2025-01 |
| NexQuake | https://github.com/0xBrsm/NexQuake, docs https://quake.nexus/web-client/ | id-Software/Quake source + patches, Emscripten, ASYNCIFY | GPL-3.0 | Active (push 2026-10) |
| FTEQW web build | https://github.com/fte-team/fteqw, spec https://raw.githubusercontent.com/fte-team/fteqw/master/specs/browser.txt | Emscripten build of FTE (QW/NQ engine), WebGL, WebSocket/WebRTC | GPL-2.0 | Active (push 2026-11) |
| raylib_Quake | https://github.com/raydelto/raylib_Quake | WinQuake on raylib; web build with touch controls (PR #15/#16) | GPL (fork of id-Software/Quake; no own SPDX tag) | Active (push 2026-10) |
| quake-srp | https://github.com/terrapapagalli1516/quake-srp, demo https://quake-srp.pages.dev | WinQuake rewritten in safe Rust, compiled to wasm32-wasip1(-threads), runs in a Web Worker; software renderer | GPL-2.0 | Active (push 2026-10) |
| QuakeJS (Quake 3, inolen) | https://github.com/inolen/quakejs, demo quakejs.com | ioquake3 via Emscripten; content server + repak tool | MIT for the JS tooling (ioquake3 itself is GPLv2) | Last push 2025-11; reported hard to build on modern Emscripten [secondary] |
| netquake.io | (no live page found) | Forum post describes it as a WebQuake-based prototype with master server, planning to move to FTE's WASM build | unknown | **[unverified]** status unknown; probably dead |
| "Kevin's WebGL ports", "quake.js" (Q1) | not found | | | **[unverified]**: nothing matching these names was found for Quake 1 |
| QuakeSpasm / vkQuake / TyrQuake WASM | not found | | | **[unverified]**: no maintained Emscripten build of these was found; Qwasm, NexQuake and FTE are the real ones |

### One-line "what we could borrow"

- **WebQuake**: it parses BSP/MDL lumps straight from `ArrayBuffer` typed-array views and keeps edicts as typed-array views (`PR.edict_size = 96 + entityfields*4`). That is the same "read in place, don't copy" model we use on flash. It also lazy-loads individual pak entries with HTTP Range requests (see 2.2).
- **Qwasm**: proves stock WinQuake runs unmodified behind a thin SDL/sys layer with a fixed hunk (`-mem`, default 16 MB). Its `wasm.cfg` default bindings (always-run, WASD, wheel = impulse 10/12) are a good small default config.
- **NexQuake**: lazy per-file virtual filesystem driven by a manifest, a prefetch of models and sounds on connect, a lock-free audio ring buffer, and the best touch layout of the lot.
- **FTEQW**: one manifest file (`.fmf`) describes where every package comes from. Packages are stored uncompressed and compressed as a whole by the transport, so the engine never needs zlib.
- **QuakeJS repak**: the best example of a **per-map asset dependency graph**. It splits the data into a "common" pack (assets used by 3 or more maps) plus one pack per map, and re-encodes audio.
- **quake-srp**: checks the port against a headless build of id's C (the "oracle"): pixel-exact frames, the mixer sample for sample, demo playback frame by frame. This is the same idea as our PC harness and qc2c bit-identity check, taken further.

---

## 2. How they handled each problem

### 2.1 Memory model

- **Qwasm** [verified, `WinQuake/Makefile.emscripten` and `sys_sdl.c`]: `-sINITIAL_MEMORY=64MB -sALLOW_MEMORY_GROWTH -sTOTAL_STACK=2MB`. The Quake hunk is one `malloc(parms.memsize)` with `parms.memsize = 16*1024*1024`, changeable with `-mem <MB>` or `-heapsize <KB>`. So the engine still sees one fixed arena; only the WASM memory around it can grow.
- **NexQuake** [verified in docs]: `-mem` default 64 MB.
- **Quake2-WASM** (GMH-Code) notes that pages freed by a dynamic heap are not returned to the system [secondary].
- **WebQuake** [verified]: has no hunk at all. Models are JS objects in `Mod.known[]` and the garbage collector frees them, so there is no fixed budget to learn from. The useful part is the **views over raw file bytes** (`new Uint8Array(buf, miptexofs, ...)`), which avoids copying texture and lump data.
- **None of the ports works to a tight budget.** All of them have 16 to 64 MB to work with. No port with a sub-megabyte or "fixed small memory" budget was found **[unverified/none found]**. Our RAM reduction work (2.76 MB down to ~245 KB) has no web precedent. The lessons that carry over are structural: one arena, views instead of copies, and sizing by use rather than by MAX_ constants.

### 2.2 Game data and pak loading

- **WebQuake: lazy per-file loading with Range requests** [verified, `COM.js`]. `COM.LoadPackFile` fetches only the 12-byte header (`Range: bytes=0-11`), then the directory, keeping `{name, filepos, filelen}` per entry. `COM.LoadFile` then fetches just `bytes=filepos..filepos+filelen-1` when a file is needed. This is exactly our XIP model: the pak directory is the index, and each file is read in place on demand.
- **Qwasm** [verified]: `--preload-file=id1` bundles the paks into `index.data` (MEMFS, all in RAM). It recommends serving gzip/Brotli. User data lives under an IDBFS mount (`/qwasm`), synced with `FS.syncfs`. Paks are deliberately **not** stored in IDBFS because syncing that much data stalls the game.
- **NexQuake** [verified in docs]: fetches a manifest from `/gamedir` and builds a VFS of **lazy nodes** under `/nexusfs/<mod>/` that download on first read, with retry and backoff. URLs are content-hashed (FNV-1a) so a CDN can cache them forever. On connect, it **prefetches the models and sounds** the server announces (that is, the precache lists), 16 workers in parallel. User files override remote ones in the search path.
- **FTEQW** [verified, `specs/browser.txt`]: a `.fmf` manifest lists the packages to fetch. "fte's web port doesn't include zlib, so pk3s should not use compression for individual files. Instead gzip them for solid archives with the browser doing all the decompression." Custom maps are fetched from the game server on demand.
- **QuakeJS (Q3)** [verified, `bin/repak.js` and `lib/asset-graph.js`]:
  - It flattens all pk3s into one directory (later paks win) and drops blacklisted files (LOD models `_[123].md3`, `.map` sources).
  - It builds a graph with one vertex per asset. `addMap` parses the BSP and adds edges for entity keys `music`, `noise` (skipping `*` names) and `model2`, plus every name in the shaders lump. It ignores `model` because inline brush models are part of the BSP. It also links assets whose name contains the map name, plus whitelisted assets.
  - `getCommonAssets` puts an asset in the common pack if `references >= referenceThreshold` (3) or it is whitelisted (gfx/, sound/, menu/, ui/, qvm, cfg).
  - `getMapAssets` walks outward from a map, skipping anything common.
  - Output: map-specific pk3s plus common pk3s split at 16 MB, with **WAV re-encoded to Opus**. A JSON manifest (`content.quakejs.com/assets/manifest.json`) lists what the server has.
- **quake-srp** [verified, README]: the game ships with no data. A script fetches the shareware zip and checks its hashes, and the user adds `pak1.pak` locally.

### 2.3 Main loop and timing

- **WebQuake** [verified, `Sys.js`]: `setInterval(Host.Frame, 1000/60)`. `Sys.FloatTime = Date.now()*0.001 - oldtime`. `Host._Frame` clamps `frametime` to [0.001, 0.1], or uses `host_framerate` when that is set.
- **Qwasm** [verified, `sys_sdl.c`]: `emscripten_set_main_loop(main_loop, 0, 0)` (fps 0 means requestAnimationFrame, synced to the display). `main_loop` is the body of the old `while(1)` loop with `sys_ticrate` handling. A `WASM_BENCHMARK` build uses `EM_TIMING_SETIMMEDIATE` to run as fast as possible. Until the IDBFS restore finishes it returns early without running `Host_Frame`.
- **NexQuake** [verified in docs]: `emscripten_set_main_loop`, with `-sASYNCIFY` so blocking network loops can `emscripten_sleep`.
- Lesson: every port keeps `Host_Frame(dt)` unchanged and only changes **who calls it** (display-synced callback or timer). Quake copes with a variable dt; the 0.1 s clamp stops physics from exploding after a stall, such as a long flash read.

### 2.4 Audio

- **Qwasm** [verified, `snd_sdl.c`]: the stock DMA mixer (`snd_dma.c`/`snd_mix.c`) feeds an SDL audio callback, `desired.samples = 512`. Web Audio does the device-rate conversion behind SDL.
- **NexQuake** [verified in docs]: the mixer writes into a **ring buffer in the WASM heap**. A `ScriptProcessorNode` callback drains it. Single-threaded, no locks. CD music is emulated: `cd` commands call into JS, which plays tracks through an HTML5 `<audio>` element.
- **WebQuake** [verified, `S.js`]: skips Quake's mixer entirely. Each sound is turned back into a WAV and played by the browser (`createBuffer` or `new Audio('data:audio/wav;base64,...')`), with gain nodes for stereo panning. Its Web Audio path is commented out ("createBuffer is broken"). Music is `id1/media/quakeNN.ogg`. Not applicable to us.
- **QuakeJS**: WAV to Opus in the asset pipeline (decode cost is fine on a PC, too high for an M0+).
- Lesson for us: keep Quake's own 11025 Hz integer mixer (`snd_mix.c` is fixed-point already) and drain a ring buffer from DMA. This is the NexQuake pattern, with PWM or I2S DMA in place of ScriptProcessorNode.

### 2.5 Input on touch and limited controls

- **NexQuake** [verified in docs, `quake.nexus/controls/`]:
  - Layout: left 40% of the screen is a floating virtual joystick, right 60% is swipe-look, plus 9 bindable edge slots (`touch1`..`touch9`).
  - Defaults: `touch1` jump, `touch2` `impulse 12`, `touch3` fire, `touch4` `impulse 10`, `touch8` rocket launcher, `touch9` lightning gun.
  - The overlay hides after 2.5 s idle. `touch_flip` mirrors it for left-handers. `touch_sensitivity 3`. `analog_speed 400` caps the analog turn rate in degrees per second.
  - Gamepad API: left stick moves, right stick looks.
- **raylib_Quake PR #15** [verified, PR page; superseded by PR #16]:
  - Play: floating move stick on the left half (walk near the centre, run at the rim) and drag-to-look on the right. FIRE, JUMP and WPN› buttons send `+attack`, `+jump` and `impulse 10` directly, whatever the key bindings are. Free look is always on and pitch drift is suppressed. Pitch is clamped at 80/-70 degrees.
  - Separate **menu mode**: arrows, OK, BACK, Y and N, with swipe steps of 40 px.
- **Qwasm** [verified]: there is a "Touchscreen-only controls" checkbox on the demo page, but no documentation of it. `wasm.cfg` sets always-run (`cl_forwardspeed/cl_backspeed 400`) and `impulse 10/12` on the wheel.
- **Auto-aim**: no web port documents its own aim assist **[none found]**. Stock Quake already has **server-side vertical autoaim**: `PF_aim` with `sv_aim` (default 0.93 in WinQuake `sv_main.c`, from our own knowledge of the source). That is why stock Quake plays acceptably without mouselook.

### 2.6 Engine binary size

- No port reports deliberate code-size work [none found]. FTE: "pre-gzip it if you can". Qwasm: serve gzip/Brotli. Qwasm uses `-O3 -ffast-math -funroll-loops`, the opposite of what we want for flash or XIP-cache friendliness. Nothing to borrow beyond "compress at the transport layer, not inside the engine".

### 2.7 QuakeC

- **Every web port interprets `progs.dat`** [verified for WebQuake (`PR.ExecuteProgram` switch over `PR.statements`) and stated in the Qwasm README]. Quake2-WASM compiles game DLL code to WASM, but that is C, not QuakeC.
- The nearest thing to ahead-of-time QuakeC: **progsdat** (https://github.com/ftrvxmtrx/progsdat), "Quake's progs.dat-to-C decompiler" meant to run game logic natively in necroquake. Dormant since 2018, licence NOASSERTION, working state unknown **[secondary]**. FTEQCC/QCVM are interpreters. No QuakeC JIT was found **[none found]**.
- Our `tools/qc2c.py` (AOT to C, verified bit-identical) is ahead of every web port here.

### 2.8 Savegames

- **WebQuake** [verified, `COM.js`, `Host.js`]: `COM.WriteFile` stores the file in `localStorage` under `Quake.<gamedir>/<filename>`, and `COM.LoadFile` checks localStorage **before** paks. Saves are the stock version-5 text format (comment, 16 spawn parms, skill, map, time, 64 lightstyles, globals block, one block per edict).
- **Qwasm** [verified]: IDBFS mount, `FS.syncfs(true)` at boot (the main loop waits for it), `FS.syncfs()` after a save or config change.
- **FTE**: `cfg_save` writes the config to localStorage ("browsers might still wipe it all eventually").
- **NexQuake**: IDBFS. User files override remote files.
- Lesson: a **writable overlay looked up before the read-only pak** is the universal pattern. For us that is a small reserved flash region (or a few SRAM-buffered sectors) checked by `COM_FOpenFile` before the XIP pak image.

---

## 3. Per-map asset manifests: what exists and what we should build

- **QuakeJS's asset graph** (2.2) is the only real per-map dependency tool found. It is Q3-specific (shaders lump, `model2`, `noise`, `music`).
- For Quake 1, searches turned up no ready-made "which files does this map need" tool **[none found]**. Nearby tools:
  - q1tools' **BSP Texture Trimmer** (https://github.com/q1tools/q1tools.github.io) crops unused texture area in a .bsp.
  - id's qutils `qfiles` builds paks from the `files.dat` that qcc writes (the list of files the progs precaches, game-wide, not per map).
  - QPakMan and other pak managers list and rebuild paks.
- **Why Quake 1 makes this easy** (from our knowledge of the WinQuake source, not from a web port): `PF_precache_model` and `PF_precache_sound` are only allowed while the server is loading ("Precache can only be done in spawn functions"), and `SV_ModelIndex` / `setmodel` require the model to be precached already. So after `SV_SpawnServer` finishes, `sv.model_precache[]` and `sv.sound_precache[]` are the **complete, authoritative list** of models and sounds the server can ever reference on that map. That covers the world, the `*N` inline models, everything precached in `W_Precache`/`worldspawn`, and the per-classname spawn precaches.
- **What the precache lists miss, and must be added as a fixed "engine" set**:
  - client-side hard-coded assets: `cl_tent.c` models (`progs/bolt*.mdl`, `progs/beam.mdl`) and sounds (`weapons/r_exp3.wav`, `tink1`, `ric1-3`, `wizard/hit.wav`, `hknight/hit.wav`)
  - `snd_dma.c` ambients (`ambience/water1.wav`, `wind2.wav`)
  - menu, console and HUD files (`gfx.wad`, `gfx/*.lmp`, `conchars`, `misc/menu*.wav`, `misc/talk.wav`)
  - `progs.dat` (not needed: we use qc2c)
  - palette and colormap
  - `default.cfg`/`quake.rc`
  - the skybox texture inside the BSP
- **Map-to-map links**: `trigger_changelevel` `map` keys (and `start.bsp` hub exits) give the levels reachable from each map, so the converter can follow the chain from `start`, or ship only a selected episode.

Recommended converter algorithm (QuakeJS structure, Quake-1 data source):
1. On the PC harness, load each candidate map (`map eXmY`, run 1-2 frames) and dump `sv.model_precache`, `sv.sound_precache` and every `COM_FOpenFile`/`COM_LoadFile` name (an access log, which also catches files the lists miss).
2. Union with the fixed engine set.
3. Compute reference counts across the selected maps. Anything used by 3 or more maps (QuakeJS's threshold), or by the engine set, goes in a "common" region. The rest goes next to its map. With XIP everything is in one image anyway, so the real benefit is **dropping assets no selected map uses** and choosing **which maps to ship** under the ~12 MB limit.
4. Emit a manifest (name, offset, size, CRC, list of maps that use it) next to the flash image. The firmware can use it to fail early with a clear message instead of a `Host_Error` mid-level.

---

## 4. Ranked ideas for the RP2040 port

1. **High: per-map precache dump to drive the flash converter.** Run each map headless on the PC harness, log `sv.model_precache`/`sv.sound_precache` plus a file-access log, add the fixed client/menu set, and emit per-map manifests and a union. Quake guarantees the precache lists are complete after spawn, so this is exact, not a heuristic. This is the main tool for getting 17 MB down to 12 MB. (QuakeJS asset graph, NexQuake precache prefetch.)
2. **High: choose maps by budget using the manifests.** Show shared vs unique bytes per map (QuakeJS common/per-map split, threshold 3) so we can decide, for example, "all of episode 1 plus start" against "shareware plus selected registered maps". Drop the demos (`demo1-3.dem`) and everything the manifest doesn't reference.
3. **High: writable overlay checked before the pak** for saves and config, in a reserved flash region. WebQuake checks localStorage before paks, Qwasm uses IDBFS plus explicit sync. Use explicit, rare syncs (on save only), as Qwasm does, because flash erase/program stalls XIP like IDBFS sync stalls the browser. Keep the stock text save format (bounded size, easy to debug on the PC).
4. **High: size edicts and tables by actual use, the way WebQuake sizes edicts from progs (`96 + entityfields*4`).** The 96-byte header plus the real field count, times a per-map edict count from the manifest step (entity-lump count + headroom for missiles and gibs), instead of `MAX_EDICTS`. The same applies to `cl_entities` and `mod_known` (precache counts per map are known from idea 1). This is our inference from WebQuake's layout, not something any port did for memory reasons.
5. **Medium: keep the stock fixed-point mixer at 11025 Hz and drain it from a DMA ring buffer** (NexQuake heap ring buffer pattern). Converter side: downsample any 22 kHz sounds to 11 kHz 8-bit and drop unused sounds. Skip Opus/OGG (too expensive to decode on an M0+). CD music: not supported, or a stub, like the web ports' `cd` to JS.
6. **Medium: frame pacing like Qwasm's `main_loop`.** Keep `Host_Frame(dt)` unchanged, drive it from LCD DMA completion (our "vsync"), keep the `frametime` clamp of [0.001, 0.1] so a slow flash read doesn't break physics, and use `host_framerate` for deterministic harness runs. A WASM_BENCHMARK-style "uncapped" build flag helps with profiling.
7. **Medium: control scheme for few buttons.**
   - Rely on stock `sv_aim` vertical autoaim (0.93) instead of mouselook.
   - Always-run by default (`cl_forwardspeed 400`, Qwasm `wasm.cfg`).
   - One "weapon cycle" button sending `impulse 10` (NexQuake, raylib).
   - FIRE and JUMP sent as `+attack`/`+jump` directly, independent of binds (raylib).
   - A separate menu mode where the same buttons act as arrows, OK and BACK (raylib PR #15).
   - If a joystick or touch panel is added later: walk near the centre, run at the rim; `analog_speed`-style turn-rate cap (NexQuake 400 deg/s).
8. **Medium: oracle-style verification.** Extend the harness, as quake-srp does: compare framebuffer CRCs and mixer output of the RP2040-layout build against stock WinQuake on fixed demo frames, with the same rigour as the qc2c check, for every RAM-saving change (map-structure compaction, edict shrinking).
9. **Low: BSP texture trimming and mip pruning in the converter** (q1tools BSP Texture Trimmer). Small wins on flash, worth doing only after ideas 1-2.
10. **Low: content-hashed, uncompressed-inside, compressed-outside packaging** (FTE, NexQuake). Not relevant to XIP read-in-place (compression would break in-place reads), except for the image sent over USB: compress for transport and decompress while flashing.
11. **Low: lazy loading or streaming.** XIP already gives "load on first touch" for free. The web ports' lazy VFS solves network latency, which we don't have. The only analogue is caching hot flash data in SRAM, which is a separate design question.

---

## Sources

- WebQuake repo: https://github.com/Triang3l/WebQuake
- WebQuake source, read directly: https://raw.githubusercontent.com/Triang3l/WebQuake/master/Client/WebQuake/COM.js , .../Sys.js , .../Host.js , .../PR.js , .../S.js , .../Mod.js
- WebQuake author's Habr post (Russian): https://habr.com/ru/post/177159/
- lrusso Quake1: https://github.com/lrusso/quake1
- Qwasm (formerly Quake-WASM): https://github.com/GMH-Code/Qwasm , https://github.com/GMH-Code/Quake-WASM , demo https://qwasm.m-h.org.uk/
- Qwasm source: https://raw.githubusercontent.com/GMH-Code/Qwasm/master/WinQuake/Makefile.emscripten , .../WinQuake/sys_sdl.c , .../WinQuake/snd_sdl.c , .../WinQuake/id1/wasm.cfg
- Quake2-WASM: https://github.com/GMH-Code/Quake2-WASM
- initialed85 fork: https://github.com/initialed85/Quake-WASM
- NexQuake: https://github.com/0xBrsm/NexQuake , https://quake.nexus/web-client/ , https://quake.nexus/controls/
- FTEQW: https://github.com/fte-team/fteqw , https://raw.githubusercontent.com/fte-team/fteqw/master/specs/browser.txt
- raylib_Quake touch PR: https://github.com/raydelto/raylib_Quake/pull/15
- quake-srp: https://github.com/terrapapagalli1516/quake-srp , https://quake-srp.pages.dev , article https://intragoals.com/article/quake-port-written-in-safe-rust-runs-in-the-browser-with-no-gpu-needed-181
- QuakeJS: https://github.com/inolen/quakejs , https://raw.githubusercontent.com/inolen/quakejs/master/bin/repak.js , https://raw.githubusercontent.com/inolen/quakejs/master/lib/asset-graph.js
- QuakeJS forks and related: https://github.com/begleysm/quakejs , https://q3js.com/quake-3-webassembly
- netquake.io mention (Func_Msgboard archive): https://www.quaddicted.com/webarchive/www.celephais.net/board/view_thread.php%3Fid%3D61722.html
- progsdat (progs.dat to C): https://github.com/ftrvxmtrx/progsdat
- q1tools (BSP Texture Trimmer): https://github.com/q1tools/q1tools.github.io
- id qutils (qfiles/files.dat): https://github.com/id-Software/Quake-Tools/tree/master/qutils
- Emscripten runtime and main loop: https://emscripten.org/docs/porting/emscripten-runtime-environment.html , audio https://emscripten.org/docs/porting/Audio.html
