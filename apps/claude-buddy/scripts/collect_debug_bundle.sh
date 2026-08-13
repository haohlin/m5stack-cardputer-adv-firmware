#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$ROOT/scripts"
source "$SCRIPT_DIR/ansi.sh"

STAMP="$(date -u +%Y%m%d-%H%M%S)"
OUT="${1:-$ROOT/release/debug/$STAMP}"
mkdir -p "$OUT"

PY="$ROOT/.venv/bin/python"
if [[ ! -x "$PY" ]]; then
  PY="python3"
fi

run_capture() {
  local name="$1"
  shift
  {
    printf '$'
    printf ' %q' "$@"
    printf '\n\n'
    "$@"
  } >"$OUT/$name" 2>&1 || true
}

redact_json_file() {
  local src="$1"
  local dst="$2"
  "$PY" - "$src" "$dst" <<'PY' || true
import json
import pathlib
import sys

src = pathlib.Path(sys.argv[1])
dst = pathlib.Path(sys.argv[2])
if not src.exists():
    dst.write_text("not found\n")
    raise SystemExit

data = json.loads(src.read_text())

def redact(obj):
    if isinstance(obj, dict):
        out = {}
        for key, value in obj.items():
            if key.lower() in {"token", "password", "pass", "secret", "key"}:
                out[key] = "<redacted>"
            else:
                out[key] = redact(value)
        return out
    if isinstance(obj, list):
        return [redact(v) for v in obj]
    return obj

dst.write_text(json.dumps(redact(data), indent=2) + "\n")
PY
}

collect_device_status() {
  local port="$1"
  "$PY" - "$port" <<'PY'
import json
import sys
import time

try:
    import serial
except Exception as exc:
    print(f"pyserial unavailable: {exc}")
    raise SystemExit(0)

port = sys.argv[1]
for attempt in range(1, 4):
    try:
        ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=1)
    except Exception as exc:
        print(f"open failed: {exc}")
        raise SystemExit(0)

    try:
        time.sleep(5)
        ser.reset_input_buffer()
        ser.write(b'{"cmd":"status"}\n')
        ser.flush()
        deadline = time.time() + 5
        buf = b""
        while time.time() < deadline:
            chunk = ser.read(512)
            if chunk:
                buf += chunk
                if b"\n" in buf:
                    break
        text = buf.decode("utf-8", "replace").strip()
        if text:
            try:
                print(json.dumps(json.loads(text), indent=2))
            except Exception:
                print(text)
            raise SystemExit(0)
    finally:
        ser.close()

    time.sleep(1)

print("no status ack after 3 attempts")
PY
}

ansi_info "Collecting debug bundle: $OUT"

run_capture git-status git -C "$ROOT" status --short --branch
run_capture git-head git -C "$ROOT" rev-parse HEAD
run_capture archive-manifest cat "$ROOT/release/archive/MANIFEST.txt"
run_capture usb-ports bash -lc 'ls -1 /dev/cu.usb* /dev/tty.usb* 2>/dev/null || true'

if [[ -f "$HOME/.claude-cardputer-bridge/config.json" ]]; then
  redact_json_file "$HOME/.claude-cardputer-bridge/config.json" "$OUT/bridge-config-home.redacted.json"
fi

if [[ -f "$ROOT/release/bridge-config/bridge.json" ]]; then
  redact_json_file "$ROOT/release/bridge-config/bridge.json" "$OUT/bridge-config-folder.redacted.json"
fi

if port="$("$SCRIPT_DIR/select_cardputer_port.sh" 2>/dev/null)"; then
  printf '%s\n' "$port" >"$OUT/device-port.txt"
  if [[ "${CARDPUTER_DEBUG_SERIAL:-0}" == "1" ]]; then
    collect_device_status "$port" >"$OUT/device-status.json" 2>&1 || true
  else
    {
      printf 'skipped\n'
      printf 'Set CARDPUTER_DEBUG_SERIAL=1 to query {"cmd":"status"} over USB CDC.\n'
      printf 'On Cardputer ADV, opening USB CDC can reset/re-enumerate the board, so this is opt-in.\n'
    } >"$OUT/device-status.json"
  fi
else
  printf 'no port detected\n' >"$OUT/device-port.txt"
  printf 'no port detected\n' >"$OUT/device-status.json"
fi

if command -v curl >/dev/null 2>&1; then
  run_capture bridge-health curl -fsS http://127.0.0.1:17877/health
fi

claude_log="$HOME/Library/Logs/Claude/main.log"
if [[ -f "$claude_log" ]]; then
  {
    printf 'Recent Claude Desktop buddy-related log lines from %s\n\n' "$claude_log"
    rg -n "buddy|buddy-ble|char_end|bridge-config|Hardware Buddy" "$claude_log" |
      tail -n 200
  } >"$OUT/claude-buddy-log.txt" 2>&1 || true
fi

cat >"$OUT/README.md" <<EOF
# Cardputer ADV Debug Bundle

Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)

Files:

- \`git-status\`: current branch and dirty state
- \`git-head\`: current commit
- \`archive-manifest\`: stable/experimental firmware archive aliases
- \`usb-ports\`: visible USB serial devices
- \`device-port.txt\`: selected Cardputer USB serial port, if detected
- \`device-status.json\`: skipped by default; set \`CARDPUTER_DEBUG_SERIAL=1\`
  to query device \`{"cmd":"status"}\` over USB CDC
- \`bridge-config-*.redacted.json\`: redacted bridge config
- \`bridge-health\`: local bridge health endpoint, if running
- \`claude-buddy-log.txt\`: filtered Claude Desktop buddy log lines

Secrets are redacted from JSON files by key name. Review before sharing.
EOF

ansi_ok "Debug bundle written: $OUT"
