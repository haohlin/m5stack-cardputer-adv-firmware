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
[[ "$APP_ID" == "claude-buddy" ]]
[[ "$APP_TITLE" == "Claude Desktop Buddy" ]]
[[ "$APP_VERSION" == "1.3.0" ]]
[[ "$APP_PLATFORMIO_ENV" == "cardputer-adv-launcher-ota" ]]
[[ "$APP_FIRMWARE_REL" == ".pio/build/cardputer-adv-launcher-ota/firmware.bin" ]]
[[ "$APP_ARTIFACT_PREFIX" == "Claude-Desktop-Buddy" ]]

buddy_ini="apps/claude-buddy/platformio.ini"
[[ "$(rg -c '^\[env:' "$buddy_ini")" == "1" ]]
rg -q '^\[env:cardputer-adv-launcher-ota\]$' "$buddy_ini"
rg -q '^board_build\.partitions = ../../contracts/launcher/cardputer-adv-8mb\.csv$' "$buddy_ini"
if rg -q 'no_ota\.csv|^\[env:cardputer-adv\]$|^\[env:m5stickc-plus\]$' "$buddy_ini"; then
  echo "Current Buddy PlatformIO config exposes a legacy non-Launcher environment" >&2
  exit 1
fi

buddy_wrapper="apps/claude-buddy/scripts/build_cardputer_adv.sh"
rg -Fq 'exec ./scripts/pio_local.sh run -e cardputer-adv-launcher-ota "$@"' "$buddy_wrapper"
if wrapper_output="$(CARDPUTER_ADV_ENV=cardputer-adv "$buddy_wrapper" 2>&1)"; then
  echo "Buddy build wrapper accepted forbidden legacy environment override" >&2
  exit 1
fi
[[ "$wrapper_output" == "CARDPUTER_ADV_ENV is not supported; normal builds always use cardputer-adv-launcher-ota." ]]

legacy_ini="$(git show claude-buddy-v1.3.0:apps/claude-buddy/platformio.ini)"
rg -q '^\[env:cardputer-adv\]$' <<<"$legacy_ini"
rg -q '^board_build\.partitions = no_ota\.csv$' <<<"$legacy_ini"

for helper in \
  flash_cardputer_adv.sh \
  flash_cardputer_adv_bin.sh \
  erase_cardputer_adv.sh \
  archive_cardputer_adv_fw.sh \
  package_release.sh; do
  if [[ -e "apps/claude-buddy/scripts/$helper" ]]; then
    echo "Recovery helper remains exposed in normal scripts directory: $helper" >&2
    exit 1
  fi
  if [[ ! -x "apps/claude-buddy/scripts/recovery/$helper" ]]; then
    echo "Recovery helper is missing or not executable: $helper" >&2
    exit 1
  fi
done
[[ -f apps/claude-buddy/scripts/recovery/merge_bin.py ]]

# Discover every normal Buddy/root Markdown document. Exclude only material
# whose path explicitly identifies it as recovery or immutable history.
normal_docs=(README.md)
while IFS= read -r doc; do
  normal_docs+=("$doc")
done < <(
  rg --files docs apps/claude-buddy -g '*.md' |
    rg -v '(^|/)(docs/history|scripts/recovery)/'
)
if direct_guidance="$(rg -n -i '(\./scripts/(flash|erase)[^ ]*|flash_cardputer_adv|erase_cardputer_adv|pio run .* -t (upload|erase)|esptool .* (write_flash|erase_flash)|M5Burner|write_flash 0x0|erase_flash|download mode|normal merged firmware image|generated merged binary)' "${normal_docs[@]}")"; then
  echo "Direct-flash guidance found in normal Buddy documentation:" >&2
  printf '%s\n' "$direct_guidance" >&2
  exit 1
fi
if ./cardputer --help | rg -qi '(flash|erase|scripts/recovery)'; then
  echo "Root cardputer interface exposes recovery operations" >&2
  exit 1
fi

echo "cardputer tool static checks passed"
