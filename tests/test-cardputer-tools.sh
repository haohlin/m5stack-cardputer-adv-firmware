#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

bash -n cardputer tools/cardputer/common.sh
./cardputer apps | rg -qx 'rfid2       RFID2 Clone Station'
./cardputer apps | rg -qx 'claude-buddy Claude Desktop Buddy'
./cardputer install-help | rg -q 'Launcher Settings -> USB MSC'

source tools/cardputer/common.sh
load_contract
[[ "$LAUNCHER_OTA_SIZE" == "0x4F0000" ]]
load_app rfid2
[[ "$APP_PLATFORMIO_ENV" == "cardputer-adv-rfid2" ]]
load_app claude-buddy
[[ "$APP_PLATFORMIO_ENV" == "cardputer-adv-launcher-ota" ]]

echo "cardputer tool static checks passed"
