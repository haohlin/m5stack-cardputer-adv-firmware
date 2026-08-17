#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$("$ROOT/scripts/select_cardputer_port.sh")"
fi

PYTHON="$ROOT/.venv/bin/python"
if [[ ! -x "$PYTHON" ]]; then
  PYTHON="python3"
fi

exec "$PYTHON" "$ROOT/scripts/serial_status.py" "$PORT"
