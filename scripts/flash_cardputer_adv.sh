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

if [[ ! -f "$ESPTOOL" ]]; then
  ansi_fail "Could not find PlatformIO esptool.py at $ESPTOOL"
  exit 1
fi

start_seconds="$SECONDS"
ansi_info "Building $ENV_NAME before flash"
./scripts/pio_local.sh run -e "$ENV_NAME"

for image in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE" "$BOOT_APP0"; do
  if [[ ! -f "$image" ]]; then
    ansi_fail "Missing flash image: $image"
    exit 1
  fi
done

ansi_info "Writing flash at 115200 baud; watchdog reset will start the app"
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
  ansi_ok "Flash succeeded in $((SECONDS - start_seconds))s"
else
  status=$?
  ansi_fail "Flash failed in $((SECONDS - start_seconds))s"
  exit "$status"
fi
