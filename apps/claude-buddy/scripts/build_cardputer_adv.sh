#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ENV_NAME="${CARDPUTER_ADV_ENV:-cardputer-adv-launcher-ota}"
case "$ENV_NAME" in
  cardputer-adv-launcher-ota|cardputer-adv) ;;
  *)
    printf 'Unsupported CARDPUTER_ADV_ENV: %s\n' "$ENV_NAME" >&2
    exit 2
    ;;
esac

exec ./scripts/pio_local.sh run -e "$ENV_NAME" "$@"
