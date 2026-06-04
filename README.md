# Cardputer-Adv RFID2 Firmware

Standalone firmware for using the M5Stack Unit RFID2 on the Cardputer-Adv Grove
port with owned MIFARE Classic 1K lab cards.

This firmware initializes RFID2 at I2C address `0x28` on Grove pins `SDA=G2`
and `SCL=G1`, then runs a keyboard-driven, slot-based lab-card flow with three
modes — **READ**, **WRITE**, and **CLONE**. It authenticates sectors against a
configurable **multi-key dictionary** (seeded with public defaults), captures
the full card (all 16 sectors / 64 blocks when the keys are known), and persists
slots to **microSD** with full **RAM-only fallback** when no card is inserted.

- **WRITE** copies the readable data blocks to a destination card (no UID, no
  trailers) — safe.
- **CLONE** copies the data blocks **and** rewrites UID / block 0 via the gen1a
  backdoor (`MIFARE_SetUid` fallback for gen2) — this needs a **magic /
  UID-changeable card**. Sector-trailer (keys + access-bit) copying is available
  but **off by default** (`trailers on`) because a bad access-bit write can
  permanently brick a sector.

Unknown (non-default) keys cannot be recovered (no Crypto1 cracking on this
hardware) — see the cloning notes below and `CLAUDE.md`.

## Hardware Baseline

Official M5Stack references line up with this repo:

- Cardputer-Adv PlatformIO target: `esp32-s3-devkitc-1` with
  `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`.
- Unit RFID2: WS1850S, 13.56MHz, ISO/IEC 14443 Type A/B, I2C at `0x28`.
- Unit RFID/RFID2 Arduino tutorial: `MFRC522_I2C(0x28, -1)` and MIFARE Classic
  read/write examples.
- Cardputer keyboard API: `M5Cardputer.begin(cfg, true)`,
  `M5Cardputer.update()`, and `M5Cardputer.Keyboard.keysState()`.

Relevant docs:

- https://docs.m5stack.com/en/core/Cardputer-Adv
- https://docs.m5stack.com/en/unit/rfid2
- https://docs.m5stack.com/en/arduino/projects/unit/unit_rfid
- https://docs.m5stack.com/en/arduino/m5cardputer/keyboard

Architecture, gotchas, and out-of-scope notes live in [CLAUDE.md](CLAUDE.md).

## Build

```sh
./scripts/build_cardputer_adv.sh
```

## Flash

```sh
./scripts/flash_cardputer_adv.sh /dev/cu.usbmodem2101
```

If the port changes, omit it and the script will auto-detect when only one
matching USB serial port exists.

The flash script intentionally builds first, then writes the four ESP32-S3 image
parts through PlatformIO's bundled `esptool.py` at 115200 baud and exits with a
watchdog reset. On this Cardputer-Adv, PlatformIO's default upload/reset path can
leave the device in ROM download mode or fail after a baud-rate switch.

## On-Device Keyboard Flow

The firmware is screen-first and does not auto-read or auto-write just because a
card is detected. A write needs explicit mode/slot selection and a second Enter
confirmation.

1. Flash the firmware.
2. Wait for the HUD (status bar + READ/WRITE/CLONE tabs + slot strip).
3. Cycle mode with `<`/`>` (READ → WRITE → CLONE), or jump with `R`/`W`/`C`.
4. Choose slot 1-4 with `;`/`.` or the `1`-`4` keys.
5. **READ**: place the source card, press Enter. The result shows
   `Blocks/Sec` (e.g. `64b 16s/16` = full read). If the slot already has data,
   Enter arms overwrite — Enter again to overwrite, backtick to keep.
6. **WRITE**: select the slot, place the destination card, Enter to arm, Enter
   again within 8 s to write the data blocks.
7. **CLONE**: select the slot, place the destination card, Enter to arm, Enter
   again to clone data + UID/block 0 (needs a magic card for the UID).

Slots persist to microSD when a card is inserted; otherwise they are RAM-only
and clear on reset/power-cycle.

Keyboard shortcuts:

- `<` / `>`: cycle mode (READ / WRITE / CLONE).
- `;` / `.`: choose slot. `1`-`4`: jump to slot.
- `R` / `W` / `C`: jump to read / write / clone mode.
- Enter: run selected read, or arm then confirm write/clone.
- Backspace/Del: arm/confirm clearing the selected slot.
- Backtick: cancel an armed action.

Expected limitations:

- Only MIFARE Classic 1K is handled.
- Only sectors whose keys are in the dictionary are read/written (default `FF` +
  any keys you add). Unknown keys cannot be recovered.
- UID / block 0 is only rewritten on **magic / UID-changeable** cards.
- Sector trailers (keys + access bits) are written only with `trailers on`.
- Same-card data-block rewrites are also available via the `write-block` serial
  command.

## Cloning & access control — what actually transfers

A card just holds **data**, gated by **keys**, with a **UID**. Whether a clone is
*accepted by a reader* depends on what that reader checks. Three common models:

**1. Data-based reader (auth with keys → read data → decide).**
The "access" lives in the **data blocks**, which the WRITE/CLONE data copy
already transfers. The reader authenticates each sector with a key, then reads —
so the clone works only if the destination's **keys match** what the reader uses.
A blank destination already has the default key `FF…FF` on every sector, so if
the source is also all-default, the reader authenticates with `FF`, reads the
copied data, and access works **without writing trailers**. Trailers are needed
only if the source uses **non-default keys** (then the blank dest's `FF` wouldn't
match and you must write the source's keys into the trailers), or if specific
access bits matter.

**2. UID-based reader (just checks the serial number).**
The reader reads **block 0 / UID** (no key needed) and looks it up; data and keys
are irrelevant. This requires cloning the UID → a **magic card**. A normal
destination can never pass.

**3. Cryptographic / UID-diversified reader.**
Keys are derived from the UID, or the card does challenge–response, or data is
signed. These often **cannot be cloned at all**, even with full data + keys +
UID, because the security isn't in copyable bytes.

So, directly:

- A **byte-for-byte full copy** = data + trailers + UID. But **functional access**
  usually needs only the subset your particular reader checks.
- You only need to **write trailers** if the source uses non-default keys (so the
  reader can authenticate the clone) or relies on specific access bits.
- You only need the **UID** (→ a magic card) if the reader is UID-based or
  UID-diversified (models 2/3).
- Not enabling trailers does **not** automatically mean "no access": with a
  data-based reader and default keys (model 1), the data-only clone already
  carries the access.

Practical order: **test the data-only clone in the real reader first** (cheapest).
If it's rejected, the limiter is almost always the UID → use a magic card.

## Monitor

```sh
./scripts/monitor_cardputer_adv.sh /dev/cu.usbmodem2101
```

For a parsed realtime status view:

```sh
./scripts/watch_status.sh /dev/cu.usbmodem2101
```

The helper opens the port with DTR/RTS settings that avoid forcing the ESP32-S3
back into ROM download mode. It prints periodic heartbeat/status lines, card
detections, and store/write summaries.

The firmware also accepts these USB serial commands at 115200 baud:

- `status`
- `slots`
- `next`
- `ui`
- `mode read|write|clone`
- `slot <1-4>`
- `scan`
- `store [slot] [confirm]`
- `dump [slot]`
- `write [slot] confirm`
- `clone [slot] confirm`
- `write-block <block> <32hex>`
- `keys` / `key add <12hex>` / `key clear` / `key reset`
- `trailers on|off`
- `sd`
- `clear [slot]`
- `clear all confirm`
- `reset-rfid`
- `version`
- `help`

One-shot command helper:

```sh
./scripts/send_command.sh status /dev/cu.usbmodem2101
```

Serial write also requires explicit confirmation:

```sh
./scripts/send_command.sh "write 1 confirm" /dev/cu.usbmodem2101
```

To rewrite a normal data block on the card currently held on RFID2:

```sh
./scripts/send_command.sh "write-block 4 00112233445566778899AABBCCDDEEFF" /dev/cu.usbmodem2101
```

`write-block` accepts separators in the hex string, but it still requires
exactly 16 bytes. It refuses block 0 and sector trailers.

## JTAG Runtime Status

If USB CDC output is unavailable, use the ESP32-S3 built-in USB-JTAG path for a
live "is the app executing?" check:

```sh
./scripts/jtag_pc_status.sh
```

This halts CPU0 briefly, prints the current program counter, maps it through the
firmware ELF with `addr2line`, resumes the CPU, then exits. A `pc` in the
`0x420...` range usually means the app is executing from flash-mapped code. A
`0x400...`/`0x403...` PC can mean ROM, bootloader, or IRAM code and should be
read with the symbol line.

Install OpenOCD if needed:

```
./scripts/pio_local.sh pkg install --global --tool platformio/tool-openocd-esp32
```

## Expected Screen Output

The HUD shows a status bar (`RFID2` link, `SD`/`RAM`, key count `Kn`, `TRL` when
trailers are on), READ/WRITE/CLONE mode tabs, a slot strip (filled = has data,
boxed = selected), a content panel, and a contextual action bar. Result screens:

- `Read saved` — source data saved; the panel shows `Nb Ns/16` (blocks/sectors,
  e.g. `64b 16s/16` = full read).
- `Read failed` — `Auth failed N/16`; the card uses keys not in the dictionary.
- `Write complete` — `Written: N, Failures: M` (data blocks only).
- `Clone complete` / `Clone partial` — `UID copied`/`UID kept`, blocks + failures.
  On a normal card the UID can't change, so expect one failure for block 0.
- `RFID2 not found` — check the Grove cable and that the unit is at `0x28`.
