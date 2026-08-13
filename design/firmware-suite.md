# Firmware-suite design

```text
M5Stack Cardputer ADV Firmware
├── apps/rfid2          RFID2 Clone Station
├── apps/claude-buddy   Claude Desktop Buddy
├── contracts/launcher  pinned partition contract
└── cardputer           shared developer entrypoint
```

## Boundaries

- Apps own code, PlatformIO configuration, dependencies, `.venv`, `.pio`, and
  semantic versions.
- Root owns app selection, artifact provenance, SD staging, Launcher contract,
  release policy, and operator documentation.
- External Launcher owns boot selection, USB MSC, SD browser, OTA write to
  `ota_0`, and return-to-launcher behavior.

## Artifact path

```text
app source -> PlatformIO firmware.bin -> dist/<app>/manifest + raw image
          -> Launcher USB MSC tools/ -> Launcher Install -> ota_0
```

Never install a merged full-flash image through Launcher. Buddy merged images
and direct serial flash remain recovery-only because they replace flash regions
outside normal app OTA path.

## Claude Buddy history boundary

- `claude-buddy-v1.3.0` preserves the recovered pre-Launcher raw-source
  baseline and historical image provenance. Floating legacy dependencies mean
  a successful clean rebuild may have a different SHA-256.
- The historical tag is never staged, installed, or direct-flashed.
- Claude Buddy app and raw OTA package provenance use version `1.4.0`. Current
  normal build, package, install, and debug routes use root `./cardputer` plus
  Launcher OTA. `claude-buddy-v1.4.0` becomes the first Launcher release tag
  only after physical Launcher-installed proof.

## Claude Buddy security decisions

- BLE device name may use public MAC suffix for identification, but pairing PIN
  uses ESP32 CSPRNG output generated at boot. Public hardware identity never
  supplies authentication entropy.
- Bluetooth off is authoritative: firmware does not advertise while disabled,
  disconnects an active peer, ignores RX writes and reads, purges queued bytes
  plus partial command state, and resumes advertising from an empty queue when
  enabled.
- Character folder push remains flat and content-compatible with Hardware Buddy.
  Character names and filenames allow only alphanumeric, dot, underscore, and
  hyphen characters; separators, traversal, control bytes, and truncation are
  rejected before LittleFS access.
- Transfer `total` is required and preserves 4 KiB filesystem headroom. Each
  chunk must fit declared file and package budgets, and short writes fail the
  transfer. Failed or 30-second-stalled transfers remove partial files.
  Existing valid flat packs still install unchanged.
- Character manifests share one checked 32-GIF append boundary across scalar
  and array state forms. Overflow rejects the manifest instead of truncating or
  writing beyond static storage.
- Host prep/flash tools apply the same single-component name rule and prove
  resolved containment before deletion or copying. Manifest state names and
  generated filenames use the same rule; recovery flash input is flat regular
  files only and rejects directories and symlinks before complete size accounting.
- Formatted protocol replies transmit only when `snprintf` reports complete
  output within the destination buffer.
- Optional Wi-Fi bridge splits loopback-only HTTP health/hooks from separate
  TLS WebSocket device listener. Hook/device use independent high-entropy bearer
  credentials; hook input, pending questions, and timeouts are bounded before
  parsing. Health exposes no summary, device state, or device identity.
- Physical Wi-Fi pairing accepts only complete `wss://.../device` replacement
  config containing endpoint, pairing token, and public PEM CA. Firmware sends
  token in Authorization header and uses CA-validated `beginSslWithCA`; it
  rejects legacy `ws://`, query token, weak token, partial update, and missing
  CA. TLS private key remains desktop-local.
- Character packs stage in reserved LittleFS. Transfer abort, timeout,
  malformed input, and incomplete content retain active character; only fully
  received/parsed pack replaces it. ZIP prep bounds members, compressed and
  expanded bytes, expansion ratio, safe names, and temporary extraction.
- Debug bundles redact configured secrets and omit Claude content unless
  `CARDPUTER_DEBUG_INCLUDE_CONTENT=1`; that private opt-in is not redacted.
- Wi-Fi password/pairing token require NVS. Source does not establish at-rest
  encryption: physical extraction resistance needs secure boot plus flash/NVS
  encryption provisioned and verified outside repository builds. No eFuse state
  changes occur in normal workflow.

## RFID2 security decisions

- Serial command confirmation uses one exact trailing `confirm` token; substring
  forms are never accepted. `write` and `clone` have a matching physical UI arm
  requirement, while serial literal `write-block` is rejected rather than given
  an unsafe bypass around the physical UI workflow.
- Keyboard write, clone, and clear arming uses one rollover-safe 8-second
  deadline, matching the UI text and limiting unattended destructive state.
- Persistent slot, key, and configuration inputs are bounded before parsing:
  files are limited to 16 KiB, lines to 128 characters, and the MIFARE key
  dictionary to 256 unique entries. Existing valid slot and key formats remain
  compatible.
- The legacy deploy helper no longer sends firmware or credentials over HTTP.
  It stages a local ESP32 image only to a mounted Launcher USB MSC `tools/`
  directory, verifies SHA-256 from an exclusively created temporary file before
  atomic replacement, and removes older matching versions only after the new
  image is safe. The volume and its direct `tools/` child must be real
  directories, not symlink aliases; cleanup is limited to the fixed
  `RFID2-Clone-Station-v*` artifact family and skips symlinks.
- Serial `write`/`clone` require exact trailing `confirm` plus matching fresh
  physical UI arm, target slot, and present card inside eight-second window.
  Serial literal `write-block` is disabled because arbitrary payload has no
  existing physical UI arm. Physical UI write/clone behavior remains unchanged.
- RFID dump/key data on removable microSD and USB serial is owner-operated lab
  data. This release does not claim encrypted storage; confidential deployments
  need user-approved encrypted storage or hardware-backed key design.
