#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PYTHON_BIN="${PYTHON_BIN:-python3}"
VENV_PY="$ROOT/.venv/bin/python"
VENV_GRAPHIFY="$ROOT/.venv/bin/graphify"

if [[ ! -x "$VENV_PY" ]]; then
  "$PYTHON_BIN" -m venv "$ROOT/.venv"
fi

if [[ ! -x "$VENV_GRAPHIFY" ]]; then
  "$VENV_PY" -m pip install --upgrade "graphifyy>=0.8.14,<0.9"
fi

if "$VENV_GRAPHIFY" update .; then
  exit 0
fi

# Newer graphify docs describe update mode as a flag rather than a subcommand.
exec "$VENV_GRAPHIFY" . --update --no-viz

