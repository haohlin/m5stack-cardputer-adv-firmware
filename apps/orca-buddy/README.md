# Orca Cardputer Buddy

Independent Orca Cardputer app contract, version `0.1.0`.

Firmware provides an independent Orca status console, local Wi-Fi setup,
secure USB serial pairing, CA-validated WSS transport, notices, bounded
questions, and keyboard prompt drafts. It shares only documented
`orca-cardputer/v1` messages with `desktop-bridge`; no Claude compatibility
code, protocol, characters, naming, or assets are used.

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
then send that complete line over USB serial. Pairing bearer and CA contents
are never shown. `W` reconfigures Wi-Fi; `F` opens a local forget confirmation;
`P` enters a prompt draft. Normal workflow has no direct-flash helper.
