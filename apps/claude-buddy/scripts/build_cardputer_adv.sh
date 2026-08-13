#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ -n "${CARDPUTER_ADV_ENV:-}" ]]; then
  printf 'CARDPUTER_ADV_ENV is not supported; normal builds always use cardputer-adv-launcher-ota.\n' >&2
  exit 2
fi

exec ./scripts/pio_local.sh run -e cardputer-adv-launcher-ota "$@"
