#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="claude-buddy-v1.3.0"
HISTORICAL="e969dbfe9fd2f444c477e6df0d9bcd9c7d564ce203f80c2f4815d0e58bcb5051"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git -C "$ROOT" archive "$TAG:apps/claude-buddy" | tar -x -C "$TMP"

(
  cd "$TMP"
  ./scripts/pio_local.sh run -e cardputer-adv -t clean
  ./scripts/pio_local.sh run -e cardputer-adv
)

ACTUAL="$(shasum -a 256 "$TMP/.pio/build/cardputer-adv/firmware.bin" | awk '{print $1}')"
printf 'historical_sha256=%s\n' "$HISTORICAL"
printf 'clean_build_sha256=%s\n' "$ACTUAL"
