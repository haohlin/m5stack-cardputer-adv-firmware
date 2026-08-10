#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

ENV_NAME="${CARDPUTER_ADV_ENV:-cardputer-adv-launcher-ota}"
exec ./scripts/pio_local.sh run -e "$ENV_NAME" "$@"
