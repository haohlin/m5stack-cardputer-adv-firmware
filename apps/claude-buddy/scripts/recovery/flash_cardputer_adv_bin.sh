#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

usage() {
  cat >&2 <<'EOF'
RECOVERY ONLY: writes a merged full-flash image outside Launcher OTA.

Usage: ./scripts/recovery/flash_cardputer_adv_bin.sh [--wipe-nvs] <merged-bin> [serial-port]

Examples:
  ./scripts/recovery/flash_cardputer_adv_bin.sh release/archive/cardputer-adv-stable-latest.bin
  ./scripts/recovery/flash_cardputer_adv_bin.sh release/archive/cardputer-adv-experimental-latest.bin /dev/cu.usbmodem1101
  ./scripts/recovery/flash_cardputer_adv_bin.sh --wipe-nvs release/archive/cardputer-adv-experimental-latest.bin

By default this preserves the NVS partition so BLE bonds, brightness, sound,
Wi-Fi bridge settings, and pet settings survive reflashing a merged image.
Use --wipe-nvs for a clean flash that intentionally requires BLE re-pairing.
EOF
}

PRESERVE_NVS=1
if [[ "${1:-}" == "--wipe-nvs" ]]; then
  PRESERVE_NVS=0
  shift
elif [[ "${CARDPUTER_PRESERVE_NVS:-1}" == "0" ]]; then
  PRESERVE_NVS=0
fi

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

BIN="$1"
if [[ ! -f "$BIN" ]]; then
  ansi_fail "Firmware binary not found: $BIN"
  exit 1
fi

PORT="$(./scripts/select_cardputer_port.sh "${2:-}")"
BAUD="${ESPTOOL_BAUD:-921600}"
NVS_OFFSET="${CARDPUTER_NVS_OFFSET:-0x9000}"
NVS_SIZE="${CARDPUTER_NVS_SIZE:-0x5000}"
NVS_BACKUP=""
TMPDIR_FLASH=""

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
  ansi_fail "Could not find PlatformIO esptool.py under ~/.platformio/packages."
  ansi_info "Install PlatformIO through a normal root ./cardputer build claude-buddy first."
  exit 1
fi

cleanup() {
  if [[ -n "$TMPDIR_FLASH" ]]; then
    rm -rf "$TMPDIR_FLASH"
  fi
}
trap cleanup EXIT

format_duration() {
  local total="$1"
  printf '%02d:%02d:%02d' "$((total / 3600))" "$(((total / 60) % 60))" "$((total % 60))"
}

print_footer() {
  local status="$1"
  local elapsed="$2"
  local result="SUCCESS"
  local result_colored="${ANSI_GREEN}${ANSI_BOLD}SUCCESS${ANSI_RESET}"
  local count_line="1 succeeded"

  if [[ "$status" -ne 0 ]]; then
    result="FAILED"
    result_colored="${ANSI_RED}${ANSI_BOLD}FAILED${ANSI_RESET}"
    count_line="1 failed"
  fi

  local duration
  duration="$(format_duration "$elapsed")"

  printf '\n===================================================================== [%s] Took %ss =====================================================================\n\n' "$result_colored" "$elapsed"
  printf '%s\n' 'Environment    Status    Duration'
  printf '%s\n' '-------------  --------  ------------'
  printf 'cardputer-adv  %-8s  %s\n' "$result" "$duration"
  printf '====================================================================== %s in %s ======================================================================\n' "$count_line" "$duration"
}

ansi_warn "RECOVERY ONLY: this may replace Launcher and other flash regions."
ansi_info "Flashing $BIN"
ansi_info "Using Cardputer ADV port: $PORT"
start_seconds="$SECONDS"
if [[ "$PRESERVE_NVS" -eq 1 ]]; then
  TMPDIR_FLASH="$(mktemp -d)"
  NVS_BACKUP="$TMPDIR_FLASH/nvs.bin"
  ansi_info "Saving NVS before flash ($NVS_OFFSET+$NVS_SIZE)"
  if ! "$PYTHON_BIN" "$ESPTOOL" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud "$BAUD" \
    read_flash "$NVS_OFFSET" "$NVS_SIZE" "$NVS_BACKUP"; then
    ansi_fail "Could not back up NVS; aborting to avoid losing BLE pairing."
    ansi_info "Retry with --wipe-nvs only if you intentionally want a clean re-pair."
    exit 1
  fi
else
  ansi_warn "NVS preservation disabled; BLE and stored settings may be erased."
fi

if "$PYTHON_BIN" "$ESPTOOL" \
  --chip esp32s3 \
  --port "$PORT" \
  --baud "$BAUD" \
  write_flash 0x0 "$BIN"; then
  flash_status=0
else
  flash_status=$?
fi

if [[ "$flash_status" -eq 0 && "$PRESERVE_NVS" -eq 1 ]]; then
  ansi_info "Restoring NVS after flash"
  if "$PYTHON_BIN" "$ESPTOOL" \
    --chip esp32s3 \
    --port "$PORT" \
    --baud "$BAUD" \
    write_flash "$NVS_OFFSET" "$NVS_BACKUP"; then
    flash_status=0
  else
    flash_status=$?
  fi
fi

elapsed_seconds=$((SECONDS - start_seconds))
print_footer "$flash_status" "$elapsed_seconds"
exit "$flash_status"
