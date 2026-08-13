# Building, Releasing, and Installing

Claude Buddy uses the collection root entrypoint and Launcher OTA contract.
Run all firmware commands from the collection repository root.

The app-local wrapper uses a local `.venv` PlatformIO install when a global
`pio` is unavailable. Set `PYTHON_BIN=/path/to/python3.12` to select a Python
interpreter.

## Build

```bash
./cardputer build claude-buddy
```

The build creates the raw Launcher-compatible application image at:

```text
apps/claude-buddy/.pio/build/cardputer-adv-launcher-ota/firmware.bin
```

## Release

```bash
./cardputer release claude-buddy
```

Release validates the ESP application header and Launcher size limit, then
writes a versioned raw image and provenance manifest under
`dist/claude-buddy/`.

## Install through Launcher

1. Boot Launcher. Hold any key during app boot to return to it.
2. Open Launcher **Settings → USB MSC**.
3. Run:

```bash
./cardputer stage claude-buddy
```

4. Exit USB MSC, open `tools/`, select the staged Claude Buddy image, and choose
   **Install**.
5. Launcher writes the app to `ota_0` and keeps Launcher available.

## Normal Boot

Claude Buddy boots into the Hardware Buddy splash and home UI. Hold any key
during boot to return to Launcher.

## Debug

```bash
./cardputer debug claude-buddy serial
./cardputer debug claude-buddy ble
```

Serial and BLE proof remain separate. Physical-device validation is also
separate from a successful local build.

## Recovery boundary

Full-flash tools are isolated under `scripts/recovery/`. They can replace or
erase Launcher and require explicit recovery approval. They are not used for
normal build, release, install, or runtime debugging.

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

Pairing codes come from the firmware pairing screen. Do not infer a code from
public device identity.

## Custom Character Packs

Anthropic's folder push protocol is unchanged from upstream. Drag a character
folder with `manifest.json` and GIF state files into the Hardware Buddy window.
The project auto-formats a fresh LittleFS partition on first boot so GIF packs
can be received on a fresh filesystem.
