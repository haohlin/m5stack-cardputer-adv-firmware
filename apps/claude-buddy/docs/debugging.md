# Debugging Playbook

This project has two transports that can fail independently:

- **Official Claude Desktop Hardware Buddy BLE**: pairing, heartbeat status,
  assistant turn events, permission approve/deny, and folder push.
- **Local USB serial**: bridge provisioning and optional firmware status.

Do not open USB serial against a working BLE/Wi-Fi session unless you need it.
On Cardputer ADV, macOS opening USB CDC can reset/re-enumerate the ESP32-S3.
Use BLE/bridge/device-screen checks first; use serial status only when the
device is already in a broken state or when provisioning bridge config.

## Quick Triage

1. Confirm the exact firmware that is running:

   ```bash
   ./scripts/collect_debug_bundle.sh
   ```

   By default this does not open USB CDC for a status query. If the device is
   already broken and serial disruption is acceptable, run:

   ```bash
   CARDPUTER_DEBUG_SERIAL=1 ./scripts/collect_debug_bundle.sh
   ```

   A successful device status should include:

   ```json
   "fw": "adv-dev-bridge-serial"
   ```

2. Confirm the USB serial port:

   ```bash
   ./scripts/select_cardputer_port.sh
   ```

3. If bridge config is the problem, provision over USB serial:

   ```bash
   CARDPUTER_BRIDGE_HOST="$(ipconfig getifaddr en1)" \
     CARDPUTER_WIFI_ON_DEVICE=1 \
     ./scripts/write_bridge_config_serial.sh
   ```

   If `en1` is empty, check `en0`:

   ```bash
   ipconfig getifaddr en0
   ipconfig getifaddr en1
   ```

4. Enter Wi-Fi on the device only if it was omitted from the host command:

   ```text
   Menu -> settings -> wifi -> enter wifi
   ```

5. Start the desktop bridge on a LAN-reachable interface:

   ```bash
   cd desktop-bridge
   CARDPUTER_BRIDGE_BIND_HOST=0.0.0.0 npm run start
   ```

6. Check the device Wi-Fi page and the debug bundle for:

   ```text
   bridge ok
   ```

## Bridge Config Flow

Preferred provisioning path:

```bash
./scripts/write_bridge_config_serial.sh
```

Optional explicit host:

```bash
CARDPUTER_BRIDGE_HOST=192.168.1.23 ./scripts/write_bridge_config_serial.sh
```

Optional host plus Wi-Fi credentials from the Mac:

```bash
CARDPUTER_BRIDGE_HOST=192.168.1.23 \
CARDPUTER_WIFI_SSID="your ssid" \
CARDPUTER_WIFI_PASS="your password" \
./scripts/write_bridge_config_serial.sh
```

Host-only provisioning is usually safer for shared logs because the Wi-Fi
password never leaves the device keyboard:

```bash
CARDPUTER_BRIDGE_HOST=192.168.1.23 \
CARDPUTER_WIFI_ON_DEVICE=1 \
./scripts/write_bridge_config_serial.sh
```

The only bridge-side data the device needs from the Mac is:

- bridge host/IP
- bridge port
- pairing token

The device also needs Wi-Fi credentials to join the same LAN, but those can be
typed on the device and stored in NVS.

## BLE Folder Push

Use Claude Desktop Hardware Buddy folder push for character packs and other
non-critical data. Do not use it as the primary bridge provisioning path while
debugging.

Claude Desktop may show or send a folder name such as:

```text
release/bridge-config
```

That prefix is the selected folder path relative to the repo, not a stable
protocol field. Firmware should treat folder names and file paths as unstable
and detect config by content/file basename, not by exact folder name.

If folder push fails with:

```text
char_end failed - character did not reload
```

debug it as a transfer classification problem:

- Did the device receive `bridge.json`?
- Was the path nested, for example `release/bridge-config/bridge.json`?
- Did firmware try to call `characterInit()` instead of saving bridge config?
- Can the same config be saved over USB serial?

If USB serial provisioning works, the bridge config itself is valid and the
remaining problem is BLE folder transfer handling.

## Evidence Capture

Run:

```bash
./scripts/collect_debug_bundle.sh
```

The script writes a local bundle under:

```text
release/debug/
```

It collects:

- git revision and dirty state
- firmware archive manifest
- visible USB serial ports
- redacted bridge config files
- device `status` ack over USB serial only when `CARDPUTER_DEBUG_SERIAL=1`
- recent Claude Desktop buddy-related log lines
- local bridge health endpoint, if running

Secrets are redacted from collected JSON. Do not paste raw bridge config files
or Wi-Fi credentials into issues or chat.

## Recommended Debug Order

Use this order to avoid chasing symptoms across layers:

1. **Build identity**: install the current Launcher OTA artifact and confirm the
   device boots without serial probing.
2. **USB command path**: only if needed, confirm `status` and `bridge_config`
   acks with `CARDPUTER_DEBUG_SERIAL=1`.
3. **Stored config**: confirm bridge host/token are saved and Wi-Fi is either
   valid or intentionally missing.
4. **Network reachability**: ensure Mac and Cardputer are on the same LAN.
   VPNs and hotspot isolation can block local traffic.
5. **Bridge process**: run desktop bridge with
   `CARDPUTER_BRIDGE_BIND_HOST=0.0.0.0`.
6. **WebSocket state**: check device Wi-Fi page for `bridge dial` then
   `bridge ok`.
7. **BLE baseline**: confirm Claude Desktop shows connected/encrypted and
   permission approve/deny still works.
8. **Folder push**: only then test folder push and classify failures separately
   from bridge config validity.

## Common Failures

### No USB serial response

Check the port:

```bash
./scripts/select_cardputer_port.sh
```

Install a Launcher OTA build that initializes USB serial. Status should include
`fw`.

### `bad host (empty)`

The device parsed a bridge config command but did not see `host`. Regenerate
with:

```bash
CARDPUTER_BRIDGE_HOST="$(ipconfig getifaddr en1)" \
  CARDPUTER_WIFI_ON_DEVICE=1 \
  ./scripts/write_bridge_config_serial.sh
```

If needed, pass the port explicitly:

```bash
./scripts/write_bridge_config_serial.sh /dev/cu.usbmodem1101
```

### `bridge wait` or `bridge dial`

The device has Wi-Fi and config, but the WebSocket bridge is not reachable.
Start the bridge with:

```bash
cd desktop-bridge
CARDPUTER_BRIDGE_BIND_HOST=0.0.0.0 npm run start
```

Then verify the host IP is on the same network as the device.

### Claude Desktop shows folder-send error

First prove config validity over USB serial. If that works, keep the config
and debug BLE folder transfer separately.

## What Codex Should Do In Future Debug Runs

Codex should follow this sequence:

1. Read `graphify-out/GRAPH_REPORT.md`.
2. Capture current state with `git status --short --branch`.
3. Prefer `rg` for source search.
4. Verify the device via USB serial before changing BLE logic.
5. Add or run focused scripts rather than relying on screenshots alone.
6. Build and release the exact Launcher OTA image being tested through root
   `./cardputer`.
7. Install through Launcher USB MSC and Launcher OTA only when hardware work is
   explicitly in scope.
8. Run shell syntax checks, `git diff --check`, and `./scripts/graphify_update.sh`
   after code changes.
9. Do not commit until the feature is working and explicitly approved.
