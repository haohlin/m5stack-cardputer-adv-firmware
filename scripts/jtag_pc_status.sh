#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

if [[ $# -gt 1 ]]; then
  ansi_fail "Usage: $0 [platformio-env]"
  exit 2
fi

ENV_NAME="${1:-${CARDPUTER_ADV_ENV:-cardputer-adv-rfid2}}"
OPENOCD="${OPENOCD:-$HOME/.platformio/packages/tool-openocd-esp32/bin/openocd}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-$HOME/.platformio/packages/tool-openocd-esp32/share/openocd/scripts}"
ADDR2LINE="${ADDR2LINE:-$HOME/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line}"
ELF="$ROOT/.pio/build/$ENV_NAME/firmware.elf"

if [[ ! -x "$OPENOCD" ]]; then
  ansi_fail "OpenOCD not found: $OPENOCD"
  echo "Install it with: ./scripts/pio_local.sh pkg install --global --tool platformio/tool-openocd-esp32" >&2
  exit 1
fi

if [[ ! -f "$ELF" ]]; then
  ansi_fail "Firmware ELF not found for env '$ENV_NAME': $ELF"
  echo "Build first: CARDPUTER_ADV_ENV=$ENV_NAME ./scripts/build_cardputer_adv.sh" >&2
  exit 1
fi

tmp="$(mktemp)"
cleanup() {
  rm -f "$tmp"
}
trap cleanup EXIT

set +e
"$OPENOCD" \
  -s "$OPENOCD_SCRIPTS" \
  -c "set ESP32_S3_ONLYCPU 1" \
  -f board/esp32s3-builtin.cfg \
  -c "init; halt; reg pc; resume; shutdown" >"$tmp" 2>&1
status=$?
set -e

if [[ $status -ne 0 ]]; then
  ansi_fail "OpenOCD status probe failed"
  sed -n '1,220p' "$tmp" >&2
  exit "$status"
fi

pc="$(
  awk '
    /pc \(\/32\):/ { print $NF; exit }
    /PC=0x[0-9a-fA-F]+/ {
      if (match($0, /PC=0x[0-9a-fA-F]+/)) {
        print substr($0, RSTART + 3, RLENGTH - 3);
        exit;
      }
    }
  ' "$tmp"
)"

if [[ -z "$pc" ]]; then
  ansi_fail "OpenOCD succeeded but no PC register was found"
  sed -n '1,220p' "$tmp" >&2
  exit 1
fi

echo "env=$ENV_NAME"
echo "pc=$pc"

if [[ "$pc" == 0x420* ]]; then
  echo "region=app-flash"
elif [[ "$pc" == 0x400* || "$pc" == 0x403* ]]; then
  echo "region=rom-or-iram"
else
  echo "region=unknown"
fi

if [[ -x "$ADDR2LINE" ]]; then
  "$ADDR2LINE" -pfiaC -e "$ELF" "$pc" | sed 's/^/symbol=/'
else
  ansi_fail "addr2line not found: $ADDR2LINE"
fi
