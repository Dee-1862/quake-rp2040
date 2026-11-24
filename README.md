# QuakeOG

A Quake port to **one RP2040**: the display CPU of the FreeWili OG (264 KB RAM, 16 MB flash, no FPU). The board's second RP2040 runs only a small companion that boots the display CPU and kicks its watchdog (`apps/qog/main/main.c`, about 30 lines, all of it visible).

Status: platform layer running on the board (32 fps at 320×200 through the LCD pipeline). id's engine imported unmodified and building on Windows, with a RAM meter. See [docs/current-plan.md](docs/current-plan.md).

## Docs

- [docs/current-plan.md](docs/current-plan.md): where we are against the plan
- [docs/changes.md](docs/changes.md): what changed and why
- [docs/lessons.md](docs/lessons.md): mistakes not to repeat; **read before flashing**
- [docs/structure.md](docs/structure.md): rules that keep the tree small
- [docs/game-data.md](docs/game-data.md): how to get and place pak0.pak
- [docs/ram-meter.md](docs/ram-meter.md): measuring static and dynamic RAM
- `QuakeOG Port Plan.html`: the original full plan (kept locally, not in the repo)

## Setup

Needs the Raspberry Pi Pico VS Code extension's toolchain under `~/.pico-sdk` (SDK 2.3.0, GCC `15_2_Rel1`, ninja 1.13.2, picotool 2.3.0), CMake ≥ 3.21 and Python 3.

```bash
git clone https://github.com/freewili/wiliOGbsp third_party/wiliOGbsp
git clone https://github.com/next-hack/MG24Quake third_party/MG24Quake   # reference only
python -m pip install pyserial    # for fw.py flash/console; use a venv if you prefer
```

## Build

```bash
cmake --preset target            # target-posix on macOS/Linux
cmake --build build --target qog_main
```

This produces `build/apps/qog/qog_main.uf2`, which carries the display image inside it.

## Flash (app)

If the board has never had the BSP's display bootloader, do that once from the BSP tree (`fw bootloader`, see its README). Then:

```bash
python third_party/wiliOGbsp/tools/fw.py flash qog --uf2 build/apps/qog/qog_main.uf2
python third_party/wiliOGbsp/tools/fw.py console     # watch the [qog] timing line
```

Never copy a display app's UF2 by hand. See [docs/lessons.md](docs/lessons.md), L4.

## Writing the data region

```bash
python tools/mkdata.py -o build/qogdata_test --test-pattern     # no game data needed
python third_party/wiliOGbsp/tools/fw.py bootsel --cpu display  # only the display CPU's drive appears
# copy build/qogdata_test.uf2 to the RPI-RP2 drive
# the display reboots into its bootloader: press GREEN to start the app
```

The data UF2 writes only from 0x10100000 up (QOG_DATA_OFFSET in platform/qog_flashmap.h), so the app and its metadata are untouched. Hold green while the app boots to time a full CRC check of the region. *This procedure has not yet been tested on a board.*

## Controls (platform test)

| Input | Alone | With green held |
| --- | --- | --- |
| Gray | Forward (box up) | Back (box down) |
| Yellow | Turn left (box left) | Jump (box hops) |
| Blue | Turn right (box right) | Crouch (box shrinks) |
| Red | Next weapon (tap; box colour) | Previous weapon (tap) |
| Green | Fire (box red) | Modifier |
| Yellow + blue | Menu (box grey) | |

Tilt nudges the box. **Holding red for 6 s powers the board off**, which is why nothing held for long is on red.

## Licences

QuakeOG's engine code will be GPL (from id Software's GPL release and MG24Quake, GPL-3.0). The BSP is under the FreeWili Hardware License, which is not open source. Whether the two can be combined is an open question for FREE-WILi; nothing will be published until it's answered. Quake's game data belongs to id Software. Never commit it. `mkdata.py` converts your own copy.
