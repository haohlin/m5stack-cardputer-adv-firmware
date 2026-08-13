#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

source "$ROOT/scripts/ansi.sh"

if [[ $# -lt 1 || $# -gt 2 ]]; then
  ansi_fail "Usage: $0 <merged-bin> [serial-port]"
  exit 2
fi

ansi_warn "RECOVERY ONLY: this replaces flash regions outside Launcher OTA."
exec "$ROOT/scripts/recovery/flash_cardputer_adv_bin.sh" "$@"
