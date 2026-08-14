#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PAYLOAD="${1:-}"
PORT="${2:-}"

if [[ -z "$PAYLOAD" ]]; then
  echo "Usage: $0 <protected-orca-pair-file> [serial-port]" >&2
  exit 2
fi

if [[ -z "$PORT" ]]; then
  PORT="$("$ROOT/scripts/select_cardputer_port.sh")"
fi

PYTHON="$ROOT/.venv/bin/python"
if [[ ! -x "$PYTHON" ]]; then
  PYTHON="python3"
fi

exec "$PYTHON" "$ROOT/scripts/serial_pairing.py" "$PAYLOAD" "$PORT"
