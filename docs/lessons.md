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

**L8. A display update erases only the sectors its image needs** (`bl_receiver.c`). That's why a game-data region high in flash survives code updates. After a data UF2 is written through the display's BOOTSEL drive, the display CPU reboots into its bootloader and **waits for RUN**. Press green, or power-cycle, to start the app. *Reasoned from code, not yet tested on a board.*

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

**L17. The board may arrive with someone else's BSP firmware** (ours had "wilirehab 002"). Check the USB product strings before flashing. If they start `FWOG display`, the bootloader is already there and `fw bootloader` isn't needed. Flashing replaces that firmware, so ask first.
