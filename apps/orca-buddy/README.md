# Orca Cardputer Buddy

Independent Orca Cardputer app contract, version `0.1.6`. Firmware header
shows this version on device and release checks keep it equal to bridge/plugin
metadata.

Firmware provides an independent Orca status console, local Wi-Fi setup,
secure USB serial pairing, CA-validated WSS transport, notices, bounded
questions, and keyboard prompt drafts. It shares only documented
`orca-cardputer/v1` messages with `desktop-bridge`; no Claude compatibility
code, protocol, characters, naming, or assets are used.

The local Orca Desktop plugin source is in [`orca-plugin`](orca-plugin/). Add
that folder only through **Settings → Plugins → Development**. Its read-only
panel shows focused worktree context; explicit Command Palette commands start,
stop, and inspect the local bridge or send protected USB pairing material. No
action runs when plugin is added or enabled. See its guide for exact flow.

Normal build, package, stage, and serial-monitor entrypoints are exposed only
through `./cardputer`. Launcher installs the raw app image from `tools/`.
See [`../../design/orca-cardputer-buddy.md`](../../design/orca-cardputer-buddy.md).

Run host behavior tests before firmware builds:

```sh
bash apps/orca-buddy/tests/run_host_tests.sh
./cardputer build orca-buddy
```

First setup scans Wi-Fi and accepts a keyed passphrase locally. Firmware saves
that network, then directly re-associates at every boot/reconnect when it is in
range; no rescan or password re-entry. During a connection attempt, press
**Del** to cancel or **Ctrl + [** for Escape. Cancelling a saved-network retry
keeps credentials but pauses automatic retry until a later successful/manual
connection. Generate single protected `orca-pair` payload with desktop bridge
management tooling, then send it without revealing contents:

```sh
./apps/orca-buddy/scripts/write_pairing_serial.sh "$PAIR_FILE" [serial-port]
```

The firmware accepts one complete `orca-pair` line and emits only a success or
failure acknowledgement. `W` scans and configures Wi-Fi on-device; `F` opens a
local forget confirmation; `P` enters a prompt draft. Pairing bearer and CA
contents are never shown. Normal workflow has no direct-flash helper.

Wi-Fi is persisted as its own small checksummed record. Saving a connected
network never rewrites bridge bearer or CA material, so Wi-Fi setup does not
depend on large-pairing storage. Pairing uses a separate record and may use its
safe SPIFFS fallback only when needed. Version `0.1.6` does not reuse the
unreliable combined record from earlier development builds: save Wi-Fi, then
pair once again. USB pairing waits for a transient ESP32-S3 CDC re-enumeration
before it sends paced frames.

The USB command `status` has one fixed secret-free reply: firmware version,
saved/live Wi-Fi, saved pairing, live WSS bridge, and pairing store. Use the
plugin's **Device Diagnostics** command or `read_device_status.sh`; neither
shows an SSID, passphrase, bearer, CA, or endpoint.

If pairing fails, use **Orca Cardputer: Bridge Status** first. It reports
`failed` when launchd records a nonzero bridge exit rather than treating a
loaded-but-dead service as running. The plugin also writes mode-0600,
secret-free lifecycle records at
`~/.orca-cardputer-bridge/logs/plugin-events.jsonl`: payload-created,
USB-transfer-begin, device acknowledgement, or a fixed failure class. These
records never contain payloads, tokens, certificates, Wi-Fi credentials, raw
serial output, or subprocess text.
