#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/release/bridge-config}"
BRIDGE_CONFIG="${CARDPUTER_BRIDGE_CONFIG:-$HOME/.claude-cardputer-bridge/config.json}"

ensure_bridge_config() {
  if [[ -f "$BRIDGE_CONFIG" ]]; then
    return
  fi
  (
    cd "$ROOT/desktop-bridge"
    npm run build >/dev/null
    node dist/index.js --print-config >/dev/null
  )
}

json_escape() {
  node -e 'process.stdout.write(JSON.stringify(String(process.argv[1] || "")).slice(1, -1))' "$1"
}

default_host() {
  if [[ -n "${CARDPUTER_BRIDGE_HOST:-}" ]]; then
    printf '%s\n' "$CARDPUTER_BRIDGE_HOST"
    return
  fi
  for dev in en0 en1; do
    ip="$(ipconfig getifaddr "$dev" 2>/dev/null || true)"
    if [[ -n "$ip" ]]; then
      printf '%s\n' "$ip"
      return
    fi
  done
  printf '127.0.0.1\n'
}

default_ssid() {
  if [[ -n "${CARDPUTER_WIFI_SSID:-}" ]]; then
    printf '%s\n' "$CARDPUTER_WIFI_SSID"
    return
  fi

  local dev raw ssid
  while IFS= read -r dev; do
    [[ -n "$dev" ]] || continue
    raw="$(networksetup -getairportnetwork "$dev" 2>/dev/null || true)"
    case "$raw" in
      "Current Wi-Fi Network: "*)
        ssid="${raw#Current Wi-Fi Network: }"
        if [[ -n "$ssid" && "$ssid" != "You are not associated with an AirPort network." ]]; then
          printf '%s\n' "$ssid"
          return
        fi
        ;;
    esac
  done < <(
    {
      networksetup -listallhardwareports 2>/dev/null |
        awk '/Hardware Port: Wi-Fi/{getline; if ($1 == "Device:") print $2}'
      printf '%s\n' en0 en1
    } | awk 'NF && !seen[$0]++'
  )
}

ensure_bridge_config

TOKEN="${CARDPUTER_BRIDGE_TOKEN:-$(node -e 'const fs=require("fs"); const p=process.env.CARDPUTER_BRIDGE_CONFIG || `${process.env.HOME}/.claude-cardputer-bridge/config.json`; process.stdout.write(JSON.parse(fs.readFileSync(p, "utf8")).token || "");')}"
HOOK_PORT="${CARDPUTER_BRIDGE_PORT:-17877}"
PORT="${CARDPUTER_BRIDGE_DEVICE_PORT:-$((HOOK_PORT + 1))}"
CA_FILE="${CARDPUTER_BRIDGE_TLS_CA:-}"
HOST="$(default_host)"
WIFI_ON_DEVICE="${CARDPUTER_WIFI_ON_DEVICE:-}"
if [[ "$WIFI_ON_DEVICE" == "1" ]]; then
  SSID=""
  PASS=""
else
  SSID="$(default_ssid)"
  PASS="${CARDPUTER_WIFI_PASS:-}"
fi

if ! printf '%s' "$TOKEN" | python3 "$ROOT/scripts/bridge_token.py"; then
  echo "Bridge token must be 32 URL-safe characters provisioned from a CSPRNG." >&2
  exit 1
fi
if [[ -z "$CA_FILE" || ! -f "$CA_FILE" ]]; then
  echo "Set CARDPUTER_BRIDGE_TLS_CA to public PEM CA file used by desktop TLS listener." >&2
  exit 1
fi
CA="$(<"$CA_FILE")"
if [[ "$CA" != *"-----BEGIN CERTIFICATE-----"* || "$CA" != *"-----END CERTIFICATE-----"* ]]; then
  echo "CARDPUTER_BRIDGE_TLS_CA must contain PEM certificate material." >&2
  exit 1
fi

if [[ "$WIFI_ON_DEVICE" != "1" && -z "$SSID" ]]; then
  echo "Could not auto-detect Wi-Fi SSID. Hotspot/tethering can hide it from macOS." >&2
  echo "Set CARDPUTER_WIFI_SSID, enter it below, or leave blank to type Wi-Fi on the device." >&2
  read -r -p "Wi-Fi SSID: " SSID
fi

if [[ -n "$SSID" && -z "$PASS" ]]; then
  read -r -s -p "Wi-Fi password for $SSID: " PASS
  printf '\n' >&2
fi

if [[ -e "$OUT" ]]; then
  echo "Refusing to overwrite existing bridge config folder: $OUT" >&2
  exit 1
fi
mkdir -p "$OUT"

cat > "$OUT/manifest.json" <<EOF
{
  "name": "bridge-config",
  "type": "claude-cardputer-bridge",
  "version": 1
}
EOF

if [[ -n "$SSID" ]]; then
  wifi_json=$(cat <<EOF
,
  "wifi": {
    "ssid": "$(json_escape "$SSID")",
    "password": "$(json_escape "$PASS")"
  }
EOF
)
else
  wifi_json=""
fi

cat > "$OUT/bridge.json" <<EOF
{
  "v": 1,
  "type": "claude-cardputer-bridge",
  "endpoint": "wss://$(json_escape "$HOST"):$PORT/device",
  "token": "$(json_escape "$TOKEN")",
  "ca": "$(json_escape "$CA")"$wifi_json
}
EOF

chmod 600 "$OUT/bridge.json"
echo "Wrote bridge config folder: $OUT"
echo "Secure bridge host: $HOST:$PORT"
if [[ -n "$SSID" ]]; then
  echo "Wi-Fi SSID: $SSID"
  echo "Token/password: configured (hidden)"
else
  echo "Wi-Fi SSID/password: omitted; enter on the device"
  echo "Token: configured (hidden)"
fi
echo "Drop this folder onto Claude Desktop Hardware Buddy to save it on the device."
