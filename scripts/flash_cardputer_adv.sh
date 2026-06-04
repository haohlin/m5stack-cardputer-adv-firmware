#!/usr/bin/env bash
# Build RFID2 firmware and deploy the .bin to the launcher SD card.
#
# THE LAUNCHER FIRMWARE ALWAYS STAYS ON THE DEVICE.
# Never flash RFID2 (or any other firmware) directly to the device via esptool.
# The correct flow is:
#   1. This script builds the firmware and copies the .bin to the SD card tools/ folder.
#   2. The SD card must be mounted via the launcher's USB MSC mode (Settings → USB MSC).
#   3. After the SD is updated, eject it and exit USB MSC mode in the launcher.
#   4. From the launcher file browser navigate to tools/ → select the bin → Install.
#   5. The launcher's OTA mechanism flashes it to the ota_0 partition.
#
# If the SD card is not mounted, the script builds only and reminds you to enable USB MSC.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

if [[ $# -gt 0 ]]; then
  ansi_fail "Usage: $0   (no arguments — SD is accessed via USB MSC, not serial port)"
  echo ""
  echo "  Correct flow:"
  echo "    1. In the launcher: Settings → USB MSC"
  echo "    2. Run: $0"
  echo "    3. Eject SD, exit USB MSC in launcher"
  echo "    4. In launcher: navigate to tools/ → Install the .bin"
  exit 2
fi

ENV_NAME="${CARDPUTER_ADV_ENV:-cardputer-adv-rfid2}"
FIRMWARE="$ROOT/.pio/build/$ENV_NAME/firmware.bin"

# ---- Build ---------------------------------------------------------------
start_seconds="$SECONDS"
ansi_info "Building $ENV_NAME"
./scripts/pio_local.sh run -e "$ENV_NAME"

if [[ ! -f "$FIRMWARE" ]]; then
  ansi_fail "Build produced no firmware.bin"
  exit 1
fi

# ---- Extract version -----------------------------------------------------
FW_VERSION="$(grep -oP 'kFwVersion\[\] = "\K[^"]+' src/main.cpp 2>/dev/null || echo "unknown")"
SD_BIN_NAME="RFID2-Clone-Station-v${FW_VERSION}.bin"
ansi_ok "Build succeeded in $((SECONDS - start_seconds))s  →  $SD_BIN_NAME"

# ---- Find SD card (must be mounted via launcher USB MSC) -----------------
SD_TOOLS=""
for vol in /Volumes/*/; do
  if [[ -d "${vol}tools" ]] && [[ -d "${vol}rfid" ]]; then
    SD_TOOLS="${vol}tools"
    break
  fi
done

if [[ -z "$SD_TOOLS" ]]; then
  echo ""
  ansi_fail "SD card not mounted. Cannot deploy."
  echo ""
  echo "  Steps to deploy:"
  echo "    1. On the Cardputer: launcher → Settings → USB MSC"
  echo "    2. Run this script again"
  echo "    3. Eject SD, exit USB MSC"
  echo "    4. In launcher: tools/ → $SD_BIN_NAME → Install"
  exit 1
fi

SD_ROOT="$(dirname "$SD_TOOLS")"
ansi_info "SD card found at $SD_ROOT"

# ---- Remove old RFID2 bins, copy new one ---------------------------------
for old in "$SD_TOOLS"/RFID2*.bin; do
  [[ -f "$old" ]] && rm "$old" && ansi_info "  removed: $(basename "$old")"
done

cp "$FIRMWARE" "$SD_TOOLS/$SD_BIN_NAME"
ansi_ok "Deployed: $SD_TOOLS/$SD_BIN_NAME ($(du -sh "$SD_TOOLS/$SD_BIN_NAME" | cut -f1))"

# ---- Eject ---------------------------------------------------------------
sleep 1
if diskutil quiet eject "$SD_ROOT" 2>/dev/null; then
  ansi_ok "SD card ejected"
else
  ansi_info "Could not auto-eject — please eject manually"
fi

echo ""
ansi_info "Next: exit USB MSC in the launcher, then:"
ansi_info "  tools/ → $SD_BIN_NAME → Install"
