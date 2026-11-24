# Lessons learnt

Read this before touching the build, flash layout or hardware. Each entry is something that cost time, or would have cost a board. Add new ones at the end and never renumber.

## Build and tooling

**L1. Point CMake at the prebuilt picotool and pioasm.** Without `picotool_DIR`, the SDK downloads picotool's source and tries to build it with a host C compiler, and this PC has none ("No CMAKE_C_COMPILER could be found"). The Pico extension ships prebuilt copies under `~/.pico-sdk/picotool/2.3.0/picotool` and `~/.pico-sdk/tools/2.3.0/pioasm`, and our `CMakePresets.json` sets both. If a fresh configure fails this way, delete `build/` and configure again with the preset.

**L2. `fw.py` targets the BSP's own tree.** For our project use `cmake --preset target` and `cmake --build build --target qog_main`. Flash with `python third_party/wiliOGbsp/tools/fw.py flash qog --uf2 build/apps/qog/qog_main.uf2`. `fw` needs `pyserial`, so install it in a venv or let the user install it. A global `pip install` was blocked in this session.

**L3. No host C compiler on this machine.** Host tests are Python for now. C logic that should be host-tested (`qog_input_step`, `qog_data_check_header`, `qog_video_build_map`) is written as pure functions behind `#ifndef HOST_TEST`, so they can be tested as soon as MSYS2 MinGW GCC is installed. The BSP README recommends that compiler for its own host tests.

## BSP and hardware rules (from wiliOGbsp AGENTS.md; these bite)

**L4. Never UF2-copy a display *application*.** It lands at 0x10021000 without the metadata sector at 0x10020000, the bootloader refuses it, and the display CPU drops off USB. Display apps ride inside the main UF2 (`fw flash`). Data-only UF2s at 0x10400000 are a different case (see L8).

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
