# QuakeOG

id Software's WinQuake, ported to the FREE-WILi OG board, whose two RP2040 microcontrollers (Cortex-M0+, no FPU, 264 KB of RAM each, no external RAM) run the game between them. The main chip runs id's server, the display chip runs id's client and renderer, and the two talk over the board's inter-chip UART the way id's client and server talk over a network.

<!-- Photo: replace this line with a picture of the board running E1M1, for example ![QuakeOG on the FREE-WILi OG](docs/img/board-e1m1.jpg) -->

For the full story (why it is hard, how it works, how it compares with other ports) see [docs/showcase.md](docs/showcase.md).

## Headline facts

| | |
| --- | --- |
| Hardware | FREE-WILi OG: two RP2040s, 264 KB SRAM and 16 MB QSPI flash each, 320x240 ST7789 LCD, five buttons, I2S speaker, WS2812 LED bar |
| Split | Main chip: server, QuakeC compiled to C, id's sound mixer. Display chip: client, renderer, status bar, menus, buttons, I2S output |
| Link | UART at 12.5 Mbaud with RTS/CTS flow control, a reliable stream carrying id's own client and server messages |
| Picture | 320x200, 8-bit palette, a 320x152 3D view, a one-byte depth buffer (user approved) |
| Speed, E1M1 on the board | 7.5 fps at 200 MHz with flash at 50 MHz. 10.4 fps at 250 MHz (1.20 V) with flash at 125 MHz. 10.9 fps with the Fast render mode |
| Exactness | The split game gives the same state hashes as one-process WinQuake on the PC. The Exact render mode is byte-identical to id's renderer over 121 PC test frames. Fast mode (the board default) changes about 2.8% of pixels on average, about 9% in the worst frame |
| Level data | Baked on the PC by `tools/mkboard.py` into flash images that the chips read in place through XIP |

As far as our search found (November 2026), this is the first Quake port to the RP2040 we know of, and the first we know of split across two microcontrollers. Other ports are faster: see the comparison in [docs/showcase.md](docs/showcase.md#other-ports).

## Status and limits

- **Running on the board:** the split game on E1M1 at the speeds above. Earlier board apps: the demo player (`apps/qogdemo`, 3 to 7 fps before the speed work), the renderer-only E1M1 fly-through (`apps/qogfly`) and a span benchmark (`apps/qogbench`).
- **In progress:** the display client now holds every episode 1 level in RAM (73 KB hunk, PC measured). The episode 1 flash images and the new flash layout they need are not on the board yet. More speed work is under way.
- **The whole game does not fit:** its display data is about 38 MB against 32 MB of flash on both chips together. The options are one episode at a time, or streaming levels from a PC.
- **Server RAM:** the server's 160 KB hunk covers most of episode 1, but e1m3 needs about 170 KB with sound (PC measurement).
- **One-byte depth buffer:** in rare views it needs an edge fallback that the board has no stack for, and the game stops with an error there (4 times in a PC fight tour of episode 1). Open.
- **Sound** plays through the board's speaker (heard by the user). **Saving** to the main chip's flash runs exactly on the PC and is not yet tried on the board.
- No multiplayer, no CD music, no keyboard console.

Details: [docs/handoff.md](docs/handoff.md) and [docs/changes.md](docs/changes.md).

## Game data

Quake's game data belongs to id Software. **It is never in this repository, and nothing made from it is committed** (`data/`, `*.pak`, `qogbake.bin` and board images are git-ignored). You supply your own copy and the tools convert it on your PC.

For the board use the registered or retail data (Steam or GOG, `id1\pak0.pak` and `id1\pak1.pak` in `data\id1\`). The shareware licence forbids modified or derived versions of its data, and the board images are derived (see [docs/lessons.md](docs/lessons.md), L39). Where to find the files and how to check them: [docs/game-data.md](docs/game-data.md).

## Setup

Needs the Raspberry Pi Pico VS Code extension's toolchain under `~/.pico-sdk` (SDK 2.3.0, GCC `15_2_Rel1`, ninja 1.13.2, picotool 2.3.0), CMake 3.21 or newer and Python 3. The PC build (needed to bake the board images) uses MSVC (VS Build Tools 2022).

```bash
git clone https://github.com/freewili/wiliOGbsp third_party/wiliOGbsp
git clone https://github.com/next-hack/MG24Quake third_party/MG24Quake   # reference only
python -m pip install pyserial    # for fw.py flash/console; use a venv if you prefer
```

If the board has never had the BSP's display bootloader, install it once from the BSP tree (`fw bootloader`, see its README). A board whose USB product strings start with `FWOG display` already has it (lessons L17).

## Build and flash the split game

These are the steps of [docs/handoff.md](docs/handoff.md). Run them from the repo root, and put only one CPU in BOOTSEL at a time (both chips show the same USB serial).

1. Build the PC version of the engine and record the asset manifests with it ([docs/game-data.md](docs/game-data.md)). Then build both data images in one run: `qb.uf2` for the display (written at 0x10100000), `qb_main.uf2` for the server (0x10200000). Start and all of episode 1 fit both chips:

   ```powershell
   python tools\mkboard.py --pak data\id1\pak0.pak --pak data\id1\pak1.pak --manifests build-pc-dev\manifests `
       --maps start e1m1 e1m2 e1m3 e1m4 e1m5 e1m6 e1m7 e1m8 --exe build-pc-dev\Release\quakeog_pc.exe `
       --stub-maps --main --display-no-sounds --display-no-progs -o build-board\qb
   ```

2. Build the game with every option given, since the build folder keeps old values (the main app carries the display app inside it):

   ```bash
   cmake --preset target -DQOG_SPLIT=ON -DQOG_AOT=ON -DQOG_AOT_PAK=<path to pak0.pak> -DQOG_SPLIT_MAIN_EMBED=quake_display -DQOG_SOUND=ON -DQOG_CL_HUNK_KB=75 -DQOG_CL_STACK_KB=32 -DQOG_SV_HUNK_KB=174 -DQOG_SV_STACK_KB=21 -DQOG_SYS_KHZ=250000 -DQOG_SYS_MV=1200 -DQOG_FLASH_FAST=ON -DQOG_FAT_FORMAT_IF_MISSING=ON
   cmake --build build --target quake_main
   ```

3. With the game running as it is, write the display's data: `python third_party/wiliOGbsp/tools/fw.py bootsel --cpu display`, then copy `build-board\qb.uf2` to the RPI-RP2 drive. The display then waits in its bootloader.
4. Flash the game: `python third_party/wiliOGbsp/tools/fw.py flash quake --uf2 build/apps/quake/quake_main.uf2`. Main reports a missing server data image until the next step.
5. Write the server's data: `python third_party/wiliOGbsp/tools/fw.py flash quake --uf2 build-board\qb_main.uf2`

The order and why are in [docs/handoff.md](docs/handoff.md).

Never flash the stand-in display image as the game. Read the consoles on the two chips' USB serial ports (main COM7 and display COM8 on the development PC), DTR on. Every 2 s they print frame timing lines (`[time cl]`, `[time sv]`).

Never UF2-copy a display *application* by hand (lessons L4). Read [docs/lessons.md](docs/lessons.md) before flashing.

## Controls

From the game's button mapping (`platform/qog_input.c`, v4):

| In the game | Button |
| --- | --- |
| Fire (tap one shot, hold keeps firing) | Gray |
| Forward | Yellow |
| Turn left | Green |
| Turn right | Blue |
| Next weapon | Red, tapped |
| Back | Red + yellow |
| Jump | Red + green |
| Swim down | Red + blue |
| Previous weapon | Red + gray |
| Menu | Green + blue |
| Power off | Red held alone for 6 s (the LED bar counts down in red). A combo with red never powers off |

| In menus | Button |
| --- | --- |
| Up, down | Yellow, blue |
| Left, right | Green, red |
| Enter, yes | Gray |
| Back, no | Green + blue |

The board menus drop Multiplayer and anything that needs a keyboard, and add a Controls page, a Levels page that lists the maps in the flash image, a Render Exact / Fast switch in Options, and yes/no prompts answered by buttons.

## Repository layout

```
engine/      id's WinQuake, imported unmodified, then changed in small single-purpose commits behind QOG_ flags
port/        id's Sys_/VID_/IN_/SNDDMA_ interfaces: pc/, rp2040/, common/
platform/    board drivers (video, input, data) built on the BSP
apps/        quake (the split game), qogdemo, qogfly, qogbench, qog (platform test)
pc/          the Windows build, for development and the exactness tests
tools/       mkboard.py, mkpak.py, qc2c.py (QuakeC to C), stack_chain.py, ram_meter.py, pctest.ps1
```

## Docs

- [docs/showcase.md](docs/showcase.md): the project explained for people new to it
- [docs/handoff.md](docs/handoff.md): where things stand and the flash steps
- [docs/changes.md](docs/changes.md): what changed, why, and what was measured
- [docs/lessons.md](docs/lessons.md): mistakes not to repeat. **Read before flashing**
- [docs/structure.md](docs/structure.md): rules that keep the tree small
- [docs/game-data.md](docs/game-data.md): getting the data and building the board images
- [docs/ram-meter.md](docs/ram-meter.md): measuring static and dynamic RAM
- [docs/current-plan.md](docs/current-plan.md): the original one-chip plan and its tiers
- Research: [hardware](docs/research-freewili-hardware.md), [embedded techniques](docs/research-embedded-techniques.md), [open source Quake bases](docs/research-open-source-quake-bases.md), [RP2040 games](docs/research-rp2040-games.md), [MG24Quake notes](docs/phase1-mg24quake-notes.md)

## Licences

QuakeOG's engine code will be GPL (from id Software's GPL release and MG24Quake, GPL-3.0). The BSP is under the FreeWili Hardware License, which is not open source. Whether the two can be combined is an open question for FREE-WILi, and nothing will be published until it is answered. Quake's game data belongs to id Software. Never commit it. The tools convert your own copy.
