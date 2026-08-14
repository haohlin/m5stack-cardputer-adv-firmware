# M5Stack Cardputer ADV Firmware development

`apps/rfid2`, `apps/claude-buddy`, and `apps/orca-buddy` are independent
PlatformIO projects. Each
owns its source, `platformio.ini`, libraries, `.venv`, `.pio`, and release
metadata. Root tooling only selects an app and enforces shared Launcher install
contract.

## Normal development loop

```sh
./cardputer build rfid2
./cardputer release rfid2
./cardputer stage rfid2
```

Repeat with `claude-buddy` or `orca-buddy`. `release` produces raw ESP application image and
JSON provenance manifest under `dist/<app>/`. `stage` rebuilds, verifies ESP
image header, enforces Launcher OTA size, copies only raw image to mounted
Launcher SD `tools/`, verifies SHA-256, and removes only older binaries with
same app prefix. Staging rejects symlinked root, `tools`, target, or temporary
entries; copy, checksum, atomic replace, and cleanup remain anchored to
validated `tools/` directory descriptor.

Device steps: boot Launcher (hold any key while installed app boots to return),
open **Settings → USB MSC**, run `stage`, exit USB MSC, then select new file in
`tools/` and choose **Install**. Launcher stays installed and writes app to
`ota_0`.

Set `CARDPUTER_SD_ROOT=/Volumes/<volume>` when volume auto-detection is
ambiguous. Set `CARDPUTER_NO_EJECT=1` when manual ejection is preferred.

## Debug flow

```sh
./cardputer debug rfid2 serial [port]
./cardputer debug rfid2 status [port]
./cardputer debug rfid2 jtag
./cardputer debug claude-buddy serial [port]
./cardputer debug claude-buddy ble
./cardputer debug orca-buddy serial [port]
```

RFID2 smoke test: `version`, `status`, `scan`, confirm SD/RAM state, then use a
read-only card scan before write or clone. Buddy smoke test: boot, USB serial
identity, BLE advertising/pairing, and one permission-prompt path.

Orca Buddy operator path: build/release/stage through Launcher, initialize and
install `apps/orca-buddy/desktop-bridge` on an explicit LAN address, create a
protected `orca-pair` payload, then run
`apps/orca-buddy/scripts/write_pairing_serial.sh` once. On the device press
`W` to scan/select Wi-Fi and enter its passphrase; verify a status snapshot,
one bounded question answer, one prompt draft, and reconnect after a bridge
restart. Do not run the serial sender while a serial monitor owns the port.

## Recovery boundary

`apps/claude-buddy/scripts/recovery/` retains direct serial flash, erase, and
merged-image tooling. Recovery only; never routine install flow. If Launcher is
wiped, restore it from pinned external source, then re-check `launcher.lock`
before staging apps.

## Versioning

Read [`../VERSIONING.md`](../VERSIONING.md) before release. New tags are
app-namespaced: `rfid2-vX.Y.Z`, `claude-buddy-vX.Y.Z`, and `orca-buddy-vX.Y.Z`.
`claude-buddy-v1.3.0` is historical raw-source provenance, not a Launcher
release. Claude Buddy package metadata is now `1.4.0`.
`claude-buddy-v1.4.0` will be the first Launcher release tag and requires
physical Launcher-installed proof before creation.
