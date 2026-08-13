#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ $# -gt 1 ]]; then
  echo "Usage: $0 [serial-port]" >&2
  exit 2
fi

PORT="$(./scripts/select_cardputer_port.sh "${1:-}")"
exec ./scripts/pio_local.sh device monitor -p "$PORT" -b 115200
