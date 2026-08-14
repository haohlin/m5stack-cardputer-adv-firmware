#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 ]]; then
  printf 'Usage: %s [serial-port]\n' "$0" >&2
  exit 2
fi

if [[ -n "${1:-}" ]]; then
  printf '%s\n' "$1"
  exit 0
fi

if [[ -n "${CARDPUTER_ADV_PORT:-}" ]]; then
  printf '%s\n' "$CARDPUTER_ADV_PORT"
  exit 0
fi

ports=()
case "$(uname -s)" in
  Darwin) patterns=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.usb*) ;;
  Linux) patterns=(/dev/ttyACM* /dev/ttyUSB*) ;;
  *) patterns=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyACM* /dev/ttyUSB*) ;;
esac

shopt -s nullglob
for pattern in "${patterns[@]}"; do
  for port in $pattern; do
    ports+=("$port")
  done
done
shopt -u nullglob

if [[ ${#ports[@]} -ne 1 ]]; then
  printf 'Pass one Cardputer ADV USB serial port explicitly.\n' >&2
  exit 1
fi

printf '%s\n' "${ports[0]}"
