# Cardputer-Adv RFID2 Firmware Handover

## Release

- Release name: latest stable / first release
- Version: `0.5.0-keyboard-ui`
- Target hardware: M5Stack Cardputer-Adv with Unit RFID2 on the Grove port
- Firmware environment: `cardputer-adv-rfid2`
- Repo path: `/Users/haohanl/dev/cardputer-rfid2-fw`

## Current Firmware Summary

This firmware is a keyboard-driven, slot-based MIFARE Classic 1K lab-card
reader/writer for the M5Stack Cardputer-Adv and Unit RFID2.

It initializes RFID2 at I2C address `0x28` on Grove pins:

- SDA: `G2`
- SCL: `G1`

It uses the `MFRC522_I2C` library and authenticates MIFARE Classic sectors with
factory default Key A:

```text
FF FF FF FF FF FF
```

## What Was Implemented

- Standalone PlatformIO firmware for Cardputer-Adv.
- Build script: `./scripts/build_cardputer_adv.sh`.
- Flash script: `./scripts/flash_cardputer_adv.sh /dev/cu.usbmodem2101`.
- Direct `esptool.py` flash path at 115200 baud with watchdog reset.
- RFID2 I2C detection and status reporting.
- Card detection preview with UID, SAK, and PICC type.
- Manual on-device flow with no automatic write on card detection.
- Official `M5Cardputer` keyboard input path.
- Four RAM-only saved dump slots.
- Version number assigned to each successful saved read.
- Device UI state:
  - `READ Slot N`
  - `WRITE Slot N vX`
  - armed write/overwrite state with 8 second confirmation window
- Keyboard controls:
  - left/right: switch read/write mode
  - up/down: choose slot
  - `1`-`4`: jump to slot
  - `R` / `W`: jump to read/write mode
  - Enter: run or confirm selected action
  - Backspace: arm/confirm selected-slot clear
  - Esc/backtick: cancel armed action
- Read flow for MIFARE Classic 1K sectors authenticating with default Key A.
- Write flow for stored normal data blocks only.
- Serial command interface:
  - `status`
  - `slots`
  - `next`
  - `ui`
  - `mode read|write`
  - `slot <1-4>`
  - `scan`
  - `store [slot] [confirm]`
  - `dump [slot]`
  - `write [slot] confirm`
  - `write-block <block> <32hex>`
  - `clear [slot]`
  - `clear all confirm`
  - `reset-rfid`
  - `version`
  - `help`
- Serial write guard requiring `write [slot] confirm`.
- Same-card normal data block rewrite through `write-block`.
- Realtime status watcher: `./scripts/watch_status.sh`.
- One-shot serial command helper: `./scripts/send_command.sh`.
- USB-JTAG runtime status helper: `./scripts/jtag_pc_status.sh`.
- README with build, flash, monitor, and manual usage instructions.

## Verified Behavior

Verified on the connected Cardputer-Adv at `/dev/cu.usbmodem2101`:

- Build succeeded for environment `cardputer-adv-rfid2`.
- Flash succeeded using `./scripts/flash_cardputer_adv.sh /dev/cu.usbmodem2101`.
- Device reported:
  - firmware `cardputer-rfid2-fw`
  - version `0.5.0-keyboard-ui`
  - `rfid_ready=true`
  - `i2c_scan=0X28`
  - selected UI state `READ Slot 1`
  - four empty slots after boot
- `write 1` without confirmation was rejected.
- `help` listed the manual-slot command surface.
- Serial `mode` and `slot` commands updated the screen/UI state.
- `watch_status.sh` displayed mode, selected slot, armed state, slot versions,
  and last-card state.
- `jtag_pc_status.sh` mapped the program counter into app flash.
- Physical key presses were not manually tapped during this verification run.

## What Was Not Implemented

- UID rewrite.
- Block 0 rewrite.
- Sector trailer copying.
- Sector trailer writing.
- Key copying.
- Access-bit copying.
- Access-bit writing.
- Non-default key recovery.
- Nested/hardnested key recovery.
- Full MIFARE Classic card cloning.
- DESFire support.
- MIFARE Plus support.
- 125 kHz ID card support.
- Persistent saved slots across reset/power-cycle.
- SD card storage for dumps.
- Multi-key dictionary configuration.
- Multi-device firmware targets beyond Cardputer-Adv.

## Important Current Code Paths

- Main firmware: `src/main.cpp`
- Serial probe firmware: `src/serial_probe.cpp`
- USB-JTAG probe firmware: `src/jtag_probe.cpp`
- Minimal USB-JTAG probe: `src/jtag_min.cpp`
- PlatformIO environments: `platformio.ini`
- Build helper: `scripts/build_cardputer_adv.sh`
- Flash helper: `scripts/flash_cardputer_adv.sh`
- Serial command helper: `scripts/send_command.sh`
- Serial status watcher: `scripts/watch_status.sh`
- USB-JTAG PC mapper: `scripts/jtag_pc_status.sh`

## Build And Flash

```sh
cd /Users/haohanl/dev/cardputer-rfid2-fw
./scripts/build_cardputer_adv.sh
./scripts/flash_cardputer_adv.sh /dev/cu.usbmodem2101
```

## Manual Device Usage

1. Boot the Cardputer.
2. Wait for `RFID2 keyboard UI`.
3. Use left/right to choose read or write mode.
4. Use up/down or `1`-`4` to choose a slot.
5. Place source card on RFID2.
6. Press Enter in `READ Slot N` to read into the selected slot.
7. Select `WRITE Slot N vX`.
8. Place destination card on RFID2.
9. Press Enter once to arm write.
10. Press Enter again within 8 seconds to write.

## Serial Usage

Show status:

```sh
./scripts/send_command.sh status /dev/cu.usbmodem2101
```

Show slots:

```sh
./scripts/send_command.sh slots /dev/cu.usbmodem2101
```

Read slot 1:

```sh
./scripts/send_command.sh "store 1" /dev/cu.usbmodem2101
```

Overwrite slot 1:

```sh
./scripts/send_command.sh "store 1 confirm" /dev/cu.usbmodem2101
```

Write slot 1:

```sh
./scripts/send_command.sh "write 1 confirm" /dev/cu.usbmodem2101
```

Rewrite normal data block 4:

```sh
./scripts/send_command.sh "write-block 4 00112233445566778899AABBCCDDEEFF" /dev/cu.usbmodem2101
```

Watch realtime status:

```sh
./scripts/watch_status.sh /dev/cu.usbmodem2101 --poll 2
```

## Notes For Future Maintainers

- Saved dump slots are RAM-only.
- Block 0 and sector trailers are skipped by write operations.
- Destination UID matching source UID is rejected in slot write mode.
- `write-block` allows same-card edits only for normal data blocks.
- Keyboard support uses the official M5Cardputer library.
- The direct `esptool.py` flash flow is used because it reliably starts the app
  after flashing this Cardputer-Adv.
- `graphify update .` was requested by workspace instructions, but `graphify`
  was not available in this shell.
