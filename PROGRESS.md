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
- Codex Security standard scan `804e44c6-b026-4ffe-bc91-3a04c568e42d` sealed
  bridge transport/auth, hook input bounds, character/archive availability,
  debug-content, and RFID serial confirmation findings. Source remediation
  requires CA-validated WSS, atomic bridge config replacement, local protected
  hook credential, transactional character staging, bounded ZIP extraction,
  exact confirmation, and matching physical UI arm for serial write/clone.
- Task 7b review remediation now requires CSPRNG-shaped bridge credentials,
  stores complete bridge state in one crash-atomic NVS record, binds serial
  write/clone to the armed card identity, and bounds loopback hook listener
  timeouts, connections, and requests per socket.

## Latest verification

- RFID2 Launcher-OTA review-remediation build: 610,273 bytes.
- Claude Buddy Launcher-OTA review-remediation build: 1,817,425 bytes.
- Both fit Launcher `ota_0` limit: 5,177,344 bytes.
- Root tool checks and focused RFID2 and Claude Buddy host security tests passed.
- Historical v1.3.0 source build passed; its clean-build hash differs from the
  observed binary as accepted floating-dependency drift.
- Security regressions cover unauthenticated/oversized hook rejection,
  sanitized health, configured/generated token rules, hook admission limits,
  secure pairing, atomic bridge-record integrity, bridge transfer rejection,
  character rollback, ZIP limits, and RFID arm identity matching.

## Pending hardware proof

- Install the current RFID2 artifact through Launcher on physical Cardputer ADV.
- Install Claude Buddy v1.4.0 through Launcher on physical Cardputer ADV.
- RFID2: boot/version/status, RFID2 I2C detection, read-only card scan.
- Claude Buddy: boot, USB serial, BLE advertising/pairing, permission prompt.

## External security residuals

- Source validation cannot prove deployed ESP32 secure boot, flash encryption,
  or NVS encryption. Enabling them is a separate irreversible provisioning
  decision; no eFuse changes occur here.
- RFID2 microSD dump/key contents and host physical USB are owner-operated lab
  data. This release documents but does not claim encrypted removable storage.
- Physical Wi-Fi bridge needs operator-provided TLS certificate/private key/
  public CA and device smoke proof. Private key must remain outside repository.
