# Firmware-suite progress

## Completed

- Repository renamed to `m5stack-cardputer-adv-firmware` and made public.
- RFID2 and Claude Buddy histories joined in one Git graph.
- Both apps moved into independent `apps/<app>/` PlatformIO workspaces.
- Shared `./cardputer` build, release, stage, and debug entrypoint added.
- Launcher 8 MB OTA partition contract pinned and compiled by both apps.
- Direct Buddy serial flash moved to recovery-only tooling.
- Global Codex skill added for this firmware collection.
- Claude Buddy v1.3.0 recovered and tagged as historical raw-source provenance;
  it remains outside the Launcher release line.
- Claude Buddy app and raw Launcher OTA package provenance advanced to v1.4.0;
  its release tag remains blocked on physical Launcher-installed proof.
- Current Buddy build, install, and debug documentation exposes Launcher-only
  normal workflow; direct flash and erase remain recovery-only.
- Claude Buddy BLE, character transfer/parser, transfer failure cleanup, status
  formatting, and host character tools hardened with focused regression coverage.
- RFID2 serial card writes, arm timeout, persisted input bounds, and Launcher
  staging hardened with focused regression coverage.

## Latest verification

- RFID2 Launcher-OTA security build: 612,529 bytes.
- Claude Buddy Launcher-OTA security build: 1,815,909 bytes.
- Both fit Launcher `ota_0` limit: 5,177,344 bytes.
- Root tool checks and focused RFID2 and Claude Buddy host security tests passed.
- Historical v1.3.0 source build passed; its clean-build hash differs from the
  observed binary as accepted floating-dependency drift.

## Pending hardware proof

- Install the current RFID2 artifact through Launcher on physical Cardputer ADV.
- Install Claude Buddy v1.4.0 through Launcher on physical Cardputer ADV.
- RFID2: boot/version/status, RFID2 I2C detection, read-only card scan.
- Claude Buddy: boot, USB serial, BLE advertising/pairing, permission prompt.
