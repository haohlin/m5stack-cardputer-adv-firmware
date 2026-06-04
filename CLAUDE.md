# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Standalone PlatformIO/Arduino firmware for the **M5Stack Cardputer-Adv** (ESP32-S3)
driving a **Unit RFID2** (WS1850S / MFRC522-class, I2C `0x28` on the Grove port,
SDA=G2/SCL=G1). It is a keyboard-driven, slot-based MIFARE Classic 1K
reader / writer / cloner for the owner's own lab cards.

## Build / flash / monitor

```sh
./scripts/build_cardputer_adv.sh                      # build env cardputer-adv-rfid2
./scripts/flash_cardputer_adv.sh /dev/cu.usbmodem2101 # rebuilds, then esptool flash @115200
./scripts/monitor_cardputer_adv.sh /dev/cu.usbmodem2101
./scripts/watch_status.sh /dev/cu.usbmodem2101 --poll 2   # parsed realtime status
./scripts/send_command.sh "<cmd>" /dev/cu.usbmodem2101    # one-shot serial command
./scripts/pio_local.sh run -e cardputer-adv-rfid2         # raw PlatformIO build
```

- Flashing uses a direct `esptool.py` path (4 image parts @115200, watchdog reset)
  because PlatformIO's default upload/reset can leave this board in ROM download
  mode. Don't "simplify" it back to `pio run -t upload`.
- There is no test suite; verification is **hardware-in-the-loop** over USB serial.
  Most read/write/clone paths require a physical card and cannot be verified
  without the device — flash, then drive `send_command.sh` / the on-device keyboard.
- Serial command surface (115200): `status slots next ui mode read|write|clone
  slot <1-4> scan store [slot] [confirm] dump [slot] write [slot] confirm
  clone [slot] confirm write-block <block> <32hex> keys key add|clear|reset
  trailers on|off sd clear [slot]|all confirm reset-rfid version help`.

## Architecture (the parts that span files / aren't obvious)

- **`src/main.cpp` is the entire firmware** — one translation unit, anonymous
  namespace. `platformio.ini` `build_src_filter` selects exactly one `.cpp` per
  env; `serial_probe.cpp`, `jtag_probe.cpp`, `jtag_min.cpp` are diagnostic-only
  builds (probe/jtag envs), never compiled into the RFID2 firmware.
- **Card selection uses WUPA, not REQA.** The 50 ms live preview poll
  (`pollCardPreview`) ends with `PICC_HaltA()`, so a card sits in HALT between
  polls. `wakeAndSelectCard()` uses `PICC_WakeupA` (+ retry) which wakes HALT and
  IDLE cards; `PICC_IsNewCardPresent()` (REQA) does **not** wake HALT and will
  intermittently fail. All explicit read/write/clone selection goes through
  `wakeAndSelectCard()`.
- **microSD must use the `FSPI` host.** The Cardputer-Adv **display owns
  `SPI3_HOST` (Arduino `HSPI`)** (M5GFX autodetect). The SD `SPIClass` is `FSPI`
  on SCK=40/MISO=39/MOSI=14/CS=12. Using `HSPI` for SD collides with the panel
  bus and makes `SD.begin()` fail. SD is purely additive: every SD access is
  guarded by `sdReady`, and all slots/keys work fully in RAM when no card is in.
  Verified on hardware: with a FAT32 card inserted + reset, `sd_ready=true` and
  slots/keys/config (`/rfid/slot*.dump`, `keys.txt`, `config.txt`) survive a
  power-cycle.
- **GPIO5 must be driven HIGH before SD init when the LoRa Cap is attached.**
  GPIO5 is the LoRa Cap's SPI CS and both it and the SD share the FSPI bus. If
  GPIO5 floats LOW at boot, the LoRa chip asserts on FSPI during `SD.begin()` →
  bus contention → `sd_ready=false`. Fix: `pinMode(5,OUTPUT); digitalWrite(5,HIGH)`
  in `setup()` before `initSdStorage()`. Same fix is in the M5Stack Launcher
  (boards/m5stack-cardputer/interface.cpp:84).
- **Slot model.** 4 `StoredDump` slots (RAM), each a full MIFARE 1K image with
  per-sector working Key A + readable mask. Persisted to `/rfid/slot*.dump` text
  files and reloaded on boot when SD is present; key dictionary persists to
  `/rfid/keys.txt`. On load, counts are recomputed from parsed data (don't trust
  the stored `counts` line).
- **Multi-key dictionary auth.** `authenticateSectorWithDictionary()` tries each
  dict key (FF first) as Key A, re-selecting after a failed auth. Seeded with
  public defaults in `seedDefaultKeyDictionary()`. Unknown-key sectors are simply
  reported failed (no Crypto1 cracking — see "Future / out of scope" below).
- **Navigation** is two-row: **Up/Down** (`;`/`.`) pick the focused row (mode tabs
  vs slot strip), **Left/Right** (`,`/`/`) move within it; a left-edge caret marks
  the focused row (selected items keep their accent colour either way). `R`/`W`/`C`
  jump mode, `1-4` jump slot, Enter run/arm, Del clear, backtick cancel. **M** opens
  the options overlay (brightness + 5-level sound, default off; Esc/Back/M closes,
  saved to `config.txt`). Boot shows an animated splash that waits for any key.
- **Three modes / actions** (UI tabs READ/WRITE/CLONE):
  - WRITE = safe: data blocks only (no block 0, no trailers), dest auth via dict.
  - CLONE = full: data + block 0/UID via library `MIFARE_OpenUidBackdoor` (gen1a,
    exact 16-byte) with `MIFARE_SetUid` fallback (gen2). **Sector-trailer writing
    is gated behind `cloneWriteTrailers` (off by default) — a bad access-bit write
    can permanently brick a sector.** Magic card required for UID/block-0 writes.
- **UI is a 240×135 TFT HUD** (`drawHome`) with a per-mode accent (green/amber/red).
  `drawLines` paints full-screen result/notice screens and sets
  `resultScreenActive` so the preview poll won't repaint over a result.

## Conventions / gotchas

- After editing code, the workspace asks to run `graphify update .` (AST-only) —
  it is often unavailable in-shell; note it rather than failing.
- Destructive RF ops (write/clone) are intentionally double-confirmed (arm →
  confirm within 8 s on device; `confirm` keyword over serial). Keep those guards.
- Bump `kFwVersion` in `src/main.cpp` for any behavioral change; it's reported by
  `version`/`status` and used to confirm the running build.

## Future / out of scope

Deliberately NOT implemented, with the technical reason each is infeasible on this
hardware/library stack (don't accept tickets to "just add" these without it):

- **Nested / hardnested key recovery (Crypto1 cracking).** Recovering unknown keys
  needs raw encrypted nonce capture and precise command timing from the PCD. The
  I2C MFRC522 lib exposes no raw nonce stream / low-level transceive timing, so
  cracking can't run on-device. Only dictionary auth is feasible; unknown-key
  sectors are reported failed.
- **Key copying (reading Key A/B back).** MIFARE Classic trailers never return the
  stored keys on a read — key bytes always read as zeros (only access bits are
  readable). A dump can't recover original secret keys; clones reuse the per-sector
  working Key A discovered during the read plus the dictionary.
- **DESFire / MIFARE Plus.** Not Classic/Crypto1 cards — they need a full ISO
  14443-4 (T=CL) APDU transport plus DES/3DES/AES secure messaging (and SL1/2/3 for
  Plus): an entirely separate command + crypto stack. Out of scope.
- **125 kHz cards.** The RFID2 (WS1850S) is a 13.56 MHz reader; 125 kHz needs
  different hardware.

## Repo rules

- Prefer fetching/building on mature, official, or well-maintained open-source
  solutions over reinventing functionality. For this project that means M5Stack's
  official libraries (M5Unified, M5Cardputer, M5GFX), library-provided example
  sketches, and established community projects — use those as the starting point
  instead of writing equivalent code from scratch.
- Pin the official M5 libraries as the project SDK: M5Unified, M5Cardputer (board
  support), and the M5Stack-recommended RFID2 driver (kkloesener/MFRC522_I2C) are
  pinned to exact versions/commits in `platformio.ini` for reproducible builds. Do
  not swap these for alternative forks without a clear, documented reason.
