# Handoff: where QuakeOG stands

## On the board today
- **Demo player** (apps/qogdemo): flashed and playing Quake demo 1 on the FREE-WILi OG at 3 to 7 fps, from agent-o. It uses a 1-byte depth buffer.
- **E1M1 fly-through** (apps/qogfly): ran at 6 to 9 fps before the demo player replaced it.
- **Benchmark** (apps/qogbench): 52.5 ms per 320x152 lit view before the speed-ups.

## The full game, split across both RP2040s
- Server on the main chip (quake_main), client on the display chip (quake_display), joined by the link driver. Exact on the PC (state hashes, saves and frames identical to the single-process game).
- quake_main links (about 1.7 KB RAM spare). quake_display links for E1M1 only (68 KB hunk, 34 KB stack, about 1 KB spare), using the 1-byte depth buffer the user approved for the board client only.
- Client hunk (Agent Y): 73 KB now covers start and all of episode 1 (worst e1m4 70.4 KB on a fight tour). The whole game needs about 82 KB (e2m6). The display image without sounds and progs.dat (`mkboard.py --display-no-sounds --display-no-progs`) still needs 13.19 MB for episode 1 and main 5.75 MB, so the data offsets must move first (docs/changes.md, Agent Y). The server hunk is 160 KB, e1m3 needs about 170 with sound.
- Sound (agent-w, not yet heard on the board): id's mixer runs on main for the display's sound calls, the display plays the samples on the I2S amplifier by PIO and DMA. Main's data image must carry the sounds (`mkboard.py --main`), the client stack is 32 KB with sound. Exact on the PC. Agent K's 48 KB exact edge fallback is not reserved: on the board it stops with an error (QOG_ZEDGE_STACK_GUARD).
- The display chip's flash fits start plus e1m1 to e1m4 (12.47 MB). All of episode 1 does not fit (16.07 MB). The options are in docs/changes.md (agent-j): move QOG_DATA_OFFSET to 0x200000, 4-bit ADPCM sounds, or load levels from the PC companion.

## First flash of the split game (one CPU in BOOTSEL at a time)
Run from the repo root once the merges in branches agent-r, agent-t and agent-s are on main. Details are in docs/changes.md (agent-t and agent-r entries) and docs/game-data.md.
1. Build both images from one run: `python tools/mkboard.py ... --main` (display image qb.uf2 and server image qb_main.uf2). Start with `--maps start e1m1` so the client hunk fits.
2. `python third_party/wiliOGbsp/tools/fw.py bootsel --cpu display`, then copy the display data UF2 to the drive.
3. `python third_party/wiliOGbsp/tools/fw.py flash quake --uf2 <server data UF2>`
4. `cmake --preset target -DQOG_AOT=ON -DQOG_AOT_PAK=<path to pak0.pak> -DQOG_SPLIT_MAIN_EMBED=quake_display`, then `cmake --build build --target quake_main`, then `python third_party/wiliOGbsp/tools/fw.py flash quake --uf2 build/apps/quake/quake_main.uf2`
5. Never flash the stand-in display image as the game. Read the consoles with COM7 (main) and COM8 (display), DTR on.
Expect a debugging round: the full-speed link and saving to the main chip's FAT volume have never run on hardware.

## Housekeeping
- Push with GitHub Desktop or `git push`. Silent pushes fail on authentication.
- Remove finished worktrees (qog-wt-b to qog-wt-u and qog-wt-fly) and their branches: unlink each `third_party` junction first with `cmd /c rmdir`, then `git worktree remove`, then `git branch -d`.
- Rules: tests always go through tools/pctest.ps1 (hidden, `-nosound`, `-basedir`). Commits have a one-line title, bullet body, no colons, semicolons or em dashes, no mention of any AI.
- Game data (data/id1, qogbake.bin, any board image) is never committed.

## Still open (nothing was done on it)
- Client hunk gap: about 17 KB more for start to e1m4, about 29 KB for the whole game. Agent U was meant to start on it but was blocked and did no engineering work (branch agent-u holds only a note, do not merge it).
- Next steps, from the measurements: build a base, a depth and a split pair build, measure `qog_mem hunk` per map on the split client, then cut the biggest blocks exactly. Candidates are the 7.5 KB net_message in the z-buffer temp region during signon, the per-level map arrays (about 35 KB on E1M1) and the model_t RAM table. The 4 KB console and command buffer cuts change behaviour, so they need the user's decision.
- Find out why the 1-byte depth buffer reaches the edge fallback in some views, since on the board that stops the game with an error.
- Sound: listen on the board, and raise `_snd_mixahead` (0.1, at most 0.18) if it breaks up. The display's flash image still holds sounds the client no longer opens (about 1.6 MB to win for maps).
