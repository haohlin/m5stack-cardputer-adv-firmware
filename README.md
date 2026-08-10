# M5Stack Cardputer ADV Firmware

Public firmware collection for the M5Stack Cardputer ADV (ESP32-S3). Each app
stays an independent PlatformIO project; shared tooling provides one safe
Launcher-OTA installation path.

Project home: [haohlin/m5stack-cardputer-adv-firmware](https://github.com/haohlin/m5stack-cardputer-adv-firmware).
Formerly `cardputer-rfid2-fw`; RFID2 release history remains intact.

- `apps/rfid2` — RFID2 Clone Station for owned MIFARE Classic lab cards.
- `apps/claude-buddy` — Claude Desktop Buddy BLE companion device.

## Quick start

```sh
./cardputer build rfid2
./cardputer release rfid2
./cardputer stage rfid2
```

Replace `rfid2` with `claude-buddy`. `stage` requires Launcher USB MSC mode;
Launcher then installs the raw app image from SD `tools/` through OTA. See
[development guide](docs/development.md), [versioning policy](VERSIONING.md),
and [firmware-suite design](design/firmware-suite.md).

## Project rules

- Launcher stays external and permanent. User firmware is staged as a raw app
  image and installed through Launcher OTA.
- Apps share deployment contract only. Do not merge PlatformIO environments,
  dependencies, source code, or release versions.
- Existing RFID2 `v*` tags are retained as legacy history. New collection tags
  are namespaced by app.

## License

Each app retains its own licensing material. No root-wide license has been
chosen for the collection; do not assume one from this README.
