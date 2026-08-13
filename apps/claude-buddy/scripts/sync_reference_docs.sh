#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/reference/vendor}"
WEB="$OUT/web"
REPOS="$OUT/repos"
PIO="$OUT/platformio"

mkdir -p "$WEB" "$REPOS" "$PIO"

fetch_url() {
  local name="$1"
  local url="$2"
  printf 'fetch %-42s %s\n' "$name" "$url"
  curl -fsSL --retry 3 --retry-delay 1 "$url" -o "$WEB/$name"
}

sync_repo() {
  local name="$1"
  local url="$2"
  local dir="$REPOS/$name"
  if [[ -d "$dir/.git" ]]; then
    printf 'update %-41s %s\n' "$name" "$url"
    git -C "$dir" fetch --depth 1 origin
    git -C "$dir" checkout -q FETCH_HEAD
  else
    printf 'clone  %-41s %s\n' "$name" "$url"
    git clone --depth 1 "$url" "$dir"
  fi
}

fetch_url "m5stack-cardputer-adv.html" \
  "https://docs.m5stack.com/en/core/Cardputer-Adv"
fetch_url "m5stack-cardputer-adv.pdf" \
  "https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/static/pdf/static/en/core/Cardputer-Adv.pdf"
fetch_url "m5stack-cardputer-arduino-program.html" \
  "https://docs.m5stack.com/en/arduino/m5cardputer/program"
fetch_url "m5stack-cardputer-adv-factory.html" \
  "https://docs.m5stack.com/en/guide/restore_factory/cardputer_adv"
fetch_url "espressif-esp32s3-datasheet.pdf" \
  "https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf"
fetch_url "esp-idf-esp32s3-wifi.html" \
  "https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/network/esp_wifi.html"
fetch_url "esp-idf-esp32s3-coexist.html" \
  "https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html"
fetch_url "arduino-esp32-wifi.html" \
  "https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html"

sync_repo "M5Cardputer" "https://github.com/m5stack/M5Cardputer.git"
sync_repo "M5Unified" "https://github.com/m5stack/M5Unified.git"
sync_repo "M5GFX" "https://github.com/m5stack/M5GFX.git"

if [[ -d "$ROOT/.pio/libdeps/cardputer-adv" ]]; then
  mkdir -p "$PIO/libdeps-cardputer-adv"
  find "$ROOT/.pio/libdeps/cardputer-adv" -maxdepth 2 \
    \( -name 'library.json' -o -name 'library.properties' -o -name 'README.md' \) \
    -print0 |
    while IFS= read -r -d '' file; do
      rel="${file#"$ROOT/.pio/libdeps/cardputer-adv/"}"
      mkdir -p "$PIO/libdeps-cardputer-adv/$(dirname "$rel")"
      cp "$file" "$PIO/libdeps-cardputer-adv/$rel"
    done
fi

framework="${HOME}/.platformio/packages/framework-arduinoespressif32"
if [[ -d "$framework" ]]; then
  mkdir -p "$PIO/framework-arduinoespressif32"
  cp "$framework/package.json" "$PIO/framework-arduinoespressif32/package.json" 2>/dev/null || true
  rm -rf "$PIO/framework-arduinoespressif32/WiFi-src"
  cp -R "$framework/libraries/WiFi/src" "$PIO/framework-arduinoespressif32/WiFi-src"
fi

cat >"$OUT/README.md" <<EOF
# Local Reference Cache

Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)

This directory is intentionally ignored by git. It keeps local copies of
official Cardputer ADV, M5Stack library, Arduino-ESP32, and ESP-IDF references
for firmware research/debugging.

The committed summary lives at:

- docs/reference/cardputer_adv_wifi.md
EOF

printf '\nReference cache updated: %s\n' "$OUT"
