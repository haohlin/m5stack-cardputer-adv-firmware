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
