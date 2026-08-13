#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/ansi.sh"

if [[ $# -gt 1 ]]; then
  ansi_fail "Usage: $0 [serial-port]"
  exit 2
fi

if [[ $# -eq 1 && -n "${1:-}" ]]; then
  echo "$1"
  exit 0
fi

if [[ -n "${CARDPUTER_ADV_PORT:-}" ]]; then
  echo "$CARDPUTER_ADV_PORT"
  exit 0
fi

ports=()

add_port() {
  local port="$1"
  local existing
  [[ -e "$port" ]] || return 0
  for existing in "${ports[@]}"; do
    [[ "$existing" == "$port" ]] && return 0
  done
  ports+=("$port")
}

collect_patterns() {
  local pattern port
  shopt -s nullglob
  for pattern in "$@"; do
    for port in $pattern; do
      add_port "$port"
    done
  done
  shopt -u nullglob
}

case "$(uname -s)" in
  Darwin)
    collect_patterns "/dev/cu.usbmodem*" "/dev/cu.usbserial*" "/dev/cu.usb*" "/dev/cu.wchusbserial*" "/dev/cu.SLAB_USBtoUART*"
    ;;
  Linux)
    collect_patterns "/dev/ttyACM*" "/dev/ttyUSB*"
    ;;
  *)
    collect_patterns "/dev/cu.usbmodem*" "/dev/cu.usbserial*" "/dev/cu.usb*" "/dev/ttyACM*" "/dev/ttyUSB*"
    ;;
esac

if [[ ${#ports[@]} -eq 0 ]]; then
  ansi_fail "No USB serial port was found for the Cardputer ADV."
  exit 1
fi

if [[ ${#ports[@]} -eq 1 ]]; then
  echo "${ports[0]}"
  exit 0
fi

if [[ ! -t 0 ]]; then
  ansi_fail "Multiple USB serial ports found. Pass one explicitly."
  printf '  %s\n' "${ports[@]}" >&2
  exit 1
fi

ansi_info "Multiple USB serial ports found:"
for i in "${!ports[@]}"; do
  printf '  %d) %s\n' "$((i + 1))" "${ports[$i]}" >&2
done

while true; do
  printf 'Select upload port [1-%d]: ' "${#ports[@]}" >&2
  read -r choice
  if [[ "$choice" =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#ports[@]} )); then
    echo "${ports[$((choice - 1))]}"
    exit 0
  fi
  ansi_fail "Invalid selection: $choice"
done
