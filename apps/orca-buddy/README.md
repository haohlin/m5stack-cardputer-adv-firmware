# Orca Cardputer Buddy

Independent Orca Cardputer app contract, version `0.1.0`.

Firmware provides an independent Orca status console, local Wi-Fi setup,
secure USB serial pairing, CA-validated WSS transport, notices, bounded
questions, and keyboard prompt drafts. It shares only documented
`orca-cardputer/v1` messages with `desktop-bridge`; no Claude compatibility
code, protocol, characters, naming, or assets are used.

The local Orca Desktop plugin source is in [`orca-plugin`](orca-plugin/). Add
that folder only through **Settings → Plugins → Development**. It is a
read-only worktree-context and setup panel: it neither installs the bridge nor
touches a Cardputer. Its guide documents the separate bridge and USB pairing
steps.

Normal build, package, stage, and serial-monitor entrypoints are exposed only
through `./cardputer`. Launcher installs the raw app image from `tools/`.
See [`../../design/orca-cardputer-buddy.md`](../../design/orca-cardputer-buddy.md).

Run host behavior tests before firmware builds:

```sh
bash apps/orca-buddy/tests/run_host_tests.sh
./cardputer build orca-buddy
```

Device setup scans Wi-Fi and accepts a keyed passphrase locally. Generate the
single protected `orca-pair` payload with desktop bridge management tooling,
then send it without revealing contents:

```sh
./apps/orca-buddy/scripts/write_pairing_serial.sh "$PAIR_FILE" [serial-port]
```

The firmware accepts one complete `orca-pair` line and emits only a success or
failure acknowledgement. `W` scans and configures Wi-Fi on-device; `F` opens a
local forget confirmation; `P` enters a prompt draft. Pairing bearer and CA
contents are never shown. Normal workflow has no direct-flash helper.
