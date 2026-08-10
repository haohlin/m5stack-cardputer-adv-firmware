# Research Notes

Date: 2026-05-21

This workspace intentionally starts from existing supported work:

- Official Anthropic protocol and reference firmware: https://github.com/anthropics/claude-desktop-buddy
- Cardputer-Adv community port used as base: https://github.com/y88huang/claude-desktop-buddy-cardputer
- Official M5Stack Cardputer-Adv hardware and software docs: https://docs.m5stack.com/en/core/Cardputer-Adv
- Official M5Cardputer Arduino library: https://github.com/m5stack/M5Cardputer

## Anthropic Project Findings

The official project is `anthropics/claude-desktop-buddy`. It exposes the Claude desktop Hardware Buddy BLE protocol using Nordic UART Service, JSON status messages, permission responses, and a BLE folder push transport for GIF character packs. The upstream README explicitly says builders can use `REFERENCE.md` without taking the firmware code, which makes the protocol the stable part to preserve.

The official GitHub issue tracker is disabled. GitHub reports open issues through the PR count, so issue review means PR review for this repo.

Relevant upstream PRs reviewed:

- PR #3 migrates from `M5StickCPlus` to `M5Unified`, adds M5StickS3 support, and documents the API mapping needed for non-original boards.
- PR #5 fixes first-flash GIF transfer by changing `LittleFS.begin(false)` to `LittleFS.begin(true)`. This workspace carries that fix.
- PR #12 adds a Claude Code CLI bridge, but it is mirror-only and does not round-trip permission decisions. The desktop Hardware Buddy path remains the official path for approve/deny.
- PR #14 and #21 are M5StickC Plus2 ports. They confirm the same M5Unified/LovyanGFX direction, plus hardware-specific battery and power-button differences.
- PR #19 adds an ESP Web Tools installer. Useful later for browser flashing, but not required for the first local build.

Fork scan:

- GitHub web showed 296 forks; GitHub API returned 285 visible forks during this scan.
- The full visible fork inventory is saved in `docs/research/claude-desktop-buddy-forks-2026-05-21.tsv`.
- Notable forks:
  - `y88huang/claude-desktop-buddy-cardputer`: Cardputer-Adv port with landscape UI, keyboard mappings, SFX, NeoPixel attention LED, and release binary workflow.
  - `imliubo/claude-desktop-buddy`: active M5Unified migration branch behind upstream PR #3.
  - `openalchemy/claude-desktop-buddy-s3`: M5StickC Plus S3 port with S3 serial and M5Unified notes.
  - `jdperich/claude-desktop-buddy-cyd`: CYD port using NimBLE and a HAL, useful as a larger-screen/touch reference but not our target.
  - `Srdpersonal/buddy-S3-St7789`: generic ESP32-S3/ST7789 direction, useful if we later split from M5 libraries.

## M5Stack Cardputer-Adv Findings

Official Cardputer-Adv specs that matter to firmware:

- SoC: ESP32-S3FN8, dual-core LX7 up to 240 MHz, 8 MB flash.
- Display: ST7789V2, 1.14 inch, 240 x 135.
- Keyboard: 56 keys, Cardputer-Adv uses a TCA8418 keyboard controller rather than the older Cardputer matrix path.
- Audio: ES8311 codec, MEMS mic, NS4150B amplifier, 1 W speaker, 3.5 mm jack disables speaker amp when inserted.
- IMU: BMI270.
- Battery: 1750 mAh, battery ADC on GPIO10.
- microSD: GPIO12 CS, GPIO14 MOSI, GPIO40 CLK, GPIO39 MISO.
- I2C: GPIO8 SDA, GPIO9 SCL, shared by audio, IMU, and keyboard; keyboard INT is GPIO11.

Official PlatformIO guidance uses Arduino framework, `espressif32@6.7.0`, `esp32-s3-devkitc-1`, USB CDC build flags, and `M5Cardputer=https://github.com/m5stack/M5Cardputer`. This workspace now follows that board/platform target, adds `M5GFX_BOARD=board_M5CardputerADV` to remove avoidable board auto-detection ambiguity, and uses `M5Cardputer @ ^1.1.1`.

Official flashing/download-mode notes:

- Set the Cardputer-Adv side power switch to OFF.
- Hold `G0`, apply USB power, then release it to enter download mode.
- M5Stack recommends charging with the power switch ON.

## Community Guidance

Community writeups point to the same practical path:

- Use PlatformIO + Arduino + M5Cardputer/M5Unified for custom firmware.
- Prefer M5Burner or a merged `write_flash 0x0` binary for distributing firmware.
- Keep Cardputer-Adv keyboard handling on the TCA8418 path. DeepWiki's Cardputer-Adv notes call out I2C address `0x34`, active-low INT on GPIO11, and the need to drain the event FIFO.
- Avoid older Cardputer GPIO-matrix keyboard code on ADV. Community reports of firmware stuck on start screens or only partially seeing keys match the hardware difference between Cardputer/Cardputer v1.1 and Cardputer-Adv.
- M5Launcher is popular for swapping firmware from SD card, but there are compatibility reports around Cardputer-Adv keyboard behavior in launched apps. For the first project, producing a normal merged firmware image is the lower-risk path.

## Chosen Direction

The project uses:

- Anthropic's official Hardware Buddy BLE protocol unchanged.
- The Cardputer-Adv community fork as the starting hardware port.
- M5Stack's official `M5Cardputer` and `M5Unified` libraries instead of custom display, keyboard, IMU, or audio drivers.
- PlatformIO as the build system, because it is used by the official Anthropic repo, M5Stack docs, and most community firmware examples.
- A generated merged binary for M5Burner/esptool, using the existing `scripts/merge_bin.py` hook.
- A narrow Cardputer-Adv keyboard reader shim that still uses M5Stack's bundled `Adafruit_TCA8418` driver, but explicitly binds it to GPIO8/GPIO9 and polls the FIFO so missed GPIO11 interrupt edges do not leave the UI appearing frozen.
- A device-derived static six-digit BLE PIN in secure mode. The pairing UI is only shown during an active pairing request.
- Non-blocking Serial protocol writes and bounded BLE receive draining, so Claude Desktop traffic cannot starve the Cardputer keyboard scan loop after pairing.

Deferred:

- Direct Anthropic API calls from the device. The official Hardware Buddy path is local BLE and needs no API key.
- A custom CLI bridge. Upstream PR #12 exists, but desktop Hardware Buddy is currently the supported approve/deny route.
- M5Launcher-specific app packaging. It can be added after hardware validation with the merged firmware path.
