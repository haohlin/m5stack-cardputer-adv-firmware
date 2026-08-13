#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -x ".venv/bin/python" ]]; then
  ./scripts/pio_local.sh --version >/dev/null
fi

exec .venv/bin/python scripts/send_command.py "$@"
