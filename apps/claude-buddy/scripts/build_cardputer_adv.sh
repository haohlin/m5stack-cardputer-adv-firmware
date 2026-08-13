#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

ansi_info "Building Cardputer ADV firmware"
start_seconds="$SECONDS"
if ./scripts/pio_local.sh run -e cardputer-adv "$@"; then
  ansi_ok "Build succeeded in $((SECONDS - start_seconds))s"
else
  status=$?
  ansi_fail "Build failed in $((SECONDS - start_seconds))s"
  exit "$status"
fi
