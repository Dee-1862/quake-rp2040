# FREE-WILi hardware research for the Quake port

Researched 2026-11-22. The local BSP (`third_party/wiliOGbsp`) is treated as authoritative for the OG board. Web sources are listed at the end. Anything marked **[unverified]** is an inference or comes from a single press article, and should be checked before we design around it.

Short answers:

| Question | Recommendation |
| --- | --- |
| 1. FREE-WILi 2 | It is a different class of machine: two RP2350B chips (8 MB PSRAM each, per the vendor), a 480x320 touch screen, ESP32-C5 Wi-Fi/BLE and USB host ports. $400 preorder, shipping Q4 2026. A port there would be easy, so keep the OG as the "hard mode" headline and treat FW2 as a later, low-risk target. |
| 2. Both RP2040s | Keep "one RP2040" as the goal, but build a seam now: Quake's own client/server split, with a UART net driver in place of loopback. That keeps a two-chip fallback cheap if 245 KB fails. The link is about 100x wider than Quake needs. |
| 3. Streaming levels | USB to a PC is the only practical path. The OG has no Wi-Fi, BT or Ethernet, and the CC1101 is too slow and legally constrained. A PC companion app can load a level in about 5–15 s. The data must be the user's own copy. |
| 4. Gamepad | Easiest: the PC companion reads the pad and relays it over USB CDC. On-device: an ESP32 running Bluepad32 on main's breakout UART1. PIO-USB host on the OG is not viable, and there is no USB host port. |
| 5. Other resources | Worth using: WS2812 LEDs, the I2S speaker, the accelerometer (optional tilt aim), the RTC and main's flash. Later, maybe: the CC1101 for two-board multiplayer. Skip: the FPGA (frozen gateware, slow path, being removed in future revisions), IR as a controller, and the mic. |

---

## 1. FREE-WILi 2 vs FREE-WILi OG

### FREE-WILi 2 facts

- **CPUs:** two **Raspberry Pi RP2350B** chips, one "main" (I/O and scripting) and one "display" (screen, buttons, audio, DVI). Not RP2040 and not ESP32.
  - The FW2 BSP README says: "RP2350B, 48 GPIO, 16 MB flash, 8 MB PSRAM". The PSRAM is an APS6404L, memory-mapped at 0x11000000 by the SDK before `main()`. The BSP clocks the chip at 250 MHz.
  - freewili.com says "8 MB SRAM and 16 MB flash per RP2350". The "SRAM" there is the 8 MB PSRAM. On-chip SRAM is the RP2350's standard 520 KB (from the RP2350 datasheet).
  - The RP2350 core is dual Cortex-M33 with single-precision FPU and DSP instructions. The RP2040 is dual Cortex-M0+ with no FPU.
- **Wireless co-processor:** ESP32-C5 ("Bottlenose") with dual-band Wi-Fi 6, BLE and 802.15.4. The FW2 BSP already has "peer streams" between display apps and ESP32 apps.
- **Linux module:** Raspberry Pi CM0 (BCM2710A1, 4x A53 at 1 GHz, 512 MB, 8/16 GB eMMC). Wi-Fi 4/BT 5.0 on the CM0 is optional. Desktop Quake would simply run on this.
- **FPGA:** iCE40UP5K with its own 8 MB SRAM.
- **Display:** 3.5" 480x320 capacitive touch (ST7796-class with FT6336U touch), plus DVI out from RP2350 HSTX.
- **USB:** three host ports (two 12 Mbps ports from the display RP2350 via PIO-USB, one 480 Mbps from the CM0) plus one USB-C for the PC.
  - The FW2 BSP ships a PIO-USB HID host for keyboards, mice, HID gamepads and **XInput (Xbox) controllers**. Its own doc says this is "not run on FreeWili 2 hardware" yet **[unverified on hardware]**.
- **Other radios:** CC1101 sub-GHz; LoRa (STM32WLE5JC/SX126x, Meshtastic); 125 kHz RFID; NFC (ST25R3916B); IR.
- **Input:** 5-way D-pad, A/B/X/Y, 14 "gamer" buttons, 5 context keys, chord keyboard.
- **Audio:** 0.5 W speaker, 3.5 mm jack, 4-mic array.
- **Sensors:** BMI323 IMU, BMM350 magnetometer, SHT40, OPT4001.
- **Storage:** two microSD slots (one for the firmware side, one for the CM0).
- **Expansion:** 20-pin FW1-compatible header, 10-pin analog header, Orca modules, 13 GPIO with selectable voltage, CAN FD.
- **Power and size:** 3000 mAh battery, 152.4 x 78.9 x 22.3 mm, 290 g.
- **Price and availability:** Founders Edition **$400 preorder** from shop.freewili.com, shipping Q4 2026 (CNX Software, 2026-09-09; freewili.com says "Shipping starts in Q4"). The shop page returned HTTP 429, so the price was not re-confirmed there **[unverified beyond press]**.

### Comparison

| | FREE-WILi OG (FW1 / Classic) | FREE-WILi 2 |
| --- | --- | --- |
| MCUs | 2x RP2040 (Cortex-M0+, no FPU); BSP clocks them at 200 MHz | 2x RP2350B (Cortex-M33 + FPU); BSP clocks them at 250 MHz |
| SRAM per MCU | 264 KB | 520 KB |
| PSRAM per MCU | none | 8 MB APS6404L, memory-mapped (vendor says per RP2350; FW2 BSP confirms the display CPU) |
| Flash per MCU | 16 MB W25Q128JV | 16 MB |
| Display | 320x240 ST7789, SPI at 50 MHz | 480x320 touch, plus DVI out |
| Buttons | 5 | D-pad, ABXY, 14 game buttons, 5 context keys, keyboard |
| Wi-Fi / BT | **none** | ESP32-C5 (Wi-Fi 6 2.4/5 GHz, BLE, Thread); optional on CM0 |
| Sub-GHz | 2x CC1101 | 1x CC1101 + LoRa |
| USB to PC | internal hub, "2 FS + 1 HS" interfaces (2 RP2040s + FT232H, inferred) | USB-C |
| USB host | **none** | 2x 12 Mbps (PIO-USB on the display RP2350) + 1x 480 Mbps (CM0) |
| FPGA | iCE40UP5K + 8 MB SRAM (frozen gateware in BSP; dropped in future OG revisions) | iCE40UP5K + 8 MB SRAM |
| Storage | flash only (main has an 8 MB FAT volume) | 2x microSD + eMMC |
| Sensors | LIS3DH accelerometer, MCP7940 RTC | 9-DOF IMU, magnetometer, environment, light |
| Audio | I2S speaker, PDM mic | 0.5 W speaker, jack, 4-mic array |
| Linux | no | Raspberry Pi CM0 |
| Battery | 1000 mAh | 3000 mAh |
| Price | not listed on freewili.com; Jameco lists the "FREE-WiLi Black (Def Con Edition)" at $339 | $400 Founders preorder, ships Q4 2026 |

Sources for the OG column: the local BSP and freewili.com/freewili-og.html (Rev. 20260803). That page says "16 MByte x 2 storage, 22 MByte usable", "ICE40UP5K with 8 MByte SRAM", "Integrated hub with 3 interfaces: 2 full-speed and 1 high-speed", a 1000 mA battery and "11 GPIO".

**What this means for us.** On FW2, the whole 1.85 MB build fits in PSRAM unchanged. The FPU helps the renderer, the D-pad and ABXY make controls easy, and Wi-Fi and USB host exist. That port would mostly be a display and input shim. The OG is the hard and interesting target. FW2 is a natural "phase 2" that reuses the platform layer.

---

## 2. Using both RP2040s on the OG

### What the BSP already requires of the main CPU (even today)

These come from the BSP's AGENTS.md and headers:

- **The main CPU must run a main app.** Display firmware is never UF2-flashed. It rides **inside main's UF2**, and `fwog_display_update_run()` on main streams it over the link, writes the metadata, then releases `GUI_NRESET`.
  - So today's "one-RP2040" build already depends on a main-CPU shim (the template).
  - The honest claim is "**all game code runs on one RP2040**; the second chip only boots it and kicks its watchdog".
- **Watchdog:** main arms an 8.3 s watchdog (the RP2040 maximum) and must call `board_watchdog_kick()` every loop iteration. A hung main resets the board and holds the display in reset.
  - Long flash writes on main must kick partway through. The BSP's FAT path does this every 8 sectors.
  - The display CPU must **never** have a watchdog.
- **FPGA:** `board_init()` loads a frozen 104,090-byte bitstream at boot, which takes about 170 ms at 5 MHz on the breakout SPI pins. No public loader exists for any other bitstream (contract rule 4).
- **Breakout I/O direction:** it involves three parties (FPGA io_buffer, the PCAL6416 on the display's I2C, and main's pads). Main initiates it with link messages 0x20–0x22, and **the display app must service them** (`fwog_ioexp_link_handle`).
  - Any Quake link protocol must share the frame and pick other type bytes. Types 0x01–0x1F belong to the bootloader; apps start at 0x20.
- **Flash-write hazard:** main's flash writes require core1 to be idle. If we start core1 on main, we must add `multicore_lockout`.

### What the main CPU could usefully do

| Role | Benefit | Cost / risk | Verdict |
| --- | --- | --- | --- |
| **Run the Quake server** (SV_*, QuakeC VM, physics, edicts) with the display CPU as client (renderer, sound, input) | The natural seam. NetQuake is already split into client and server, joined by a "loopback" net driver; replace it with a UART driver. The server's RAM (edicts, mutable QC globals, `sv` state, about 60–150 KB in our current layout) moves off the display chip. Both chips need the BSP: each has its own 16 MB flash, so the converted level data is stored twice. | Two firmware images, two flash copies of the level data, and per-frame messaging. Any drift between the two copies of `.bsp` data would be a fatal bug, so the data pipeline must write both. | **Best fallback.** Design for it, build it only if 245 KB fails. |
| Hold edicts as remote RAM ("far memory" over the link) | More RAM | Every field access becomes a link round trip of tens of µs. Quake touches edict fields constantly (QC, physics). Unusable. | No |
| **Level-data cache / streamer** (main holds levels on its flash or FAT volume and pushes them to the display flash between levels) | A second 8–15 MB store, so a second episode can stay on the board | Display-flash write speed is still the bottleneck (see §3). Saves nothing per frame. | Useful, low priority |
| Split rendering (each CPU renders half the screen) | Roughly 2x render throughput and a second surface cache | A 160-row 8-bit half frame is 38 KB, about 31 ms on the link. That eats most of a 66 ms frame at 15 fps. Both CPUs need the world data and the full client state. Very complex. | No (only if CPU-bound and desperate) |
| Sound mixer on main | Frees display CPU time | The speaker hangs off the **display** CPU's I2S, so mixed PCM must cross the link: 11 kHz × 8-bit is about 11 KB/s, trivial bandwidth. Mixer RAM is small anyway. | Possible, small win |
| Input aggregation (external pad via UART1 header, CC1101, IR) | Main owns the breakout header, so this is the only place an add-on can connect | Tiny | Yes, if we add a pad (§4) |

### UART link capacity

The link is UART0 with RTS/CTS on both CPUs (GPIO 0–3), at 12.5 Mbaud, 8N1.

| Item | Value |
| --- | --- |
| Raw rate | 1.25 MB/s **per direction**, full duplex |
| Framing | `[0x7E][len16][payload ≤ 4160][crc16]`, 5 bytes overhead. A 4 KB frame takes about 3.3 ms (BSP header). |
| Budget per frame at 10 / 12 / 15 fps | about 125 / 104 / 83 KB per direction |
| A Quake server→client datagram (`MAX_DATAGRAM` 1024 B in NetQuake) | about 0.8 ms on the wire, roughly 1% of a frame budget |
| The signon / reliable message (`MAX_MSGLEN` 8000 B) | about 6.5 ms, once per level |
| A small message (input usercmd, about 20–60 B) | about 20–50 µs on the wire, plus ISR/DMA latency |
| Full 320x240 8-bit frame | 76.8 KB, about 61 ms. That is about 92% of the link at 15 fps, so shipping finished frames is not realistic. |

- **The rate is not proven.** The link header says 12.5 Mbaud is the PL011 ceiling at divisor 1.0 with "no margin", and "has never been measured on the inter-CPU trace". The README says the link is verified, and there is a `smoketest` soak app. If 12.5 Mbaud proves marginal, the BSP's fallback ladder is 10, 8.33, then 6.25 Mbaud. Even at 6.25 Mbaud the client/server split uses only about 2% of the link. Run `apps/smoketest` before relying on it.
- **The FIFO is shallow.** The RX FIFO is 32 bytes, about 25 µs at 12.5 Mbaud. Use DMA on RX, or poll tightly; RTS/CTS covers short stalls.
- **Latency:** the client/server split adds about one frame of latency (Quake already predicts nothing client-side and was designed for 100–300 ms modem latency). A 1 ms link adds nothing noticeable.

### Other inter-chip paths

There are none worth having:

- UART0 + RTS/CTS (4 wires) is the only data path.
- `GUI_NRESET` (main GPIO 28 → display reset) is control only.
- `MAIN_BOOT_OE` (display GPIO 8 gates the red button onto main's QSPI_SS/BOOTSEL strap) is control only.
- There is no shared SPI bus, no shared flash and no shared RAM.
- The display's I2C1 (expander, RTC, accel, charger) is reached by main only through link messages.
- Display GPIOs 18, 19, 20 and 28 have **no assignment in the BSP's pin map**. Whether they are routed anywhere, including to main, is **[unverified]**: check the schematic. Main has every GPIO 0–29 assigned.

### The cost

Using main for game logic changes the headline from "Quake on one RP2040" to "**Quake on the FreeWili OG's two RP2040s (2 x 264 KB)**". That is still a strong claim: rp2040-doom fits in one chip, but Quake is much bigger. Because main *already* has to run a boot/watchdog shim, the split can be presented as an optional "two-chip mode". Keep the single-chip build as the primary target, with the same engine behind a `net_uart` vs `net_loop` driver switch.

---

## 3. Connectivity for streaming levels

### What radios exist

- **OG:** no Wi-Fi, no Bluetooth, no Ethernet. The only off-board data paths are:
  - USB to a PC: each RP2040 is a full-speed USB 1.1 CDC device behind the internal hub. The hub's third, high-speed interface is the FT232H to the FPGA.
  - The breakout header (UART1, SPI1, I2C0 and 2 GPIO on main, behind level shifters).
  - 2x CC1101.
  - IR.
- **CC1101 as a data link:**
  - Data rate is programmable from 0.6 up to about 500–600 kBaud, depending on modulation (TI datasheet).
  - After preamble, sync word, length and CRC, 64-byte FIFO refills and half-duplex turnaround, realistic throughput is about **50–200 kbit/s (6–25 KB/s)**.
  - It needs a peer CC1101: a second FreeWili, a CC1101 USB dongle, or similar.
  - Regulatory limits cut this further. In the EU 868 MHz sub-bands the duty cycle is mostly 1%; in the US 902–928 MHz band, FCC 15.249 limits power unless you meet the 15.247 digital-modulation/FHSS rules.
  - A 2 MB level at 20 KB/s takes about 100 s. The 50 MB game would take more than 40 minutes, and only under a duty cycle that is legal.
  - Verdict: **not a level-streaming path.** It is fine for small packets: input, or two-board multiplayer (see §5).
- **IR:** a few hundred bytes per second and line of sight. No.
- **FW2:** ESP32-C5 Wi-Fi would make direct download possible, but this is the OG.

**Conclusion: USB to a host PC is the only real path on the OG.**

### Throughput estimates

| Stage | Estimate | Basis |
| --- | --- | --- |
| USB CDC, PC → RP2040 | **300 KB/s – 1 MB/s**; plan for about 500 KB/s | USB FS bulk ceiling is about 1.2 MB/s. Published full-speed CDC measurements are 200–350 KB/s on other MCUs and depend heavily on the host OS and transfer size. No reliable RP2040 measurement was found. **Benchmark it on our board** (TinyUSB with 64-byte packets, big host writes). |
| Flash erase + program, W25Q128JV | **~4 s/MB typical** with 64 KB block erase (16 × 150 ms + 4096 pages × 0.4 ms). **~13 s/MB** with 4 KB sector erase (256 × 45 ms + 1.6 s). Worst case is about 44 s/MB. | Datasheet figures quoted in `bsp/main_cpu/fs/fs_geom.h`. Block-erase figures are from the W25Q128JV datasheet. Use 64 KB erases for bulk level slots. |
| One level (BSP 0.5–2 MB, converted) | **about 3–15 s** (2 MB: about 4 s USB + about 8 s flash, partly overlapped) | Above |
| A whole converted episode (~14.9 MB, per `docs/fit-plan.md`) | **about 1.5–3.5 minutes** | Above |

- **XIP stalls during writes.** While the display CPU erases or programs its own flash, XIP reads stop, so nothing can run from flash. Show a "Loading" screen drawn from RAM code.
  - The display CPU has no watchdog, so long writes are safe there.
  - Writes on main must kick the watchdog.
  - Alternatively, main receives the level into **its** flash while the display CPU keeps playing, then hands it over at the level change. Main's flash is behind its own USB CDC port.

### "PC companion app" sketch

```
 PC (Windows/macOS/Linux)                          FreeWili OG
 ┌─────────────────────────────┐   USB CDC   ┌─────────────────────────────┐
 │ qog-companion               │◄──────────► │ display CPU (Quake client)  │
 │  - finds board (VID 093C,   │  (display   │  - flash layout:            │
 │    PID 2055 display/2054    │   PID 2055) │    [code][common assets]    │
 │    main, freewili-finder)   │             │    [level slot A][slot B]   │
 │  - user points it at THEIR  │             │    [saves 64 KB]            │
 │    id1/pak0.pak (+pak1.pak) │             │  - on "changelevel X":      │
 │  - converts level X on the  │             │    REQ_LEVEL{name, slot}    │
 │    fly, baked for the slot  │             │    ← DATA chunks (4 KB, CRC)│
 │    address it will occupy   │             │    erase/program 64 KB      │
 │  - pushes gamepad state     │             │    verify CRC32 → play      │
 │  - receives saves, stores   │             │  - SAVE_PUT{slot, blob}     │
 │    them in a folder (option-│             │  - GAMEPAD frames (§4)      │
 │    ally a synced cloud dir) │             └─────────────────────────────┘
 └─────────────────────────────┘
```

- **Protocol.** Reuse the BSP's `[SOF][len][payload][crc16]` frame over CDC, with a 1-byte type and a slot/sequence header.
  - Use a sliding window of 2–4 outstanding 4 KB chunks so USB and flash programming overlap.
  - Send a final CRC32 over the whole slot.
  - Keep a slot table in its own flash sector (level name, converter version, CRC). The board can then skip the transfer when the requested level is already in a slot, the way the BSP's display updater skips on matching CRC.
- **Fixed addresses.** Our converter bakes pointers for a fixed flash address (fit-plan step 1), so the PC converts each level **for the slot it will land in**.
  - Two slots allow prefetching the next level while the current one plays. The prefetch must be written by main into main's flash, or written between frames in small 4 KB steps with short XIP stalls.
  - Common assets (models, sounds, gfx, `progs` data) stay resident per episode.
- **What it unlocks.** The full registered game (about 50 MB) becomes playable without reflashing, from the user's own pak files.
- **Saves.** Quake saves are small text files, a few tens of KB. The board writes one to a local save sector, so it still works offline, and uploads it to the companion. The companion writes `saves/<name>.sav`. The user can place that folder inside their own Dropbox, OneDrive or Google Drive folder for "cloud" sync, with no server needed.
- **No PC?** The board can still play whatever is already in flash (one episode or the last levels cached).
- **Legal.** id's game data (pak files) is not ours to redistribute, and the shareware licence forbids redistributing modified data. A public server hosting levels, even converted ones, would be redistribution. Any "online library" must therefore be **the user's own private copy** (their own pak on their own disk or private cloud folder), converted locally. Whether a given private-cloud arrangement is acceptable is a legal judgement **for the user**, not something this project should decide or ship as a service. The companion should only ever read paks the user points it at, which matches `docs/game-data.md`.

---

## 4. External controller

- **The OG exposes no USB host port.** Each RP2040's native USB goes upstream into the on-board hub as a device ("Integrated hub with 3 interfaces: 2 FS, 1 HS"). The RP2040's USB controller can do host mode in silicon, but here it is wired as a device to the hub, so that is not available. **[Hub topology is inferred from the vendor spec line; confirm with the schematic.]**
- **No Bluetooth** on either CPU.

Free GPIOs, from `docs/hardware/pinmap.md` and both `board.h` files:

| CPU | Unassigned GPIO | Notes |
| --- | --- | --- |
| Display | 18, 19, 20, 28 | No BSP assignment. Routing and accessibility are **[unverified]**; possibly unconnected. GPIO 21 (LCD TE) is assigned but unused by current firmware. |
| Main | none | All 30 assigned. The user breakout is UART1 (8–11), SPI1 (12–15), I2C0 (16–17) and GPIO in/out (26/27). All of it passes through the FPGA io_buffer and the PCAL6416-controlled level shifters ("11 GPIO, 1.1–5.5 V"). SPI1 is also driven during FPGA configuration at every boot (CS held low for about 170 ms). |

### Options

| Option | How | Feasibility on OG | Verdict |
| --- | --- | --- | --- |
| **PC companion relays the pad** | The PC reads XInput/SDL gamepads (Xbox, PlayStation, Switch, anything) and sends a 12–16 byte state frame over the display CPU's CDC port at 60–125 Hz | Zero hardware. About 1–2 ms latency (1 ms FS polling plus our loop). Works with any pad the PC supports, wired or Bluetooth. Requires a PC, but streaming needs one anyway. | **Recommended first** |
| ESP32 + Bluepad32 add-on | A small ESP32 board (classic BT needs the original ESP32; S3/C3 are BLE-only) runs Bluepad32 and sends pad state over UART to main's breakout **UART1 (GPIO 8/9)** at 3.3 V. Main forwards it over the link (a few bytes per frame). | Realistic. About $5–10 of parts, 4 wires (TX, RX, 3V3, GND). The header pins must be configured as UART through `fwog_io_dir_apply()`, which the BSP marks "never run on a board" **[unverified]**. Main must run a few lines of forwarding code. | **Best untethered option** |
| PIO-USB host (Pico-PIO-USB) | Bit-banged USB FS host on two GPIOs (D− = D+ + 1) | Display spares 18/19 or 19/20 are adjacent, but routing is unknown. Main has only level-shifted breakout pins: direction-controlled translators break bidirectional USB, and there is no 5 V VBUS switch. Pico-PIO-USB needs a system clock that is a multiple of 12 MHz (e.g. 120/240 MHz) **[check the current library version]**, while the BSP pins 200 MHz and derives the link baud from it. It also costs a PIO block and CPU time on the busy display CPU. | **Not viable** without a board mod |
| USB host chip (MAX3421E over SPI) | SPI host controller on main's breakout SPI1 | Electrically possible on 12–15 (the level shifters must be configured correctly). SPI1 is shared with FPGA configuration at boot, and it needs a 5 V VBUS supply and driver work (TinyUSB supports the MAX3421E). | Possible but clumsy |
| IR remote | TV/Roku remote into the display CPU's IR RX (GPIO 16; the BSP has decoders) | Works today, but NEC repeat frames arrive about every 108 ms, there are no analog sticks, and it is line of sight | Novelty only |
| CC1101 pad | A second sub-GHz device sends pad packets | Needs custom transmitter hardware | No |

The FW2 BSP already contains `bsp/pio_usb_host` with an XInput driver, on FW2 GPIO 42/43. That code is the template if we ever target FW2 or modify an OG.

---

## 5. What else on the board can we use?

| Resource | Facts | Use for Quake | Realistic? |
| --- | --- | --- | --- |
| **iCE40UP5K FPGA** | 5.3K LUTs, 128 KB SPRAM, 120 Kbit EBR. An 8 MB APS6404L QSPI SRAM hangs off the FPGA (vendor docs), and an FT232H (FT1248 mode) high-speed USB path also goes to it. Main reaches it through breakout SPI pins at 3.25–5 MHz in the BSP. | In theory: 8 MB of "swap", or a high-speed PC→FPGA pipe | **No.** The BSP deliberately freezes the bitstream (rule 4) and has no public loader, because unknown gateware can fight the level shifters. The FW1 gateware exposes only the io_buffer register. The path to main is a slow SPI on user pins, about 0.5–3 MB/s at best with custom gateware, and µs-scale per random access. The vendor states "future OG revisions drop the FPGA", so any dependency strands those boards. |
| **Main CPU flash** (16 MB) | App region plus an 8 MB FAT volume at offset 8 MB (FatFs; not power-safe, no wear levelling) | Second-episode store or level cache, or the server's copy of the level data in two-chip mode | Yes, as raw slots rather than FAT |
| **Display CPU flash** (16 MB) | 128 KB bootloader + metadata, app from 0x10021000 | The main game-data region (12–15 MB) | Already planned |
| **Second core on each RP2040** | Display core1 is ours. Main core1 needs a flash lockout before its first flash write. | Rendering/mixing split, or server tick | Display core1 yes. Main only in two-chip mode. |
| **WS2812 LEDs** (7, GPIO 7, PIO) | Driver verified on hardware | Health/armour bar, red flash on damage, muzzle flash, powerup colour (quad = blue, pentagram = red) | **Yes:** cheap and visible. Avoid LED updates during PIO-heavy audio, or schedule them. |
| **I2S speaker** (GPIO 4/5/6) | Verified on hardware. Mono. The amp part number was not checked. | Quake's 11 kHz 8-bit mono sounds suit it well | **Yes** (already in scope). Watch the BSP's documented abort/zero-sample tricks for click-free stops. |
| **LIS3DH accelerometer** (I2C1 0x19, 100 Hz) | Verified on hardware | Optional "tilt to aim/turn" with dead zone and recentre | **Yes, as an option.** Five buttons make aiming hard, so tilt helps. Costs about 1 I2C read per frame. |
| **MCP7940 RTC** | Verified on hardware | Save timestamps, play-time display | Yes, trivial |
| **BQ25896 charger** | On I2C1; one wrong write powers the board off. There is no fuel gauge (the BQ27441 is not populated). | Battery/charging icon from charger status | Maybe. Read-only, with care. |
| **CC1101 x2** | 300–348 / 387–464 / 779–928 MHz, verified on hardware | **Two-board multiplayer:** NetQuake was built for 28.8 kbit modems, so a 38–250 kbaud link can carry it. One board's main CPU could even host the server. | Later stretch goal. Needs a half-duplex MAC and attention to regulations. |
| **IR TX/RX** | Verified on hardware | Remote-control novelty | Low value |
| **PDM mic** | Verified on hardware | Nothing useful | No |
| **ST7789 panel** | 50 MHz SPI, so a full 16-bit frame (153.6 KB) takes about 24.6 ms (about 40 fps ceiling). TE pin on GPIO 21 is unused. | Use TE to avoid tearing; DMA the line buffer | Yes |

---

## Sources

Local (authoritative for the OG):
- `third_party/wiliOGbsp/README.md`, `AGENTS.md`, `docs/hardware/pinmap.md`
- `third_party/wiliOGbsp/bsp/common/link/link_uart.h`, `link_frame.h` (link rate and framing)
- `third_party/wiliOGbsp/bsp/main_cpu/fpga/ice40.h`, `fpga_bitstream.h`, `io_dir.h`, `fpga_clk.h` (FPGA policy and path)
- `third_party/wiliOGbsp/bsp/main_cpu/fs/fs_geom.h`, `fwog_fs.h` (flash timing, FAT layout, core1 hazard)
- `third_party/wiliOGbsp/bsp/main_cpu/display_update/display_update.h`, `main_cpu/platform/board.h`, `display_cpu/platform/board.h`, `display_cpu/io_expander/ioexp_link.h`, `main_cpu/radio/cc1101.h`
- `docs/fit-plan.md`, `docs/game-data.md` (this repo)

Web:
- [FREE-WILi OG product page (freewili.com)](https://freewili.com/freewili-og.html)
- [FREE-WILi 2 home page (freewili.com)](https://freewili.com/)
- [CNX Software: FREE-WILi 2 features two RP2350 MCUs, ESP32-C5, iCE40 FPGA and Raspberry Pi CM0 (2026-09-09)](https://www.cnx-software.com/2026/09/09/free-wili-2-portable-hacking-multitool-features-two-rp2350-mcus-esp32-c5-ice40-fpga-and-raspberry-pi-cm0/)
- [ElectronicsForU: FREE-WILi 2](https://www.electronicsforu.com/news/free-wili-2-packs-electronics-lab-into-handheld)
- [FREE-WILi 2 shop page](https://shop.freewili.com/products/free-wili-2) (HTTP 429 when fetched; price taken from press)
- [freewili/wilibsp — FreeWili 2 BSP (README, docs/drivers/platform.md, docs/drivers/pio-usb-hid.md, bsp/pio_usb_host/tusb_xinput)](https://github.com/freewili/wilibsp)
- [freewili GitHub organisation](https://github.com/freewili)
- [docs.freewili.com](https://docs.freewili.com/) (FW2 docs; the older FW1 "hardware low-level details" pages that described the FT232H, the APS6404L SRAM and the iCE40 connections now return 404. Those details come from search-result snippets of those pages and are **[unverified]**.)
- [Jameco: FREE-WiLi Black (Def Con Edition), $339](https://www.jameco.com/z/FW-BLK-FREE-WILi-FREE-WiLi-Black-Def-Con-Edition-Embedded-Development-Tool_2738991.html)
- [Hackster: FREE-WILi on the DEF CON 32 badge](https://www.hackster.io/news/free-wili-brings-its-flexible-development-and-debugging-platform-to-the-def-con-32-badge-bd5ab94f1b9e) (fetch blocked; snippet only)
- [Nordic DevZone: USB CDC ACM max transfer speed](https://devzone.nordicsemi.com/f/nordic-q-a/83705/usb-cdc-acm-max-transfer-speed) and [MIT link-speed tests](https://video.cba.mit.edu/jakeread/low-level-link-speed-tests/-/raw/1d44d7bcba00e505a492a450c436335b5b584eec/2023-12_usb-real-data-rates.md) (CDC throughput reference points)
- Datasheet figures from memory, not re-fetched: TI CC1101 (data rate 0.6–600 kBaud, 64-byte FIFOs), Winbond W25Q128JV (64 KB block erase 150 ms typical / 2 s max), Pico-PIO-USB README (12 MHz-multiple clock, D− = D+ + 1), Lattice iCE40UP5K (128 KB SPRAM). **[verify before citing externally]**
