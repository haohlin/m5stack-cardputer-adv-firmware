# Claude desktop buddy port for m5stack cardputer

> **Physical desk pet for Claude Code / Cowork** — runs on the **M5Stack Cardputer ADV** (ESP32-S3). Approve permission prompts with `Y`/`N` on a real keyboard, watch your pet react to what Claude is doing, get a Mario 1-UP in your ear when an approval is waiting, and flip the device face-down to make it nap.

[![PlatformIO](https://img.shields.io/badge/built_with-PlatformIO-orange.svg)](https://platformio.org/)
[![Target](https://img.shields.io/badge/target-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Hardware](https://img.shields.io/badge/hardware-M5Stack_Cardputer_ADV-lightblue.svg)](https://shop.m5stack.com/products/m5stack-cardputer-adv)
[![Fork of](https://img.shields.io/badge/fork_of-anthropics%2Fclaude--desktop--buddy-purple)](https://github.com/anthropics/claude-desktop-buddy)

A port of [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) from the M5StickC Plus to the Cardputer ADV. The BLE bridge, stats, NVS persistence, GIF character pipeline, and all 18 original ASCII species work unchanged; this fork adds a landscape UI, full-keyboard input, 8-bit sound library, a live pet picker, two new species (`doge`, `llama`), and a handful of UX fixes.

## Local workspace

This project is maintained as a Cardputer-Adv workspace on top of the official Anthropic BLE protocol and the community Cardputer port. The upstream remote is `anthropics/claude-desktop-buddy`; this fork keeps the Cardputer hardware adaptation in `src/hal.*` and `platformio.ini`.

Research notes are in [`docs/RESEARCH.md`](docs/RESEARCH.md), including the upstream PR/fork scan, M5Stack Cardputer-Adv docs, and community implementation notes.

Architecture docs are in [`docs/architecture.md`](docs/architecture.md). The
stable path is still official Hardware Buddy BLE; optional desktop integration
now lives in [`desktop-bridge/`](desktop-bridge/) and
[`claude-plugin/`](claude-plugin/).

From the collection repository root, use the Launcher workflow:

```bash
./cardputer build claude-buddy
./cardputer release claude-buddy
./cardputer stage claude-buddy
./cardputer debug claude-buddy serial
./cardputer debug claude-buddy ble

# Optional secure Wi-Fi bridge config. TLS vars must match desktop listener.
CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." \
  CARDPUTER_BRIDGE_TLS_CA=/private/path/bridge-ca.pem \
  ./apps/claude-buddy/scripts/write_bridge_config_folder.sh

# Preferred bridge config provisioning and debug evidence capture
./apps/claude-buddy/scripts/write_bridge_config_serial.sh
./apps/claude-buddy/scripts/collect_debug_bundle.sh
```

For installation, boot Launcher, open **Settings → USB MSC**, run `stage`, exit
USB MSC, then select the staged image under `tools/` and choose **Install**.
Launcher stays installed and writes the raw app image through OTA. Full-flash
tools are isolated under `scripts/recovery/` and are never the normal path.

Current Launcher package metadata is `1.4.0`. The historical
`claude-buddy-v1.3.0` tag remains immutable and is not installable through
Launcher. Create `claude-buddy-v1.4.0` only after physical Launcher-installed
proof succeeds.

For repeatable debugging, use [`docs/debugging.md`](docs/debugging.md). The
recommended flow is: verify the Launcher-installed firmware over USB serial, provision
bridge config with `scripts/write_bridge_config_serial.sh`, then test BLE and
folder push separately. `scripts/collect_debug_bundle.sh` creates a redacted
bundle under `release/debug/` with device status, sanitized bridge health, and
firmware archive state. Claude content stays out unless explicitly opted in for
a private reviewed bundle.

Before changing hardware-sensitive code, check
[`docs/reference/cardputer_adv_wifi.md`](docs/reference/cardputer_adv_wifi.md).
Refresh the ignored local reference cache with `./scripts/sync_reference_docs.sh`;
it stores official M5Stack/Espressif docs and M5Stack SDK repos under
`reference/vendor/`.

## What it does
<img width="1498" height="958" alt="default screen for claude buddy cardputer in idle mode" src="https://github.com/user-attachments/assets/b4272ba9-3735-48a4-b76d-4af2c46088d2" />
<img width="1165" height="832" alt="info screen for claude desktop buddy cardputer in idle mode" src="https://github.com/user-attachments/assets/1af36f57-5696-4fb3-aaa4-f749ca196f30" />
<img width="1469" height="976" alt="pet screen for claude desktop buddy cardputer" src="https://github.com/user-attachments/assets/2de63c37-2c55-45ba-9c60-28c0c9b29619" />

The device connects to the Claude desktop apps over BLE (developer mode required) and acts as a physical session dashboard + permission-approval affordance:

- **Permission prompts** — when Claude asks to run a command, the device plays a Mario 1-UP jingle, pulses the RGB LED red-orange, and shows the tool name. Press `Y` to approve, `N` to deny. Approving in under 5 seconds triggers a heart animation.
- **Claude Session page** — the home screen shows the last three wrapped lines; PET page 3/3 is a BLE-only Claude Session view with session counts, completed assistant-response summaries, and a bottom prompt/input field. If Claude emits `<device_summary>...</device_summary>`, the firmware shows that tiny summary; otherwise it shows a bounded excerpt from official `evt:"turn"` messages and falls back to heartbeat activity when no turn event arrives.
- **Optional secure Wi-Fi bridge ping** — after complete TLS bridge config is provisioned over USB serial or sent as a `bridge-config` folder, firmware stores Wi-Fi and bridge settings in NVS, reconnects after reboot, and sends bounded state pings over CA-validated WSS when `wifi` is enabled. This is opt-in and does not affect official BLE path.
- **Seven pet moods** — sleep, idle, busy (3+ sessions running), attention (approval pending), celebrate (session just completed), dizzy (you shook it), heart (fast approval).
- **20 ASCII species + custom GIFs** — cycle with the live picker (Menu → pet) or drag a character pack folder onto the Hardware Buddy window to stream a custom GIF character over BLE.
- **Tamagotchi mechanics** — the pet has mood, fed, energy, and level stats that drift based on your approval cadence and Claude's token usage. Flip the device face-down and it naps (screen dims, energy refills). Shake it to make it dizzy.
- **Compact clock face** — `HH:MM:SS Mon DD` appears across the top when idle on USB power, with the pet still rendering underneath.
- **Stats page** — press `Enter` to cycle to PET mode for mood hearts, fed bar, energy bar, level badge, and lifetime counters (approvals, denials, nap hours, tokens, tokens today).

## What changed from upstream

- **HAL shim** ([`src/hal.h`](src/hal.h) + [`src/hal.cpp`](src/hal.cpp)) — wraps every `M5.Axp` / `M5.Beep` / `M5.BtnA` / `M5.BtnB` / `M5.Imu` / `M5.Rtc` call site. Current `platformio.ini` exposes only the Cardputer ADV Launcher environment; the immutable v1.3.0 tag preserves the older board environments.
- **Landscape UI** — sprite is now 240×135, every modal / Claude Session / info / pet panel relayouted for the wider-than-tall canvas. Menus centered, settings compacted to 10 rows at 10 px pitch, session/info/pet pages go full-screen with the pet hidden.
- **Keyboard input** — a HalKey event queue rising-edge-detects the Cardputer matrix and emits `Approve` / `Deny` / `Back` / `Up` / `Down` / `Left` / `Right` / `Menu` / `Demo`. Enter and `Del`/`` ` `` also swallow their matching BtnA/BtnB release so modal confirms don't double-fire into a home-screen cycle.
- **Live pet picker** — Menu → **pet** opens a full-size preview with a hint bar at the bottom; step species with `,` / `/` and commit with `Enter`. On commit the pet plays a `P_HEART` one-shot and a 3-note save fanfare.
- **8-bit SFX library** — 10 tuned note sequences (nav blip, confirm arpeggio, Zelda-lite approve chord, Mario 1-UP alert, back, deny, save fanfare, menu, warn, warn2) played through an in-loop tone sequencer shared across both boards. New `halBeepSeq()` replaces the previous single-note `beep()`.
- **RGB NeoPixel attention LED** on GPIO 21 — driven by `neopixelWrite` (shipping with the Arduino-ESP32 core, no extra dep), pulses red-orange while an approval is pending.
- **Compact clock** — the upstream charging-clock face was redesigned as a 10 px top strip (size-1 glyphs) instead of a 20 px size-2 strip, leaving y=10..135 for the pet. Tilt-to-landscape variant disabled since the UI is already landscape.
- **Two new ASCII species** — `doge` and `llama`, bringing the total to 20.
- **Sleep timeout** raised from 30 s to 2 minutes.

## Hardware

M5Stack **Cardputer ADV** (ESP32-S3 StampS3 + 1.14" 240×135 ST7789 + BMI270 IMU + NS4168 I²S speaker + 56-key QWERTY matrix + WS2812B RGB LED on GPIO 21). The stock Cardputer (no IMU) should boot and run everything except shake/face-down nap.

Original M5StickC Plus source and build environment remain preserved in the immutable v1.3.0 tag.

## Build and install

Normal v1.4.0 firmware is a raw Launcher-compatible app image. From collection
root:

```bash
./cardputer build claude-buddy
./cardputer release claude-buddy
./cardputer stage claude-buddy
```

`stage` requires Launcher **Settings → USB MSC**. After staging, exit USB MSC,
open `tools/` in Launcher, select the Claude Buddy image, and choose **Install**.
Hold any key while Claude Buddy boots to return to Launcher. Factory reset from
the app remains Menu → settings → reset → factory reset → tap twice.

## Pairing

Identical to upstream. Enable developer mode in Claude (**Help → Troubleshooting → Enable Developer Mode**), then **Developer → Open Hardware Buddy…** and pick your device. The Cardputer advertises as `Claude-XXXX` (last two MAC bytes).

## Keyboard mapping

| Key | Home screen | In a modal (menu / settings / reset / picker) | In an approval prompt |
|---|---|---|---|
| `Enter` | cycle display mode; on Claude Session acts on selected area | **confirm / commit** | **approve** |
| `Tab` | switch PET / INFO tab group | — | — |
| `Backspace` / `` ` `` | — | **close modal** | **deny** |
| `;` / `.` | select Claude / Prompt on Claude Session; scroll expanded response | move highlight up / down | — |
| `,` / `/` | prev / next page (INFO & PET) | picker: prev / next species | — |
| `Fn+Enter` | emit experimental prompt command; stock Claude Desktop needs a handler | — | — |
| `Y` / `N` | — | (picker) confirm | **approve / deny** |
| `M` | open menu | — | — |
| `G` | toggle demo mode | — | — |

Power is the hardware slide switch on the right edge. Screen auto-sleeps after 2 minutes of no activity (kept on while on USB power or while an approval is pending).

## 20 ASCII species

capybara, duck, goose, blob, cat, dragon, octopus, owl, penguin, turtle, snail, ghost, axolotl, cactus, robot, rabbit, mushroom, chonk, **doge**, **llama** — the last two are new in this fork.

Each species animates across seven states (sleep / idle / busy / attention / celebrate / dizzy / heart). Pick one from Menu → pet.

## GIF pets

Drag a valid flat character pack folder onto Hardware Buddy drop target. See
[upstream README](https://github.com/anthropics/claude-desktop-buddy#gif-pets)
for format. Host prep and firmware enforce supported nonempty states, regular
referenced GIFs, signature/dimensions/decoder-open validity, bounded source
bytes/frames/pixels, and hard 1.8 MB output/transfer limit before commit. Failed
semantic validation preserves active character. GIFs render centered in upper
80 px landscape canvas.

## Layout notes

- **Home**: pet fills the upper 80 px, the 3-line transcript is block-centered at x=57 in the bottom 28 px.
- **Claude Session**: PET page 3/3. The upper panel is Claude output with the Claude orange accent; the bottom panel is the prompt/input field. `;` selects Claude, `.` selects Prompt, `Enter` expands/compacts or enters typing, and `Fn+Enter` emits an experimental prompt command over BLE. Stock Claude Desktop ignores that command until a supported desktop handler exists. The firmware stores only a small fixed assistant excerpt to keep RAM use predictable and prefers tagged `<device_summary>` text when present.
- **Clock face** (idle + USB): one-line `HH:MM:SS  Mon DD` at size 1 in a 10 px top strip.
- **Pet stats** (the `B`-cycle stats page, separate from the menu's pet picker): two-column landscape layout, mood / fed / energy / Lv badge on the left, counters on the right.
- **Info pages**: full-screen, pet hidden. ABOUT and KEYBOARD copies rewritten for Cardputer bindings; CREDITS hardware line updated to `M5 Cardputer ADV / ESP32-S3`.

## Known limitations

- `halBatteryMilliAmps()` returns 0 — M5Unified doesn't expose a current reading on this hardware. The DEVICE info page shows `current +0mA`.
- `halTempC()` is stubbed at 25 °C (same reason).
- If the BMI270 doesn't come up (stock Cardputer, or M5Unified config mismatch), shake and face-down nap silently no-op. Serial prints `IMU: begin=0 type=0` at boot in that case.
- Some info-page copy still reads long for landscape; the CLAUDE / DEVICE / BLUETOOTH pages fit but CREDITS is tight.
- Official Hardware Buddy BLE currently exposes heartbeat snapshots, assistant turn events, file transfer, and permission approve/deny. It does not expose prompt sending or session switching. The firmware can emit an experimental `{"cmd":"prompt"}` message, but stock Claude Desktop will ignore it until a supported desktop handler exists; the device keeps the prompt draft and reports that state. The compact-summary template for that future path is documented in [`docs/DEVICE_SUMMARY_PROMPT.md`](docs/DEVICE_SUMMARY_PROMPT.md).
- `AskUserQuestion` prompts are shown as Desktop questions. `Y` only allows Claude to use the question tool; it does not select an option or send an answer back to Claude. Answer those in Claude Desktop until a supported question-answer transport exists.
- Optional Wi-Fi bridge is disabled unless a complete secure `bridge-config` is provisioned over USB serial or folder push, then enabled through Menu → settings → wifi. Physical Wi-Fi requires desktop TLS certificate, private key, and public CA paths plus `CARDPUTER_BRIDGE_DEVICE_BIND_HOST=0.0.0.0`; device accepts only CA-validated `wss://.../device` and bearer header authentication. It stores Wi-Fi password, endpoint, pairing token, and public CA in NVS until replacement/factory reset. Source-only validation cannot prove encrypted-at-rest device state; provision secure boot plus flash/NVS encryption for physical extraction resistance. USB serial provisioning remains preferred deterministic config path.
- Normal build artifacts consume no compile-time Wi-Fi, bearer, or CA header.
  Runtime BLE/USB/folder provisioning is required; stale
  `src/bridge_config.local.h` makes root build/release fail.

## Credits

- Original firmware by [Felix Rieseberg](https://github.com/felixrieseberg) at Anthropic.
- Cardputer ADV port, layout redesign, HAL shim, keyboard event routing, 8-bit SFX, pet picker, and new `doge` / `llama` species by [@y88huang](https://github.com/y88huang).

## Availability

The BLE API is only available when the Claude desktop apps are in developer mode. It's intended for makers and developers and isn't an officially supported product feature.
