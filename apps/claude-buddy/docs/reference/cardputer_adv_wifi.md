# Cardputer ADV Wi-Fi Reference

Research date: 2026-05-23

This is the local source of truth before changing Cardputer ADV Wi-Fi behavior.
Refresh the ignored local reference cache with:

```bash
./scripts/sync_reference_docs.sh
```

The cache is written to `reference/vendor/` and intentionally ignored by git.
It stores local copies of official docs, M5Stack driver repos, and the relevant
Arduino-ESP32 Wi-Fi source files from PlatformIO.

## Official Hardware Facts

- Cardputer ADV uses the Stamp-S3A core based on ESP32-S3FN8.
- Display is ST7789V2, 240 x 135.
- Flash is 8 MB. The official product page does not list PSRAM, so firmware
  should assume no PSRAM.
- Battery is 1750 mAh.
- Official operating-current table lists:
  - normal operating current: DC 4.2 V at 120.2 mA
  - Wi-Fi operating current: DC 4.2 V at 132.3 mA
  - BLE operating current: DC 4.2 V at 154.6 mA
- Charging requires the physical power switch to be `ON`.
- Official docs also define a bootloader recovery procedure. It is recovery-only
  and outside this collection's normal Launcher installation workflow.
- Relevant ADV pins:
  - battery ADC: G10
  - keyboard TCA8418: SDA G8, SCL G9, INT G11
  - LCD backlight/display power share: G38
  - microSD: CS G12, MOSI G14, CLK G40, MISO G39

Sources:

- https://docs.m5stack.com/en/core/Cardputer-Adv
- https://docs.m5stack.com/en/arduino/m5cardputer/program
- https://docs.m5stack.com/en/guide/restore_factory/cardputer_adv

## Official SDK Baseline

The M5Stack PlatformIO example uses:

```ini
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
build_flags =
    -DESP32S3
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Cardputer=https://github.com/m5stack/M5Cardputer
```

Current local PlatformIO package versions observed on 2026-05-23:

- M5Cardputer 1.1.1
- M5Unified 0.2.15
- M5GFX 0.2.21
- WebSockets 2.7.3
- Arduino-ESP32 framework package `3.20017.241212+sha.dcc1105b`

## ESP32-S3 Wi-Fi Constraints

- ESP32-S3 Wi-Fi is 2.4 GHz only: IEEE 802.11b/g/n. Do not expect 5 GHz SSIDs
  to work.
- Wi-Fi and Bluetooth LE share the RF path. ESP-IDF has coexistence logic, but
  connection and scan states give Wi-Fi longer slices and can affect BLE
  responsiveness.
- ESP-IDF says most coexistence cases switch status automatically. Custom
  connectionless power-save parameters should stay at defaults unless tested,
  because they can hurt Bluetooth performance.
- `esp_wifi_connect()` attempts connection once at the IDF layer. Application
  code should own reconnect policy when the AP is missing or reconnect is
  desired.
- Arduino `WiFi.disconnect(wifioff, eraseap)` can turn off Wi-Fi and optionally
  erase AP config from NVS. In this firmware, credentials are stored in our own
  `Preferences` keys, so do not erase Arduino AP config as a side effect unless
  intentionally resetting all Wi-Fi state.
- Arduino-ESP32 exposes `WiFi.setSleep(...)` and `WiFi.setTxPower(...)`.
  In the installed framework, `setTxPower` requires STA or AP to be started.

Sources:

- https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_wifi.html
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html
- https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
- Local source: `~/.platformio/packages/framework-arduinoespressif32/libraries/WiFi/src`

## iPhone Personal Hotspot Notes

iPhone hotspot compatibility is a separate problem from firmware stability.
An incompatible hotspot should produce a failed connection or disconnect reason,
not a board reboot. If Wi-Fi startup resets the Cardputer, treat it as a
firmware/power/coexistence bug first and keep Wi-Fi disabled by default.

For ESP32-S3 testing with iPhone Personal Hotspot:

- Enable `Allow Others to Join`.
- Keep the iPhone on the Personal Hotspot settings screen until the Cardputer
  connects.
- On iPhone 12 or later, enable `Maximize Compatibility`.
- Use an ASCII hotspot password of at least eight characters.
- Prefer a simple iPhone device name / SSID with ASCII letters and numbers while
  testing. Community reports show punctuation such as apostrophes in the
  default "`Name's iPhone`" SSID can cause ESP32 connection trouble.

Apple documents that Personal Hotspot defaults to WPA2/WPA3 Personal. With
`Maximize Compatibility` enabled, it:

- turns off 5 GHz and 6 GHz
- restricts the hotspot to 2.4 GHz
- enforces WPA2 Personal

Those settings match ESP32-S3 better than the default iPhone hotspot mode.

Sources:

- https://support.apple.com/en-us/111785
- https://support.apple.com/en-ph/guide/security/secfd166f620/web

## Implementation Rules For This Firmware

1. Official BLE stays primary. Wi-Fi must not block keyboard scanning, BLE
   parsing, or permission approve/deny.
2. Wi-Fi bridge is opt-in. Stable behavior with Wi-Fi disabled must remain the
   default fallback.
3. Do not auto-start Wi-Fi immediately at boot. Delay startup until the UI and
   BLE are initialized.
4. Never persist a state that can permanently boot-loop the device. If Wi-Fi
   crashes during startup, disable Wi-Fi automatically and keep the credentials.
5. Use STA-only mode for the bridge. Avoid AP/APSTA unless a future setup flow
   explicitly needs captive-portal provisioning.
6. Avoid Wi-Fi scanning in the main loop. If scan UI is added later, make it
   user-triggered, bounded, and cancellable.
7. Keep reconnect policy explicit and slow. A 10 second or longer retry cadence
   is reasonable for the bridge.
8. Use bounded WebSocket messages and fixed-size buffers. The firmware has no
   PSRAM and is already flash-heavy.
9. Physical bridge transport is WSS-only: provision `wss://host:port/device`,
   public PEM CA, and high-entropy pairing token as a complete replacement.
   Firmware sends bearer token in the upgrade Authorization header and uses
   CA-validated TLS; do not add insecure TLS or token query compatibility.
10. Keep hook/health HTTP on owner-protected Unix-domain socket, never TCP, and
    separate from LAN device listener. Health remains content-free; hook keeps
    independent credential plus bounded request/pending work before JSON parsing.
11. Keep Wi-Fi radio off when the bridge is disabled:
   `WiFi.disconnect(true, false)` or equivalent plus `WiFi.mode(WIFI_OFF)`.
12. Prefer lower TX power during bring-up/testing, then expose a setting only if
    range is a real problem.
13. Capture reset reason and boot count around Wi-Fi changes. A boot-loop guard
    is required for all future Wi-Fi features.
14. For field debugging, capture:
    - serial ROM reset reason
    - firmware `status` ack
    - bridge Unix-socket `/health`
    - current saved bridge config with secrets redacted

## Current Boot-Loop Hypothesis

The observed loop started after saving Wi-Fi credentials. The most likely
firmware-side failure mode is:

1. Wi-Fi credentials are saved.
2. `settings().wifi` is persisted as enabled.
3. On every boot, Wi-Fi starts automatically.
4. Wi-Fi bring-up or coexistence triggers a reset.
5. Because `settings().wifi` is still enabled, the next boot repeats the cycle.

The immediate mitigation is a boot-loop guard:

- delay Wi-Fi startup after boot
- count suspicious resets while Wi-Fi is enabled
- disable Wi-Fi after repeated failures
- keep host/token/SSID/password stored so the user can retry without retyping

The next refinement should be event-based Wi-Fi state handling rather than
assuming `WiFi.status()` polling is enough.

## Wi-Fi Flight Recorder

The firmware now records Wi-Fi startup phases in RTC memory so a reboot after a
manual Wi-Fi attempt can be diagnosed without opening USB CDC.

The Wi-Fi config screen shows:

- `diag`: current Wi-Fi phase
- `last`: phase recorded immediately before the previous reset
- `rst`: ESP-IDF reset reason from `esp_reset_reason()`
- `event`: last Arduino Wi-Fi event plus disconnect reason
- `ip`: acquired station IP, when available

Key phase values:

- `mode>`: about to call `WiFi.mode(WIFI_STA)`
- `mode-ok`: returned from `WiFi.mode`
- `begin>`: about to call `WiFi.begin(ssid, pass)`
- `begin-ok`: returned from `WiFi.begin`
- `wait-ip`: waiting for station IP
- `got-ip`: station received an IP
- `ws>`: about to start WebSocket client
- `ws-ok`: WebSocket connected to bridge

Interpretation:

- Reboot with `last mode>` means the reset happened inside or immediately after
  `WiFi.mode`.
- Reboot with `last begin>` means the reset happened inside or immediately after
  `WiFi.begin`.
- Reboot with `last wait-ip` means Wi-Fi started but connection/IP acquisition
  did not finish before reset.
- A disconnect event without reboot means hotspot compatibility or credentials
  are the likely next thing to inspect.
