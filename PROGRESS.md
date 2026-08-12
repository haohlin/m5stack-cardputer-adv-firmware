# Firmware-suite progress

## Completed

- Repository renamed to `m5stack-cardputer-adv-firmware` and made public.
- RFID2 and Claude Buddy histories joined in one Git graph.
- Both apps moved into independent `apps/<app>/` PlatformIO workspaces.
- Shared `./cardputer` build, release, stage, and debug entrypoint added.
- Launcher 8 MB OTA partition contract pinned and compiled by both apps.
- Direct Buddy serial flash moved to recovery-only tooling.
- Global Codex skill added for this firmware collection.
- Claude Buddy BLE, character transfer/parser, status formatting, and host
  character tools hardened with focused regression coverage.
- RFID2 serial card writes, arm timeout, persisted input bounds, and Launcher
  staging hardened with focused regression coverage.

## Latest verification

- RFID2 Launcher-OTA security build: 612,529 bytes.
- Claude Buddy Launcher-OTA security build: 1,294,493 bytes.
- Both fit Launcher `ota_0` limit: 5,177,344 bytes.
- Raw image header, release manifest, checksum, and temporary-SD staging passed.

## Pending hardware proof

- Install each current artifact through Launcher on physical Cardputer ADV.
- RFID2: boot/version/status, RFID2 I2C detection, read-only card scan.
- Claude Buddy: boot, USB serial, BLE advertising/pairing, permission prompt.
