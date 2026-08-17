# Versioning

## Existing history

RFID2 releases through `v1.5.7` predate this collection and remain unchanged.
They identify RFID2 only.

Current collection metadata advances RFID2 to `1.5.9` for its renamed runtime
identity (`m5stack-cardputer-adv-rfid2`) and published security hardening.

`claude-buddy-v1.3.0` is an immutable historical raw-source provenance tag. It
records the recovered pre-Launcher source and observed historical image hash;
it is not a Launcher release and must never be staged, installed, or flashed.
See [`docs/history/claude-buddy-v1.3.0.md`](docs/history/claude-buddy-v1.3.0.md).

Claude Buddy app and package provenance now use version `1.4.0`.
`claude-buddy-v1.4.0` will identify the first Launcher release. Create that tag
only after the Launcher-installed physical smoke test succeeds. Version
metadata does not make the historical v1.3.0 image Launcher-installable.

Orca Cardputer Buddy current source version is `0.1.5`. Firmware artifact,
Orca Desktop development plugin, and desktop bridge package share this version
so users can see which source set is loaded. It has no firmware artifact or
release tag yet. Reserve `orca-buddy-v0.1.5` for first completed
Launcher-installed physical smoke test after product implementation.

## New releases

Use namespaced annotated tags from now on:

```text
rfid2-vX.Y.Z
claude-buddy-vX.Y.Z
orca-buddy-vX.Y.Z
```

Before a release:

1. Update the target app's `app.env` version.
2. For RFID2, update `src/main.cpp` `kFwVersion` to exactly same version.
3. Run `./cardputer release <app>` and inspect generated manifest.
4. Stage only raw `firmware.bin` through Launcher OTA.
5. Create annotated namespaced tag after device smoke test.

Artifacts include app ID, version, Git commit, dirty-tree flag, image SHA-256,
and pinned Launcher contract commit.
