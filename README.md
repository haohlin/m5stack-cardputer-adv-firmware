# Cardputer-Adv RFID2 Firmware

Standalone firmware for using the M5Stack Unit RFID2 on the Cardputer-Adv Grove
port with owned MIFARE Classic 1K lab cards.

This firmware initializes RFID2 at I2C address `0x28` on Grove pins `SDA=G2`
and `SCL=G1`, then runs a manual, slot-based lab-card flow. It reads and writes
only readable MIFARE Classic data blocks using the factory default key. It does
not rewrite UID/block 0 and it does not write sector trailers or access bits.

## Hardware Baseline

Official M5Stack references line up with this repo:

- Cardputer-Adv PlatformIO target: `esp32-s3-devkitc-1` with
  `ARDUINO_USB_CDC_ON_BOOT=1` and `ARDUINO_USB_MODE=1`.
- Unit RFID2: WS1850S, 13.56MHz, ISO/IEC 14443 Type A/B, I2C at `0x28`.
- Unit RFID/RFID2 Arduino tutorial: `MFRC522_I2C(0x28, -1)` and MIFARE Classic
  read/write examples.

Relevant docs:

- https://docs.m5stack.com/en/core/Cardputer-Adv
- https://docs.m5stack.com/en/unit/rfid2
- https://docs.m5stack.com/en/arduino/projects/unit/unit_rfid

Release handover notes are in [HANDOVER.md](HANDOVER.md).

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

## On-Device Manual Flow

The firmware is screen-first and does not auto-read or auto-write just because a
card is detected. A write needs explicit mode/slot selection and a second hold
confirmation.

1. Flash the firmware.
2. Wait for `RFID2 manual mode`.
3. Short-click `BtnA` to cycle selections:
   `READ Slot 1`, `WRITE Slot 1`, `READ Slot 2`, `WRITE Slot 2`, up to slot 4.
4. For read mode, place the source card and long-press `BtnA`.
5. If the selected read slot already has saved data, long-press once to arm
   overwrite, then long-press again within 8 seconds to replace that slot.
6. For write mode, select the saved slot/version, place the destination card,
   long-press once to arm, then long-press again within 8 seconds to write.

Slots are RAM-only. Resetting or power-cycling the Cardputer clears the saved
versions.

Expected limitations:

- Only MIFARE Classic 1K is handled.
- Only sectors that authenticate with `FF FF FF FF FF FF` are read/written.
- Block 0, UID bytes, sector trailers, keys, and access bits are not written.
- A destination card with the same UID as the stored source is refused.
- Same-card project data rewrites are supported through the explicit
  `write-block` serial command for normal data blocks.

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

- `RFID2 manual mode`: the unit was detected and is waiting for explicit
  selection.
- `READ Slot N`: long-press `BtnA` with a source card present to save data.
- `WRITE Slot N vX`: long-press once to arm, then long-press again to write
  saved version `X`.
- `Read saved`: source lab card data blocks were saved to the selected slot.
- `Write complete`: copyable readable blocks were written to the destination.
- `RFID2 not found`: check the Grove cable and verify the RFID2 unit is on
  address `0x28`.
