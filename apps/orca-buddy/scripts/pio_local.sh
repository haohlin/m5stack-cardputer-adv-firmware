#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ -x ".venv/bin/pio" ]]; then
  exec .venv/bin/pio "$@"
fi

if command -v pio >/dev/null 2>&1; then
  exec pio "$@"
fi

PYTHON_BIN="${PYTHON_BIN:-}"
if [[ -z "$PYTHON_BIN" ]]; then
  if command -v python3.12 >/dev/null 2>&1; then
    PYTHON_BIN="$(command -v python3.12)"
  else
    PYTHON_BIN="$(command -v python3)"
  fi
fi

"$PYTHON_BIN" -m venv .venv
.venv/bin/python -m pip install --upgrade pip platformio
exec .venv/bin/pio "$@"
