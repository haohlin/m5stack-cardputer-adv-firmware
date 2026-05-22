#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

usage() {
  cat >&2 <<'EOF'
Usage: ./scripts/flash_cardputer_adv_bin.sh <merged-bin> [serial-port]

Examples:
  ./scripts/flash_cardputer_adv_bin.sh release/archive/cardputer-adv-stable-latest.bin
  ./scripts/flash_cardputer_adv_bin.sh release/archive/cardputer-adv-experimental-latest.bin /dev/cu.usbmodem1101
EOF
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

BIN="$1"
if [[ ! -f "$BIN" ]]; then
  echo "Firmware binary not found: $BIN" >&2
  exit 1
fi

PORT="$(./scripts/select_cardputer_port.sh "${2:-}")"
BAUD="${ESPTOOL_BAUD:-921600}"

PYTHON_BIN="${PYTHON_BIN:-}"
if [[ -z "$PYTHON_BIN" ]]; then
  if [[ -x ".venv/bin/python" ]]; then
    PYTHON_BIN=".venv/bin/python"
  elif command -v python3.12 >/dev/null 2>&1; then
    PYTHON_BIN="$(command -v python3.12)"
  else
    PYTHON_BIN="$(command -v python3)"
  fi
fi

find_esptool() {
  local candidates
  shopt -s nullglob
  candidates=("$HOME"/.platformio/packages/tool-esptoolpy*/esptool.py)
  shopt -u nullglob

  if (( ${#candidates[@]} > 0 )); then
    echo "${candidates[0]}"
    return 0
  fi

  return 1
}

ESPTOOL="${ESPTOOL_PY:-}"
if [[ -z "$ESPTOOL" ]]; then
  ESPTOOL="$(find_esptool || true)"
fi
if [[ -z "$ESPTOOL" ]]; then
  echo "Could not find PlatformIO esptool.py under ~/.platformio/packages." >&2
  echo "Run ./scripts/build_cardputer_adv.sh once, then retry." >&2
  exit 1
fi

format_duration() {
  local total="$1"
  printf '%02d:%02d:%02d' "$((total / 3600))" "$(((total / 60) % 60))" "$((total % 60))"
}

print_footer() {
  local status="$1"
  local elapsed="$2"
  local result="SUCCESS"
  local count_line="1 succeeded"

  if [[ "$status" -ne 0 ]]; then
    result="FAILED"
    count_line="1 failed"
  fi

  local duration
  duration="$(format_duration "$elapsed")"

  printf '\n===================================================================== [%s] Took %ss =====================================================================\n\n' "$result" "$elapsed"
  printf '%s\n' 'Environment    Status    Duration'
  printf '%s\n' '-------------  --------  ------------'
  printf 'cardputer-adv  %-8s  %s\n' "$result" "$duration"
  printf '====================================================================== %s in %s ======================================================================\n' "$count_line" "$duration"
}

echo "Flashing $BIN" >&2
echo "Using Cardputer ADV port: $PORT" >&2

start_seconds="$SECONDS"
if "$PYTHON_BIN" "$ESPTOOL" \
  --chip esp32s3 \
  --port "$PORT" \
  --baud "$BAUD" \
  write_flash 0x0 "$BIN"; then
  flash_status=0
else
  flash_status=$?
fi

elapsed_seconds=$((SECONDS - start_seconds))
print_footer "$flash_status" "$elapsed_seconds"
exit "$flash_status"
