#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ARGS=(run -e cardputer-adv -t erase)
if [[ $# -gt 0 ]]; then
  ARGS+=("--upload-port" "$1")
fi

exec ./scripts/pio_local.sh "${ARGS[@]}"
