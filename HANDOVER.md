# Cardputer-Adv RFID2 Firmware Handover

## Release

- Release name: latest stable / first release
- Version: `0.4.0-manual-slots`
- Target hardware: M5Stack Cardputer-Adv with Unit RFID2 on the Grove port
- Firmware environment: `cardputer-adv-rfid2`
- Repo path: `/Users/haohanl/dev/cardputer-rfid2-fw`

## Current Firmware Summary

This firmware is a manual, slot-based MIFARE Classic 1K lab-card reader/writer
for the M5Stack Cardputer-Adv and Unit RFID2.

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
- Four RAM-only saved dump slots.
- Version number assigned to each successful saved read.
- Device UI state:
  - `READ Slot N`
  - `WRITE Slot N vX`
  - armed write/overwrite state with 8 second confirmation window
- One-button device controls:
  - short press `BtnA`: cycle mode/slot
  - long press `BtnA` in read mode: read selected slot
  - long press `BtnA` in write mode: arm write
  - second long press within 8 seconds: execute write
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
  - version `0.4.0-manual-slots`
  - `rfid_ready=true`
  - `i2c_scan=0X28`
  - selected UI state `READ Slot 1`
  - four empty slots after boot
- `write 1` without confirmation was rejected.
- `help` listed the manual-slot command surface.
- `watch_status.sh` displayed mode, selected slot, armed state, slot versions,
  and last-card state.
- `jtag_pc_status.sh` mapped the program counter into app flash.

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
- Cardputer keyboard input beyond `BtnA`.
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
2. Wait for `RFID2 manual mode`.
3. Short press `BtnA` to select `READ Slot N`.
4. Place source card on RFID2.
5. Long press `BtnA` to read into the selected slot.
6. Short press `BtnA` to select `WRITE Slot N vX`.
7. Place destination card on RFID2.
8. Long press `BtnA` once to arm write.
9. Long press `BtnA` again within 8 seconds to write.

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
- M5Unified exposes `BtnA` as the usable generic button for this target in the
  current dependency set.
- The direct `esptool.py` flash flow is used because it reliably starts the app
  after flashing this Cardputer-Adv.
- `graphify update .` was requested by workspace instructions, but `graphify`
  was not available in this shell.
