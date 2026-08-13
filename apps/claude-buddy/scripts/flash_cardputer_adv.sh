#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

if [[ $# -gt 1 ]]; then
  ansi_fail "Usage: $0 [serial-port]"
  exit 2
fi

PORT="$(./scripts/select_cardputer_port.sh "${1:-}")"
ansi_info "Using Cardputer ADV port: $PORT"

ARGS=(run -e cardputer-adv -t upload --upload-port "$PORT")
start_seconds="$SECONDS"
if ./scripts/pio_local.sh "${ARGS[@]}"; then
  ansi_ok "Flash succeeded in $((SECONDS - start_seconds))s"
else
  status=$?
  ansi_fail "Flash failed in $((SECONDS - start_seconds))s"
  exit "$status"
fi
