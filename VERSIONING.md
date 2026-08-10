# Versioning

## Existing history

RFID2 releases through `v1.5.7` predate this collection and remain unchanged.
They identify RFID2 only.

Current collection metadata advances RFID2 to `1.5.8` for its renamed runtime
identity (`m5stack-cardputer-adv-rfid2`).

Claude Buddy arrived with source history but no Git release tags. Its current
collection metadata starts at `1.0.0`.

## New releases

Use namespaced annotated tags from now on:

```text
rfid2-vX.Y.Z
claude-buddy-vX.Y.Z
```

Before a release:

1. Update the target app's `app.env` version.
2. For RFID2, update `src/main.cpp` `kFwVersion` to exactly same version.
3. Run `./cardputer release <app>` and inspect generated manifest.
4. Stage only raw `firmware.bin` through Launcher OTA.
5. Create annotated namespaced tag after device smoke test.

Artifacts include app ID, version, Git commit, dirty-tree flag, image SHA-256,
and pinned Launcher contract commit.
