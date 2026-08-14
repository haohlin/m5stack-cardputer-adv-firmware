# Orca Cardputer Buddy

Independent Orca Cardputer app contract, version `0.1.0`.

This directory intentionally contains only Launcher-compatible metadata,
PlatformIO configuration, and generic root-tooling wrappers. Task 1 includes
no product firmware, bridge, protocol, Claude compatibility code, or graphics.

Normal build, package, stage, and serial-monitor entrypoints are exposed only
through `./cardputer`. Launcher installs the raw app image from `tools/`.
See [`../../design/orca-cardputer-buddy.md`](../../design/orca-cardputer-buddy.md).
