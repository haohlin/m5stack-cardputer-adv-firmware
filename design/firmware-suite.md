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
