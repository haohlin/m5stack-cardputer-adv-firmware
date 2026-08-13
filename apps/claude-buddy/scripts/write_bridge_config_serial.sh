#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_DIR="$ROOT/scripts"
source "$SCRIPT_DIR/ansi.sh"

PORT="$("$SCRIPT_DIR/select_cardputer_port.sh" "${1:-}")"
PY="$ROOT/.venv/bin/python"
if [[ ! -x "$PY" ]]; then
  PY="python3"
fi

"$PY" - "$PORT" <<'PY'
import json
import os
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except Exception as exc:
    print(f"[ERROR] pyserial is not available: {exc}", file=sys.stderr)
    sys.exit(1)


def default_host() -> str:
    if os.environ.get("CARDPUTER_BRIDGE_HOST"):
        return os.environ["CARDPUTER_BRIDGE_HOST"]
    for dev in ("en0", "en1"):
        try:
            value = subprocess.check_output(
                ["ipconfig", "getifaddr", dev],
                stderr=subprocess.DEVNULL,
                text=True,
            ).strip()
        except Exception:
            value = ""
        if value:
            return value
    return "127.0.0.1"


def load_token() -> str:
    if os.environ.get("CARDPUTER_BRIDGE_TOKEN"):
        return os.environ["CARDPUTER_BRIDGE_TOKEN"]
    path = Path(os.environ.get(
        "CARDPUTER_BRIDGE_CONFIG",
        str(Path.home() / ".claude-cardputer-bridge" / "config.json"),
    ))
    if not path.exists():
        print(f"[ERROR] Bridge config not found: {path}", file=sys.stderr)
        print("Start the desktop bridge once or set CARDPUTER_BRIDGE_TOKEN.", file=sys.stderr)
        sys.exit(1)
    data = json.loads(path.read_text())
    token = data.get("token", "")
    if not token:
        print(f"[ERROR] No token in bridge config: {path}", file=sys.stderr)
        sys.exit(1)
    return token


port = sys.argv[1]
host = default_host()
bridge_port = int(os.environ.get("CARDPUTER_BRIDGE_PORT", "17877"))
token = load_token()

cmd = {
    "cmd": "bridge_config",
    "v": 1,
    "type": "claude-cardputer-bridge",
    "endpoint": f"ws://{host}:{bridge_port}/device",
    "host": host,
    "port": bridge_port,
    "token": token,
}

ssid = os.environ.get("CARDPUTER_WIFI_SSID", "")
if ssid:
    cmd["wifi"] = {
        "ssid": ssid,
        "password": os.environ.get("CARDPUTER_WIFI_PASS", ""),
    }

line = (json.dumps(cmd, separators=(",", ":")) + "\n").encode()

print(f"Writing bridge config over USB serial: {port}")
print(f"Bridge host: {host}:{bridge_port}")
if ssid:
    print(f"Wi-Fi SSID: {ssid}")
    print("Token/password: configured (hidden)")
else:
    print("Wi-Fi SSID/password: omitted; enter on the device")
    print("Token: configured (hidden)")

ser = serial.Serial(port, 115200, timeout=0.2, write_timeout=1)
try:
    time.sleep(float(os.environ.get("CARDPUTER_SERIAL_BOOT_WAIT", "5")))
    ser.reset_input_buffer()
    ser.write(line)
    ser.flush()

    deadline = time.time() + 5
    buffer = b""
    while time.time() < deadline:
        chunk = ser.read(512)
        if chunk:
            buffer += chunk
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                raw = raw.strip()
                if not raw:
                    continue
                text = raw.decode("utf-8", "replace")
                try:
                    ack = json.loads(text)
                except Exception:
                    print(f"serial: {text}")
                    continue
                if ack.get("ack") == "bridge_config":
                    if ack.get("ok"):
                        print("[SUCCESS] Device saved bridge config")
                        sys.exit(0)
                    print(f"[ERROR] Device rejected bridge config: {ack.get('error', 'unknown')}", file=sys.stderr)
                    sys.exit(1)

    print("[ERROR] No bridge_config ack from device.", file=sys.stderr)
    print("Flash the latest experimental firmware; older builds did not start USB Serial.", file=sys.stderr)
    sys.exit(1)
finally:
    ser.close()
PY
