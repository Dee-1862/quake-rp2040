# Lessons learnt

Read this before touching the build, flash layout or hardware. Each entry is something that cost time, or would have cost a board. Add new ones at the end and never renumber.

## Build and tooling

**L1. Point CMake at the prebuilt picotool and pioasm.** Without `picotool_DIR`, the SDK downloads picotool's source and tries to build it with a host C compiler, and this PC has none ("No CMAKE_C_COMPILER could be found"). The Pico extension ships prebuilt copies under `~/.pico-sdk/picotool/2.3.0/picotool` and `~/.pico-sdk/tools/2.3.0/pioasm`, and our `CMakePresets.json` sets both. If a fresh configure fails this way, delete `build/` and configure again with the preset.

**L2. `fw.py` targets the BSP's own tree.** For our project use `cmake --preset target` and `cmake --build build --target qog_main`. Flash with `python third_party/wiliOGbsp/tools/fw.py flash qog --uf2 build/apps/qog/qog_main.uf2`. `fw` needs `pyserial`, so install it in a venv or let the user install it. A global `pip install` was blocked in this session.

**L3. No host C compiler on this machine.** Host tests are Python for now. C logic that should be host-tested (`qog_input_step`, `qog_data_check_header`, `qog_video_build_map`) is written as pure functions behind `#ifndef HOST_TEST`, so they can be tested as soon as MSYS2 MinGW GCC is installed. The BSP README recommends that compiler for its own host tests.

## BSP and hardware rules (from wiliOGbsp AGENTS.md; these bite)

**L4. Never UF2-copy a display *application*.** It lands at 0x10021000 without the metadata sector at 0x10020000, the bootloader refuses it, and the display CPU drops off USB. Display apps ride inside the main UF2 (`fw flash`). Data-only UF2s at 0x10100000 (0x10400000 before Agent AC) are a different case (see L8).

**L5. Every display app calls `fwog_power_poll()` every loop iteration and takes its buttons from the result.** Calling `fwog_buttons_poll()` a second time steals edges from the 6 s red-hold power-off. `qog_input_step()` takes `power.buttons.down` for this reason. Never add a watchdog to the display CPU. The main CPU must call `board_watchdog_kick()` every loop.

**L6. Red is both "move back" and the 6 s power-off.** In the plan's control table, walking backwards for 6 seconds powers the board off. **Confirmed on the board:** holding red to move the test box down starts the LED power-off countdown. **Resolved (user chose option 1):** red is tap-only everywhere. While implementing it we found that "back = green+red" still holds red. The BSP watches red's *level*, so a combo doesn't help, and every held action has to come off red entirely. Map v2: alone, red = next weapon; with green held, gray = back, yellow = jump, blue = crouch, red = previous weapon. Rule from here on: **never map a hold action to red, alone or in a combo.**

**L7. UART0 is the inter-CPU link on both chips.** Never `printf` to UART. Use `DIAG()`, which goes to USB CDC. Messages printed in the first second or two after boot are lost because USB hasn't enumerated yet, so repeat important ones.

**L8. A display update erases only the sectors its image needs** (`bl_receiver.c`). That's why a game-data region high in flash survives code updates. After a data UF2 is written through the display's BOOTSEL drive, the display CPU reboots into its bootloader and **waits for RUN**. Press green, power-cycle, or re-run `fw flash <app>`: the main chip finds the display image unchanged (the CRC matches, so no transfer) and sends RUN. *Verified on the board on 2026-11-22: the data survived and the app reported `data=ok`.*

**L9. Put only one CPU in BOOTSEL at a time.** Both chips present the same USB serial, so with two drives mounted you can't tell which is which.

## Performance facts

**L10. `st7789_blit()` is synchronous and byte-swaps on the CPU.** It's fine for UI, far too slow for frames. `qog_video` does its own RAMWR with 16-bit SPI frames over DMA instead. After a scanout it restores 8-bit mode, CS and the RX FIFO, so the BSP driver keeps working.

**L11. Flash XIP runs at 50 MHz** (clk_sys 200 MHz with flash CLKDIV 4). Texture and model reads from flash will cost more than on MG24Quake. Measure `copy=` in the `qog` console line, which times a 64 KB `memcpy` from XIP. A faster flash divider would be a BSP change: ask first.

**L12a. LCD scanout is a fixed 22.4 ms per 320×200 frame, whatever the source resolution** (measured). With a single buffer, put every piece of work that doesn't touch the frame buffer (game logic, physics, BSP traversal, edge setup) between `present()` and `wait()`, so it runs while the scanout is in flight. Only span drawing has to wait.

**L12. The RP2040's 264 KB is 256 KB main SRAM plus two 4 KB scratch banks**, and the stacks live in the scratch banks. Budget Quake against the 256 KB main bank minus BSS and heap, as listed by `arm-none-eabi-size`.

## Process

**L13. Measure, then write the number down with where it came from.** The plan's estimates were wrong in our favour (32 KB vs about 8.5 KB). The next one may be wrong the other way.

## Found on hardware

**L14. Never use the SIO FIFO for your own messages once `multicore_lockout_victim_init()` has run.** It installs a core-1 FIFO IRQ that pops and discards anything that isn't a lockout request. Our frame pointers disappeared, core 0 waited forever, and the screen stayed black. USB still enumerated because it runs from an IRQ, which made it look alive. Hand data between cores through shared volatile memory plus `__sev`/`__wfe`, as `qog_video.c` does now.

**L15. A silent console usually means core 0 is stuck, not that USB is broken.** Make every status line print even on failure paths, so "nothing printed" can't mean two different things.

**L16. Sensor reads at boot can come before the first sample.** `lis3dh_process()` returns false until data is ready. Calibrate by averaging several samples with a timeout, not with one read.

**L18. Turn the backlight on yourself.** `st7789_init_*` leaves it off, and every BSP app calls `board_backlight(255)` after init. Without it the console reported 32 fps of perfect frames while the screen looked blank. A good-looking console doesn't prove the screen is lit: always check the panel with your own eyes.

**L19. Pressing an edge button tips the board.** With tilt steering on, red and yellow looked as if they "did random things", because the tip turned into fast drift at 40 fps. Keep tilt off unless you're testing it, scale it gently, and treat it as an aiming aid, not a mover. Test-app colours must also contrast with the background (we use greys plus a black outline).

**L20. This PC has MSVC** (VS Build Tools 2022, `cl.exe` 14.44). A Windows build of the engine is possible, and host C tests too: L3 is no longer a blocker.

## Engine work

**L21. Never edit engine files with `sed -i` on Windows.** It silently turned CRLF into LF and made a 41-line rename look like an 11,084-line diff. Use the Edit tool, or a script that keeps the line endings, and check `git diff --stat` before every engine commit.

**L22. id's code says `_WIN32` when it means "id's Win32 port"**, and it says `__i386__` when it means "use the x86 assembly". Both are now neutral (`ID_WIN32` is never defined, and MSVC doesn't define `__i386__`), so every build takes the portable C paths, which are the ones the RP2040 needs.

**L23. GCC 15 defaults to C23, where `true` and `false` are keywords.** id's `typedef enum {false, true} qboolean` then fails. Compile the engine with `-std=gnu11` (the meter does, and the board build must too). GCC 14+ also makes missing prototypes an error, which id's `chase.c` has. Fix those in the engine rather than hiding them, when we get there.

**L24. Keep the engine import pure.** Copy id files byte for byte in their own commit, and make every change afterwards a separate, single-purpose commit. That keeps the GPL provenance exact and makes review possible.

**L25. Commit message rules (user):** a one-line title, a bullet-point body, no em dashes, no semicolons, no colons, and no assistant attribution anywhere in code, comments or commits.

**L26. Smart App Control (Windows 11) blocks freshly built, unsigned exes.** The first PC build ran once, then rebuilds were refused ("An Application Control policy has blocked this file", Code Integrity event 3077/3118). Every rebuild has a new hash with no reputation. Don't try to work around it: it's the user's security setting. Options are in docs/game-data.md. Firmware work is unaffected, since that code runs on the board.

**L27. The shareware data was extracted without running any 1996 code.** `quake106.zip` (MD5 `8cee4d03…1357`) holds `resource.1`, an LHA self-extracting DOS exe. A pure-Python lh5 decoder read it as data and checked each member's CRC-16. Result: `pak0.pak`, 18,689,235 bytes, MD5 `5906e599…3abb`. Never run `deice.exe` or the SFX itself.

**L28. Engine console commands must be registered during `Host_Init`** (`Cmd_AddCommand` errors once `host_initialized` is set). Register port commands from `VID_Init`. Scripted runs end with `qog_quit`, because `quit` can open a confirmation menu and hang.

**L29. To override a saved setting, wait for `host_framecount >= 1`.** `Host_Frame` skips its first calls to cap the frame rate, and the first frame that really runs executes `quake.rc` and then `config.cfg`. A plain call counter fired too early, before the config. `Cbuf_AddText` queued the command behind everything `quake.rc` stuffs in, including any scripted quit. Verify through the saved `config.cfg`, not a `+cvar` query on the command line, which runs inside `quake.rc` before late settings apply.

**L30. id ships with the crosshair off.** With buttons only, the crosshair and slow-start turning (fine aim) are what make aiming possible. Keep both in `port/common/` so every port gets them.

**L31. id's `_Host_Frame` calls `rand()` on every call ("keep the random time dependent"), including calls it then skips.** A port that calls `Host_Frame` faster than the frame cap gets a random sequence that depends on CPU speed. Only call it when a frame is due. Combined with `host_framerate` and the shared `qog_rand`, runs become exactly repeatable.

**L32. `COM_InitArgv` keeps only 50 arguments, and Quake's tokenizer splits on `:`.** Long scripted runs go in a `.cfg` file run with `+exec` (`data/id1/qogtest_*.cfg`). File paths given to console commands must be relative (no `C:`).

**L33. Hash game state by meaning, not bytes.** QuakeC string fields are offsets from `pr_strings`, and entities hold link pointers, so any change in memory layout changes a raw hash. `qog_statehash` walks the global and field definitions and hashes strings by their text. That's what makes "flash on" against "flash off" comparable.

**L34. id's pak files put lumps at odd addresses** (`e1m1.bsp` and `progs.dat` both sit at 3 mod 4). In-place use needs a realigned image: the PC flash map does it now and the board converter must too. Keep each file's disk position (`filepos`) separate from its image position (`flashpos`). Mixing them up broke `gfx.wad` reads.

**L35. Prove each RAM cut with a test that would catch a regression:** the read-only pages catch writes, `qog_shot` plus a pixel diff checks rendering, and state hashes against `-noaot`/`-noflash` check game logic.

**L36. id's cache can move blocks (`Cache_Move`), which is why model data uses offsets relative to the header.** Data left in flash needs an absolute base pointer stored in the header (`aliashdr_t.flashbase`). Group descriptors stay header-relative because they live in the cache block. Check every `(byte *)paliashdr + …` site for which kind it is.

**L37. Mix at the rate the data is stored at (11,025 Hz for Quake)**, so sounds need no resampling and can be read in place. Any conversion should happen on the PC in the converter, not on the board. Only two shareware sounds (lightning gun, 22 kHz 16-bit) don't qualify.

**L38. Run scripted tests with `+volume 0` now that the PC build has real sound.** The mixer still runs, so the code paths are exercised, but the tests stay silent.

**L39. The shareware licence forbids modified, translated or derivative versions of the data** (`SLICNSE.TXT` §3a, b, c, e), and id's engine enforces it: a changed `pak0.pak` without the registered version stops with "You must have the registered version to use modified games". Trimmed paks, QuakeC translated to C, and images extracted for the board all count. Don't bypass the check. The route id intended is the registered or retail data (Steam or GOG), which the check accepts when `gfx/pop.lmp` is present. Use the shareware only to run the unmodified PC build. (Not legal advice. The user decides.)

**L40. Opening Options > Video crashed the PC port**, because id's menu calls `vid_menudrawfn` without checking for NULL. Every port must supply that hook (and `vid_menukeyfn`).

**L41. With real sound, demo frames are no longer repeatable.** id's channel picking depends on how far the audio device has played, and it consumes `rand()`, which shifts particle placement. Compare renders with `-nosound`. Compare against a build of the previous commit made in a separate `git worktree`, so uncommitted work stays put.

**L42. A worktree build of an older commit uses that commit's build settings.** Building the pre-opt-in commit regenerated the QuakeC translation from the shareware. Check for derived files after any reference build and delete them.

**L43. Automated PC runs must never show a window or make a sound.** Use `tools/pctest.ps1`, which always passes `-hidden` (draws off-screen, mixes sound without output, playback driven by game time), restores the fixed config, and enforces a timeout. Check sound with `qog_soundlevel` (peak and RMS), never by playing it.

**L44. Two PowerShell traps that cost a session:** `$args` is a reserved automatic variable (a parameter with that name silently arrives empty, so test games started with no script and played the attract demo with sound until killed), and variable names are case-insensitive (`$t` overwrote `$T`). Use descriptive names (`$Runner`, `$case`).

**L45. Pushes must not prompt.** Use `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never git push`. If it fails, ask the user to sign in once (a terminal `git push` or GitHub Desktop) rather than leaving a Credential Manager popup waiting.

**L17. The board may arrive with someone else's BSP firmware** (ours had "wilirehab 002"). Check the USB product strings before flashing. If they start `FWOG display`, the bootloader is already there and `fw bootloader` isn't needed. Flashing replaces that firmware, so ask first.

**L46. The ARM bare-metal ABI makes enums as small as their values.** `qboolean`, `modtype_t` and `synctype_t` are one byte on the RP2040 build and four on the PC, so every engine struct with an enum had a different layout and the board refused the PC-baked flash image ("maps/e1m1.bsp is a stub ... does not match"). `-Q --help=optimizers` showed `-fshort-enums [disabled]`, which was misleading. The engine and every file that includes its headers now build with `-fno-short-enums` (PUBLIC on qog_engine). Check struct sizes on both compilers with a probe file whenever a layout must match across PC and board.

**L47. Read only the memory map section of a GNU ld map file.** The "Discarded input sections" list at the top uses the same line format, so a naive parse counted buffers that the linker had already dropped (a whole change saved nothing on the fly-through because of this). Start parsing at "Linker script and memory map".

**L48. Quake's vector macros name their arguments more than once.** `VectorCopy (p, arr[n++])` increments `n` three times. Never pass an expression with side effects to `VectorCopy`, `VectorAdd` and friends.

**L49. A board app's RAM is found by measuring each peak on the PC first.** For the fly-through: `-heapsize` and `-zone` on the PC give the true hunk floor (init peaks came from the particle pool and the pack directory read), and the stack is the deepest chain of `.su` frames. Then the link's overflow figure says exactly how much is still missing.

**L50. Only engine files are built with `-Drand=qog_rand`.** A host file in `port/` or `apps/` that calls `rand()` gets MSVC's or newlib's, so the shared random sequence skips a step and every particle after it lands elsewhere. Call `qog_rand()` by name there (port/common/qog_demo.c does for `_Host_Frame`'s per-frame call).

**L51. A client without a server still reads server cvars.** The client's particles fall by `sv_gravity`, which only SV_Init registers. Unregistered, its value is 0 and blood and debris float. Any host that leaves the server out must register it (or define it, on the board).

**L52. Text a demo stuffs waits behind a script's `wait`s.** `stufftext "bf"` is appended to the command buffer, so in a scripted run it runs only after the rest of the script, and the bonus flashes come late or not at all. A script that compares demo frames must end right after starting the demo and leave the buffer free (`qog_quitafter`, port/pc/demo_pc.c).

**L53. A zone's sampled peak is not its size.** The client zone used 3.2 KB at every frame, yet 6 and 8 KB zones failed with "Z_Malloc: failed" at startup or at a level load. Between frames the zone fills and fragments: every command line's tokens were allocated and freed there, around long-lived bindings, cvar values and light styles. Size a zone by running the scripts with `-zone N` until they pass, then keep margin. `QOG_CMD_ARGV_ARENA` takes the tokens out and an 8 KB client zone ran everything.

**L54. tools/stack_chain.py reads every .su file under the folder it is given.** After apps/qogdemo or quake_main were built in the same build folder, their copies of engine functions (the demo's engine has id's 16 KB translation table) made the client's deepest chain look 3.5 KB deeper (36,184 instead of 32,712 bytes). Measure one app at a time, or copy its library, its objects and their .su files to a folder of their own.

**L55. Never wait for a key inside a loop of your own on the board.** id's `SCR_ModalMessage` ("start a new game?") spins on `Sys_SendKeyEvents` until Y, N or escape, but the board's buttons are read only by the main loop and the link wait (`fwog_power_poll`), and it has no Y or N, so New Game would have hung the display. Ask inside the menu instead (`m_confirm` in engine/menu.c with `QOG_BOARD_MENU`). Scripts that test a menu which starts a level must end with the key that starts it: the menu's `Cbuf_AddText` runs after the rest of the script (L52). The default PC build's reference `qogtest_hash` value (`ce86f894f9de`) is the MD5 of its two `qog_statehash` lines, each ending in a newline.

**L56. A board app can run faster than the BSP without touching the BSP or the display bootloader.** `board_init()` is compiled once into the BSP library, so a per-app `FWOG_SYS_CLK_KHZ` never reaches it. Raise the voltage and the clock after `board_init()` instead, then re-derive what it already derived from the old clock: the display's backlight PWM and I2C1, main's FPGA clock (raise that divider before the switch so the gateware never sees more than 25 MHz). Do it before any other driver starts, so they take their dividers from `clock_get_hz()` at the new clock (port/rp2040/clock_rp2040.c).

**L57. The flash divider is a run-time register, and boot2 puts it back.** The SSI's BAUDR and RX_SAMPLE_DLY can be changed from RAM code with interrupts off and the other core off flash (SSIENR 0, write, SSIENR 1). Check the new setting by hashing flash through the uncached alias 0x13000000 at the old and the new divider, inside the same RAM routine, and write the old values back before returning if they differ. Every SDK flash erase or program ends by running boot2 again, which restores the boot2 divider (4 here), so main re-applies the checked divider after its FAT writes.

**L58. GCC already divides in single precision when a float quotient is stored in a float.** `lzi = 1.0 / transformed[2]` compiled to `__aeabi_fdiv` on the board, MSVC kept the double divide. Check the disassembly (`objdump --disassemble=<function>`, count the `__aeabi_d*` calls) before counting a double saving. The doubles GCC keeps are compares with double constants (`NEAR_CLIP`, `BACKFACE_EPSILON`), `ceil`, double locals and `+ 0.5`. Each can be made float only where the result is provably the same.

**L59. Never run two PC tests at once on one data folder.** Every run restores `config.cfg` at its start and writes it at its end, so a run that starts while another quits can read a half-written config. One of three parallel runs of qogtest_f_hash got a different state hash and 90% different frames, and alone it matched. Run tests one after another, or give each its own `-basedir`.

**L60. The Bash tool's here-documents eat backslashes.** `\\` arrived as `\` and `\n` inside a Python string became a real newline, which broke macro continuations and a printf format. Edit C files with the Edit tool, or write the script with the Write tool and run it.

**L61. Long affine spans cost little where 1/z does not change along the row.** MG24Quake's fixed 32-pixel sub-spans changed 14% of the pixels of the test shots, 8 to 32 pixels chosen by how much 1/z changes over the sub-span (8% at most) changed 1.5% and still saved 43% of the divides of a fly-through, because floors, ceilings and walls seen straight on are most of the picture.
**L62. A test script longer than the command buffer is dropped whole.** `exec` of a 12 KB cfg printed "Cbuf_AddText: overflow" once, never ran a line of it, and the game played the attract demos until the timeout. Keep each scripted cfg well under 8 KB and chain the next one with an `exec` on its last line (`qogtest_y_manifest*.cfg`).

**L63. Give every split pair that runs at the same time its own `-linkport`, and pass it once.** `COM_CheckParm` takes the first `-linkport` on the command line, so a runner that adds its own after a default one silently put two pairs on one port, where clients may reach the other pair's server.

**L64. Compare RAM cuts with the fixed-frame scripts, size the hunk with the fight tour.** The `qogtest_*_mem_<map>` scripts run at `host_framerate 0.05` and give the same smallest `-heapsize` every time. A `qog_tour_fight` ends a level with a different number of client entities from run to run (184 to 198 on e1m3), so its hunk low moves by a 16-entity block, but it reaches more of the level and needs up to 2 KB more than the script.

**L65. The z-buffer between views is memory with an owner per byte range.** Today: the status bar rows (15,360 B), MSG_ReadString's fallback (2 KB), the link's receive buffer (7.5 KB), and the precache names at the very end. Anything placed there must be empty or dead whenever R_RenderView starts, and should check that at run time as `Link_RxCheckIdle` does.

**L66. In the agents' bash tool a heredoc is not always literal.** Two backslash-n pairs in a quoted heredoc fed to Python arrived as a real newline and broke C string literals in qog_phase.c. Write text with backslashes through the Write or Edit tools (or build the bytes with chr), and grep the result for a broken string before building.

**L67. Build folders keep their CMake cache values after main changes the defaults.** A reused build folder still had the old client hunk and stack sizes and reported RAM 5 KB away from a fresh one. Take `end` numbers from a freshly configured folder.

**L68. Every level of a recursion carries the whole frame of the recursive function.** R_RecursiveWorldNode kept the frustum test's box corners and points in its frame at every BSP level. Moving the test into a function that is not inlined (R_QogCullNode) cut the frame from 96 to 48 B per level and made it faster.

**L69. The XIP cache counters (XIP_CTRL CTR_ACC, CTR_HIT) count both cores and saturate at 2^32.** Read them as differences around a phase and clear them between reports, when no phase is open. Core 1's span half lands in the spans phase.

**L70. A command the server half has the client run inside an exchange can draw a view.** `reconnect` (SV_SendReconnect, every changelevel) draws the loading plaque over the old level while the reconnect message already waited in the z-buffer receive buffer, so the board client stopped at every level exit. Scripts that change level with `map` from the client never reach it, because the client disconnects first. Test level changes with `changelevel` too (`qogtest_ad_chg.cfg`), and look at every `Link_ServerClientExec` for what it draws.

**L71. Measure the client hunk on the board images, not on data/id1.** On the PC a pak in data/id1 is laid out differently from its flash copy, so the client keeps a `packpos` block per pak (1,744 B for pak0 and pak1) that the board never has. Build the episode's images with `mkboard.py --stub-maps --main` and run the pair with `-basedir <img>` for the client and `-basedir <img>_main -flashbase 0x10200000` (0x10300000 before Agent AC) for the server, with the test cfgs copied into both id1 folders.

**L72. A noclip tour walks into level exits and dead teleporters.** A spot inside a `trigger_changelevel` changed the level halfway through a tour (the hunk lines then belong to another level), and e4m5 and e4m8 each have a `trigger_teleport` aimed at an `info_null`, which QuakeC removes at spawn, so touching it is a Host_Error in id's own game. The tour now takes both out before it starts. Give every pair its own `-linkport` and its own data folder, a tour and a hash run on one port is a race (L63).

**L73. Before two structs share bytes in a union, list every reader of each field.** `efrag` and `visframe` of entity_t are read only through efrags, which only statics get, and `topnode` only while its bmodel is drawn, which `grep -n "\->field"` over engine/*.c showed in five lines. The one call that touched a client entity's efrag (R_RemoveEfrags of an emptied entity) did nothing in id's game, so it can be skipped, but it would have written into the network state.

**L74. MSVC compiles a call to an undeclared function as returning int.** `COM_FlashName` was declared only under the server's flag, the PC client built and passed every test (32-bit pointers survive the int), and only the ARM build stopped. Build the board after any change to a header guard.

**L75. The server's deepest stack is a pusher and a point trace, not QuakeC.** Weighed in board bytes (L76), every episode 1 script peaked inside `SV_PushMove`, whose two lists of 600 entries were 9,600 B of one frame, and the fight tour went deeper in `SV_RecursiveHullCheck`, 104 B a level on the board and as many levels as the hull's tree is deep. Hull 0 (points: grenades, nails, `traceline`) of e1m3 is 85 levels deep, so a grenade riding a plat there can take about 9 KB of recursion under the pusher's frame. tools/stack_chain.py cuts recursion and cannot see either, so bound it by the map (`python tools/hull_depth.py data/id1/pak0.pak,data/id1/pak1.pak e1m3` prints the tree depths).

**L76. The PC can weigh every call of the server in board bytes.** Build quakeog_sv with `/Gh /GH /Ob0` (`QOG_SV_STACKPROBE`), keep a shadow call stack in `_penter` and `_pexit`, and add each function's ARM frame from the board build's `.su` files (`tools/stack_chain.py --su-table`). Popping by the stack pointer alone is wrong on x86: cdecl pushes the arguments before the call, so a sibling called with fewer arguments looks like the caller of the next one, which showed `Con_Printf` nested four times. `_pexit` pops, the stack pointer check only cleans up after `longjmp`.

**L77. The split pair's fight tour does not repeat, even without pipelining.** Frame hashes of two runs of the same build differed from frame 652 on. To compare two ways of drawing the same view, draw it twice in one run from the same state (`QOG_ZEDGE_COMPARE`): put the particles and their list heads back and push the dynamic lights again before the second `R_RenderView`, or the second picture differs for those reasons alone.

**L78. A drawer's z writes can be sent away without touching the drawer.** Every span computes its z address as `d_pzbuffer + d_zwidth * v + u`. With `d_zwidth` 0 and `d_pzbuffer` at a frame row no span has drawn yet, all of a flush's z lands in that row and the real z-buffer, here holding the edge table, is left alone (r_edge.c, `R_QogFlushNoZ`).

**L79. A script run with `-basedir <board image>` must be in that image's `id1`.** Otherwise `exec` finds nothing, the game plays its demo loop (none in the image) and waits for the timeout. mkboard does not copy the test scripts (as L71 says, copy them into both id1 folders).

**L80. `git fetch` in a worktree can wait for credentials with no prompt.** To take main's latest, merge the local `main` branch, which every worktree shares, with `GIT_TERMINAL_PROMPT=0`.

**L81. `sed -i` in the agents' bash rewrites a CRLF file with LF endings.** A one-line `sed -i` on engine/menu.c turned all 4,005 of its CRLF line ends into LF, so the diff became the whole file. The engine files are `-text` and keep id's CRLF bytes (.gitattributes). Edit them with the Edit tool or a script that reads and writes bytes, and check `git ls-files --eol <file>` before committing.

**L82. A timedemo on the PC measures the PC's pacing, not its speed.** sys_pc.c's main loop runs at most 72 frames a second and sleeps in steps of a millisecond, so `timedemo demo3` gave 54 to 57 fps on quakeog_pc and the split pair alike, Exact or Fast. Only the board's number says how fast the code is.

**L83. To run a script's lines after the text a menu queues, bind a key and press it after the menu key.** The menu's `Cbuf_AddText` goes behind the rest of the script (L52), and `exec` inserts in front. A key binding pressed with `qog_key` in the game appends its text behind the menu's, so `bind f12 "exec after.cfg"` then `qog_key enter f12` runs the menu's `timedemo` first and the script's waits and shots during and after it.

**L84. Check a demo exists before anything that ends the game for it.** id's `timedemo` disconnects in `CL_PlayDemo_f` before it opens the file, and sets `cls.timedemo` even when the file is missing, so a missing demo ends the game and leaves a timedemo flag no demo will clear. The board's Benchmark checks `COM_HasFile` before its prompt. mkboard leaves `demo*.dem` out of the images unless `--demos` is given.

**L130. A blocking write on a flow-controlled UART serialises the two chips.** The board UART has 32-byte FIFOs and each side reads only while it pumps the link. Main's answer to a sound operation sat in `qog_link_hw_write` for the whole 70 ms the display drew, so the client's tick waited unread and main's 10 ms frame ran only once the display came back to wait for it: the link wait equalled main's frame although the pipeline was on. The PC's TCP socket never blocks, so the PC could not show it. Send in the background (DMA) or never send to a peer that is not reading, and give the board a counter of the time spent waiting to write (`txstall`).

**L131. With an overlapped scanout the frame is bounded by what the next frame must overwrite first.** Once the link was fast, the next view could only start its world front end when the LCD scanout had read the frame buffer rows that the edge and vertex caches use (136 of E1M1's 152 view rows, about 12 ms), so a faster link alone saved nearly nothing: the time moved into the scan wait. Measure the wait for the scanout next to the link wait before counting a link saving.

**L132. A frame that is not sent to the LCD must be proven equal to what the LCD shows.** The status bar skip is checked on the PC by keeping an emulated panel (VID_Update copies the rows it sends), taking shots from it, and in a check build repainting anyway and comparing the rows not sent with the panel's. A palette change makes a partial scanout wrong even when the pixels are equal, since the panel converts with the palette of the scan.

**L133. In the agents' bash tool, write any Python that contains a backslash escape with the Write tool and run the file.** A heredoc turned `\\n` inside a Python string into a real newline three times in one session (L60, L66 again), so the replacement text never matched.

**L140. Count a hash's fallback before tuning the work it indexes.** The board client's lit surface hash has 256 entries (`QOG_DLIGHT_SURFS=256`), and in timedemo demo3's fights the pass that marks the visible surfaces a light reaches overflowed it. Every lost light of every lit surface then went to R_LightReaches, a float search of the light's part of the BSP tree: 1,613 node visits a view on the PC with 256 entries against 0 with 1,024, more float work than all the dynamic light cells together. Marking only surfaces that face the eye halved it, and finding the surface's node by binary search and testing the light up the parents took it to about 0.5 a view.

**L141. A phase timed per surface can hold a pass that runs once a frame.** With `QOG_DLIGHT_SPARSE` the world's light marking (R_MarkLights for every light) waits for the first question about a lit surface, which comes from R_BuildLightMap, so it is timed as lmap. Read where a lazy pass actually runs before splitting a phase's time by its per-surface work.

**L142. `unsigned += float` adds in float, and its rounding is a threshold.** id's `blocklights[i] += (rad - dist)*256` turns the cell into a float, adds and truncates back. With rad at least 1 the product is exact, so the only rounding is the sum's, and the sum truncates one higher exactly when its whole part reaches a power of two set by the fraction of 256 rad, the same for every cell of a light. An integer loop then gives id's bits (`QOG_DLIGHT_INT`), and a check build that runs both ways on every pair of three timedemos (`QOG_DLIGHT_CHECK`) is the proof to keep.

**L143. A float truncated to an int is exact in integers when the rounding cannot reach an integer.** `sd = local - s*16` truncates a float difference. Below 4096 the difference's rounding error is at most 2^-13, so when local's fraction is 0 or at least 2^-11 from a whole number the truncation is the exact one, A - 16s or A - 16s + 1 for negative values, and outside that margin id's float loop runs. Bound the error, decide outside it, fall back inside it (L122).


**L100. Model the XIP cache on the PC before buying RAM for it.** MG24Quake copies each texture into a 20 KB buffer and keeps the 16 KB colour map in RAM because its external flash is not memory mapped. Here a PC model of the 16 KB two-way cache fed with every texel, colour map and lightmap read of the spans (`QOG_XIPSIM`, engine/d_xipsim.c) gave about 2,100 misses a view in the demos, about 0.55 ms at 125 MHz flash, and the texture and colour map in RAM would save at most that. Neighbouring pixels share 8-byte lines and a surface's texels stay cached while it is drawn.

**L101. On the M0+ the lit spans pay for float work per span, not per pixel.** Count the `bl __wrap___aeabi_f*` calls of a drawer in `objdump -d` and multiply by the spans a view (1,758 in demo3): Agent AA's Fast drawer made about 40 float calls a span (about 2,150 cycles through the ROM routines), more than its 48,000 pixels cost. Integers per span (64-bit sums, one hardware divide per sub-span end) took it to about 560 cycles.

**L102. The SIO interpolators make a texel address and its step one register read.** With ADD_RAW on both lanes, shift 16 and masks for s and the row of t, INTERP0's POP_FULL returns BASE2 (the texture) plus both masked lanes and adds BASE0 and BASE1 to the accumulators, as the pixel loop's own C does. Each core has its own pair, so each core sets them per surface. The PC emulates the registers with the same rules (d_litspan.h), so the PC's frames test the loop, and the board checks the hardware once against the C expressions before it trusts them.

**L103. A smaller hunk allocation is only static RAM when the hunk shrinks with it.** The console's text comes from the client hunk, so cutting it 3 KB freed nothing until the display's `QOG_HUNK_KB` dropped by 3 too. Doing the subtraction in apps/quake/CMakeLists.txt keeps the lead's `-DQOG_CL_HUNK_KB=75` meaning Agent AD's measured hunk.

**L120. A fixed-point rewrite is checked against the float code point by point before any picture.** The first integer front end changed 1.2% of the pixels even with the exact spans, and the cause (the first scan line's u stepped with 1/256 of its slope) only showed when a check build projected every point both ways and printed the mean and worst error. With it fixed the same shots changed 0.07%.

**L121. Approximate front end changes are amplified by the Fast span drawer.** Agent AA's sub-spans start at each span's first pixel, so a span end moved by one pixel shifts every sub-span join of that row and changes texels along the whole span. Measure a front end change with the exact spans first (`QOG_FRONT_FAST_ALWAYS`), then in Fast mode.

**L122. An integer test can be exact if it decides only outside its error bound.** Bound the integer error and the float rounding of id's expression (here 0.5 and 0.003 times the sum of the corners' magnitudes), and fall back to id's float statement inside that margin: the node culling then gives id's answer in both modes for a twentieth of the cost.

**L123. Ill-conditioned points are where approximations differ most.** A point near the eye plane (z clamped to id's near clip) projects to a u that depends on every rounding, so the integer projection hands such points, and points far off the view, to id's float code.
**L110. A byte store may alias any global, so GCC reloads every global step at every pixel.** id's D_PolysetDrawSpans8 read r_zistepx, r_lstepx, the three texture steps, acolormap and the skin width from memory through a literal pointer at each pixel (about 48 Thumb instructions a written pixel), because the stores to the frame and z-buffers are bytes. Copying them into locals before the loop gives the same pixels with about a third fewer instructions (`QOG_ALIASSPAN_FAST`). Look at the inner loop's disassembly for `ldr rX, [pc, ...]` before tuning arithmetic.

**L111. The Edit tool can rewrite a CRLF engine file with LF endings.** One small edit of engine/r_alias.c left it with no CR at all (the diff stayed small only because git normalises the comparison). Edit engine files with a script that reads and writes bytes and keeps the CRLF bytes, and check `git ls-files --eol` shows `w/crlf` before every commit (L21, L81).

**L112. id's AngleVectors takes three double sines and three double cosines.** On the M0+ they are soft double routines, and every alias entity pays them twice a view (R_AliasCheckBBox and R_AliasDrawModel each build the transform), with two float 3x4 products each. Fast mode builds the same transform from a 1,024 step sine table and integer products (R_QogAliasXform), and a bounding sphere test rejects most entities before any transform.

**L113. The split pair with the board client flags stops in qogtest_d_shots at the load of e2m3.** The server half exits with no message after `map e2m3` and the client then quits (`peer gone`), in an untouched build of 85a3d22 too, so that run gives 94 frames (the three demos, the spawn shots of start to e2m2 and the 8 fhash shots), not 121. Compare those 94 with a reference pair built the same way, and take the 121 frames from quakeog_pc.
**L90. A sed that reads and writes the same file empties it.** A loop that made each episode's `qogtest_y_mem_<map>.cfg` from the e2m6 one (`sed ... e2m6.cfg > e<n>m<m>.cfg`) emptied e2m6 when it reached it, and every script made after it. An empty script runs nothing, the client plays the attract demos and the server half spins at full speed waiting, which looked like a hang of the game. Check `wc -c` of generated scripts before a long run, and make them from a copy outside the folder.

**L91. Stopping a background runner leaves its test processes running.** TaskStop ended the bash loop, but its pctest, quakeog_cl and quakeog_sv went on, and the next run of the loop had already started. List the worktree's own processes by command line (`Get-CimInstance Win32_Process` with the worktree path in `CommandLine`) and stop each, never by name alone.

**L92. A level's server peak can be its load, not its play.** Host_Loadgame_f took its 8 KB line buffer from the high hunk after the level had loaded, so a load needed the level plus 8 KB: e2m7 with sound 179 KB of 174. On the board the buffer now lives on the stack once the map has spawned (`QOG_LOADGAME_STACKBUF`), 7 to 8 KB less on every level. Size the server with fight, save and load (`qogtest_y_mem_<map>.cfg`), never with a level load alone.

**L93. Every episode ends in start, and start leads to every episode.** e1m7, e2m6 and e3m6 exit to start (not the secret levels e2m7 and e3m7, which lead back into their episode), and start's gates lead to e1m1, e2m1, e3m1, e4m1 and end (`python tools/level_exits.py <paks> <maps>`). In an image of one episode a changelevel can name a level that is not there, and SV_SpawnServer sends the reconnect before it finds that out, which left the client waiting forever. Check the level before anything is stopped (engine/host_cmd.c, `QOG_MISSING_MAPS`).

**L94. `git commit -F` needs an empty line after the title.** Without it the title and every bullet line become one long subject line.

**L95. A file added to a data pak can move the baked image by 64 KB.** The image starts at the first 64 KB boundary after the pak, and main's episode 2 image has 28 KB to spare. Look at both `end` lines of mkboard after adding any file (qogboot.txt is 5 to 6 bytes and moved nothing).

**L160. Above 250 MHz the LCD's SPI prescaler, not the flash, decides which clock is worth it.** The PL022 gives clk_peri / (2 x n), so the panel's 62.5 MHz limit is met exactly only at 250 MHz (clk / 4). From 250 to 375 MHz it falls back to clk / 6: 50 MHz at 300, 44 MHz at 266. A slower scanout moves the race point of the next view later, so 266 MHz with the flash exactly at its 133 MHz rating loses more to the race than it gains in the CPU. Work out every clock-derived rate before choosing a system clock, and compare against the measured flash cost, which is small here (about 0.5 ms a view).

**L161. On the RP2040, clk_peri has no divider, and USB pins PLL_USB to a multiple of 48 MHz.** clk_peri can only be clk_sys, PLL_SYS, PLL_USB, an oscillator or GPIN. No multiple of 48 MHz gives both an exact 12.5 Mbaud (3.125 MHz x N) and 62.5 MHz SPI (125 or 250 MHz). Moving PLL_USB at run time also drops USB, the only way to put the display CPU into BOOTSEL.

**L162. Check every flash RX sample delay and keep the middle of the passing run.** The first delay that passes can sit at the edge of the data eye, and the edge moves with temperature and from chip to chip (main needed delay 2 at 125 MHz where the display took 1). Hash in chunks with interrupts off only per chunk, putting the old setting back after each, write the chosen setting only after the whole check, and read one block again now and then while running.

**L163. A table that holds a function's own addresses can be written after the link if its size is fixed.** A const array of fixed size in .rodata keeps every address the same, so a post-build script can read the ELF's STT_FUNC symbols and patch the table bytes into the .elf, .bin and .uf2 in place. Patch the display's .bin in its own POST_BUILD, before main's genimage embeds it. Read the array from another translation unit (or through an asm barrier), or the compiler folds its placeholder into the code.
