#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

if [[ $# -gt 1 ]]; then
  ansi_fail "Usage: $0 [serial-port]"
  exit 2
fi

PORT="$(./scripts/select_cardputer_port.sh "${1:-}")"
ansi_info "Using Cardputer ADV port: $PORT"

ENV_NAME="${CARDPUTER_ADV_ENV:-cardputer-adv-rfid2}"
BOOTLOADER="$ROOT/.pio/build/$ENV_NAME/bootloader.bin"
PARTITIONS="$ROOT/.pio/build/$ENV_NAME/partitions.bin"
FIRMWARE="$ROOT/.pio/build/$ENV_NAME/firmware.bin"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
LAUNCHER_BIN="/tmp/launcher-bins/Launcher-m5stack-cardputer.bin"
LAUNCHER_URL_TAG="2.7.2"
LAUNCHER_URL_REPO="bmorcelli/Launcher"

if [[ ! -f "$ESPTOOL" ]]; then
  ansi_fail "Could not find PlatformIO esptool.py at $ESPTOOL"
  exit 1
fi

# ---- Build ---------------------------------------------------------------
start_seconds="$SECONDS"
ansi_info "Building $ENV_NAME before flash"
./scripts/pio_local.sh run -e "$ENV_NAME"

for image in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE" "$BOOT_APP0"; do
  if [[ ! -f "$image" ]]; then
    ansi_fail "Missing flash image: $image"
    exit 1
  fi
done

# ---- Extract version from firmware binary --------------------------------
FW_VERSION="$(grep -ao 'cardputer-rfid2-fw\x00[^\x00]*' "$FIRMWARE" 2>/dev/null | head -1 | cut -d$'\0' -f2 || true)"
if [[ -z "$FW_VERSION" ]]; then
  # Fallback: read kFwVersion from source
  FW_VERSION="$(grep -oP 'kFwVersion\[\] = "\K[^"]+' src/main.cpp 2>/dev/null || echo "unknown")"
fi
ansi_info "Firmware version: $FW_VERSION"

# ---- Flash RFID2 firmware ------------------------------------------------
ansi_info "Writing RFID2 firmware at 115200 baud"
if PYTHONPATH="$HOME/.platformio/packages/tool-esptoolpy" \
  "$ROOT/.venv/bin/python" "$ESPTOOL" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud 115200 \
    --before default_reset \
    --after watchdog_reset \
    write_flash -z \
    --flash_mode keep \
    --flash_freq keep \
    --flash_size keep \
    0x0 "$BOOTLOADER" \
    0x8000 "$PARTITIONS" \
    0xe000 "$BOOT_APP0" \
    0x10000 "$FIRMWARE"; then
  ansi_ok "RFID2 flash succeeded in $((SECONDS - start_seconds))s"
else
  status=$?
  ansi_fail "RFID2 flash failed in $((SECONDS - start_seconds))s"
  exit "$status"
fi

# ---- Copy firmware bin to SD card (if mounted via USB MSC) ---------------
SD_BIN_NAME="RFID2-Clone-Station-v${FW_VERSION}.bin"
SD_TOOLS=""
for vol in /Volumes/*/; do
  if [[ -d "${vol}tools" ]] && [[ -d "${vol}rfid" ]]; then
    SD_TOOLS="${vol}tools"
    break
  fi
done

if [[ -n "$SD_TOOLS" ]]; then
  ansi_info "SD card found at $(dirname "$SD_TOOLS") — updating tools/$SD_BIN_NAME"
  # Remove old RFID2 bins
  for old in "$SD_TOOLS"/RFID2*.bin; do
    [[ -f "$old" ]] && rm "$old" && ansi_info "  removed: $(basename "$old")"
  done
  cp "$FIRMWARE" "$SD_TOOLS/$SD_BIN_NAME"
  ansi_ok "Deployed $SD_BIN_NAME to SD card tools/"
  diskutil quiet eject "$(dirname "$SD_TOOLS")" 2>/dev/null && ansi_info "SD card ejected" || true
else
  ansi_info "SD card not mounted — skipping SD deploy (enable USB MSC in launcher to update SD)"
fi

# ---- Reflash launcher firmware -------------------------------------------
# RFID2 has no web UI or USB MSC. Always restore the launcher so those
# features are available immediately after the dev cycle.
ansi_info "Restoring launcher firmware..."
if [[ ! -f "$LAUNCHER_BIN" ]]; then
  ansi_info "Downloading launcher binary..."
  mkdir -p "$(dirname "$LAUNCHER_BIN")"
  gh release download "$LAUNCHER_URL_TAG" \
    --repo "$LAUNCHER_URL_REPO" \
    --pattern "Launcher-m5stack-cardputer.bin" \
    -D "$(dirname "$LAUNCHER_BIN")"
fi

if PYTHONPATH="$HOME/.platformio/packages/tool-esptoolpy" \
  "$ROOT/.venv/bin/python" "$ESPTOOL" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud 115200 \
    --before default_reset \
    --after watchdog_reset \
    write_flash -z \
    --flash_mode keep \
    --flash_freq keep \
    --flash_size keep \
    0x0 "$LAUNCHER_BIN"; then
  ansi_ok "Launcher restored — connect to WiFi to use web UI / USB MSC"
else
  ansi_fail "Launcher reflash failed — device is running RFID2 only"
fi
