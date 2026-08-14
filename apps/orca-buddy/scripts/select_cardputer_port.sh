#!/usr/bin/env bash
set -euo pipefail

declare -a ports=()

collect_port_candidates() {
  local candidate existing
  for candidate in "$@"; do
    [[ -e "$candidate" ]] || continue
    for existing in "${ports[@]-}"; do
      [[ "$existing" == "$candidate" ]] && continue 2
    done
    ports+=("$candidate")
  done
}

main() {
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

  local pattern port
  ports=()
  case "$(uname -s)" in
    Darwin) patterns=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.usb*) ;;
    Linux) patterns=(/dev/ttyACM* /dev/ttyUSB*) ;;
    *) patterns=(/dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyACM* /dev/ttyUSB*) ;;
  esac

  shopt -s nullglob
  for pattern in "${patterns[@]}"; do
    for port in $pattern; do
      collect_port_candidates "$port"
    done
  done
  shopt -u nullglob

  if [[ ${#ports[@]} -ne 1 ]]; then
    printf 'Pass one Cardputer ADV USB serial port explicitly.\n' >&2
    exit 1
  fi

  printf '%s\n' "${ports[0]}"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
