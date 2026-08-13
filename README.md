# Cardputer firmware

Two independent Cardputer ADV firmware projects in one repository:

- `apps/rfid2` — Unit RFID2 MIFARE Classic lab-card reader/writer/clone tool.
- `apps/claude-buddy` — Claude Hardware Buddy BLE device.

They share a single installation contract, not a shared firmware codebase:

```sh
./cardputer build rfid2
./cardputer release rfid2
./cardputer stage rfid2
```

Replace `rfid2` with `claude-buddy`. `stage` requires Launcher USB MSC mode;
Launcher then installs the raw app image from SD `tools/` through OTA. See
[development guide](docs/development.md).
