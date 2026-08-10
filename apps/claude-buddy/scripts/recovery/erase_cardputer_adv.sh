#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

if [[ $# -gt 1 ]]; then
  echo "Usage: $0 [serial-port]" >&2
  exit 2
fi

PORT="$(./scripts/select_cardputer_port.sh "${1:-}")"
echo "Using Cardputer ADV port: $PORT" >&2
echo "Recovery-only full erase; this removes Launcher and app state." >&2

ARGS=(run -e cardputer-adv -t erase --upload-port "$PORT")
exec ./scripts/pio_local.sh "${ARGS[@]}"
