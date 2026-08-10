# Building and Flashing

This project builds the Cardputer-Adv firmware with PlatformIO.

The wrapper uses a local `.venv` PlatformIO install when a global `pio` is
not available. Set `PYTHON_BIN=/path/to/python3.12` if you want to choose the
Python interpreter explicitly.

## Build

```bash
./scripts/build_cardputer_adv.sh
```

The build creates the normal PlatformIO firmware under `.pio/` and a merged image at:

```text
release/cardputer-adv-merged.bin
```

That merged binary can be written at flash offset `0x0` and is the file to hand to M5Burner custom firmware.

## Flash

Put the Cardputer-Adv into download mode:

1. Set the side power switch to `OFF`.
2. Hold `G0`.
3. Plug in USB-C power/data.
4. Release `G0`.

Then flash:

```bash
./scripts/flash_cardputer_adv.sh
```

The script scans USB serial ports. If exactly one device is present, it uses
that port. If multiple USB serial ports are present, it prompts you to select
one.

If auto port detection misses the device or you want to bypass the prompt:

```bash
./scripts/flash_cardputer_adv.sh /dev/cu.usbmodemXXXX
```

Clean erase before a first install or when switching firmware families:

```bash
./scripts/erase_cardputer_adv.sh
./scripts/flash_cardputer_adv.sh
```

## Flash an Archived Binary

Archived merged images live under `release/archive/`. Flash one directly without
rebuilding:

```bash
./scripts/flash_cardputer_adv_bin.sh release/archive/cardputer-adv-stable-latest.bin
```

The binary flash script uses the same USB serial port detection as the normal
flash script. Pass a port as the second argument to bypass auto-selection:

```bash
./scripts/flash_cardputer_adv_bin.sh release/archive/cardputer-adv-stable-latest.bin /dev/cu.usbmodemXXXX
```

## Normal Boot

On Cardputer-Adv builds, the firmware boots straight into the normal Hardware
Buddy splash and home UI.

## Pair with Claude

1. Open Claude Desktop or Claude Code Desktop.
2. Enable Developer Mode from `Help -> Troubleshooting -> Enable Developer Mode`.
3. Open `Developer -> Open Hardware Buddy...`.
4. Connect to the device advertising as `Claude-XXXX`.
5. If macOS asks for a Bluetooth passcode, type the six digits shown on the
   Cardputer-Adv pairing screen.

Use the Hardware Buddy picker rather than macOS Bluetooth Settings for normal
pairing. The macOS passcode dialog is expected, but the passcode must come
from the firmware's pairing screen.

If the passcode prompt still blocks pairing, forget the old `Claude-XXXX`
device in macOS Bluetooth settings, reboot the Cardputer-Adv, and reconnect
from the Hardware Buddy picker.

## Custom Character Packs

Anthropic's folder push protocol is unchanged from upstream. Drag a character folder with `manifest.json` and GIF state files into the Hardware Buddy window. The project auto-formats a fresh LittleFS partition on first boot so GIF packs can be received after a clean flash.
