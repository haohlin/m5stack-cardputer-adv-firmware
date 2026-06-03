#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

ENV_NAME="${CARDPUTER_ADV_ENV:-cardputer-adv-rfid2}"
ansi_info "Building Cardputer ADV environment: $ENV_NAME"
start_seconds="$SECONDS"
if ./scripts/pio_local.sh run -e "$ENV_NAME" "$@"; then
  ansi_ok "Build succeeded in $((SECONDS - start_seconds))s"
else
  status=$?
  ansi_fail "Build failed in $((SECONDS - start_seconds))s"
  exit "$status"
fi
