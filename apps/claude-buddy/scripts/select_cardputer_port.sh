#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 [serial-port]" >&2
}

if [[ $# -gt 1 ]]; then
  usage
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

  if (( ${#ports[@]} > 0 )); then
    for existing in "${ports[@]}"; do
      [[ "$existing" == "$port" ]] && return 0
    done
  fi

  ports+=("$port")
}

collect_glob() {
  local pattern="$1"
  local port

  for port in $pattern; do
    add_port "$port"
  done
}

collect_patterns() {
  local pattern

  shopt -s nullglob
  for pattern in "$@"; do
    collect_glob "$pattern"
  done
  shopt -u nullglob
}

case "$(uname -s)" in
  Darwin)
    collect_patterns \
      "/dev/cu.usbmodem*" \
      "/dev/cu.usbserial*" \
      "/dev/cu.usb*" \
      "/dev/cu.wchusbserial*" \
      "/dev/cu.SLAB_USBtoUART*"
    ;;
  Linux)
    collect_patterns \
      "/dev/ttyACM*" \
      "/dev/ttyUSB*"
    ;;
  *)
    collect_patterns \
      "/dev/cu.usbmodem*" \
      "/dev/cu.usbserial*" \
      "/dev/cu.usb*" \
      "/dev/cu.wchusbserial*" \
      "/dev/cu.SLAB_USBtoUART*" \
      "/dev/ttyACM*" \
      "/dev/ttyUSB*"
    ;;
esac

if [[ ${#ports[@]} -eq 0 ]]; then
  cat >&2 <<'EOF'
No USB serial port was found for the Cardputer ADV.

Reconnect USB-C, then retry. You can also pass the port explicitly:

  ./cardputer debug claude-buddy serial /dev/cu.usbmodemXXXX

EOF
  exit 1
fi

if [[ ${#ports[@]} -eq 1 ]]; then
  echo "${ports[0]}"
  exit 0
fi

if [[ ! -t 0 ]]; then
  echo "Multiple USB serial ports found, but stdin is not interactive:" >&2
  printf '  %s\n' "${ports[@]}" >&2
  echo "Pass one explicitly, for example:" >&2
  echo "  ./cardputer debug claude-buddy serial ${ports[0]}" >&2
  exit 1
fi

echo "Multiple USB serial ports found:" >&2
for i in "${!ports[@]}"; do
  printf '  %d) %s\n' "$((i + 1))" "${ports[$i]}" >&2
done

while true; do
  printf 'Select upload port [1-%d]: ' "${#ports[@]}" >&2
  read -r choice

  if [[ "$choice" =~ ^[0-9]+$ ]] &&
     (( choice >= 1 && choice <= ${#ports[@]} )); then
    echo "${ports[$((choice - 1))]}"
    exit 0
  fi

  echo "Invalid selection: $choice" >&2
done
