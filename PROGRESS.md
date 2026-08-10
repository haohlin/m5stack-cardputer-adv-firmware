# Firmware-suite progress

## Completed

- Repository renamed to `m5stack-cardputer-adv-firmware` and made public.
- RFID2 and Claude Buddy histories joined in one Git graph.
- Both apps moved into independent `apps/<app>/` PlatformIO workspaces.
- Shared `./cardputer` build, release, stage, and debug entrypoint added.
- Launcher 8 MB OTA partition contract pinned and compiled by both apps.
- Direct Buddy serial flash moved to recovery-only tooling.
- Global Codex skill added for this firmware collection.

## Latest verification

- RFID2 Launcher-OTA build: 611,397 bytes.
- Claude Buddy Launcher-OTA build: 1,293,277 bytes.
- Both fit Launcher `ota_0` limit: 5,177,344 bytes.
- Raw image header, release manifest, checksum, and temporary-SD staging passed.

## Pending hardware proof

- Install each current artifact through Launcher on physical Cardputer ADV.
- RFID2: boot/version/status, RFID2 I2C detection, read-only card scan.
- Claude Buddy: boot, USB serial, BLE advertising/pairing, permission prompt.
