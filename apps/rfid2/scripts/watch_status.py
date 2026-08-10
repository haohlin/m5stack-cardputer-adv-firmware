#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
import time

import serial


def autodetect_port() -> str:
    result = subprocess.run(
        ["./scripts/select_cardputer_port.sh"],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def print_event(line: str) -> None:
    line = line.strip()
    if not line:
        return
    try:
        event = json.loads(line)
    except json.JSONDecodeError:
        print(line)
        return

    name = event.get("event", "?")
    uptime = event.get("uptime_ms", "?")
    if name == "status":
        card = event.get("last_card")
        card_text = "none"
        if card:
            card_text = f"{card.get('uid')} {card.get('type')} SAK={card.get('sak')}"
        ui = event.get("ui") or {}
        slots = event.get("slots") or []
        slot_text = ",".join(
            f"{slot.get('slot')}:v{slot.get('version')}" for slot in slots if slot.get("valid")
        ) or "empty"
        print(
            f"[{uptime}ms] status reason={event.get('reason')} "
            f"rfid_ready={event.get('rfid_ready')} i2c={event.get('i2c_scan')} "
            f"ui={ui.get('mode')}:{ui.get('slot')} armed={ui.get('armed')} "
            f"slots={slot_text} last_card={card_text}"
        )
    elif name == "card":
        print(f"[{uptime}ms] CARD uid={event.get('uid')} sak={event.get('sak')} type={event.get('type')}")
    else:
        print(f"[{uptime}ms] {name}: {event.get('message', event)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Watch Cardputer-Adv RFID2 firmware status over USB serial.")
    parser.add_argument("port", nargs="?", help="Serial port, for example /dev/cu.usbmodem2101")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("--poll", type=float, default=5.0, help="Seconds between status commands.")
    args = parser.parse_args()

    port = args.port or autodetect_port()
    print(f"Opening {port} at {args.baud}. Press Ctrl+C to stop.", file=sys.stderr)

    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        # Avoid forcing the ESP32-S3 into download mode when opening the port.
        ser.dtr = True
        ser.rts = False
        ser.reset_input_buffer()
        ser.write(b"status\n")
        ser.flush()

        last_poll = time.monotonic()
        try:
            while True:
                raw = ser.readline()
                if raw:
                    print_event(raw.decode("utf-8", errors="replace"))

                now = time.monotonic()
                if now - last_poll >= args.poll:
                    ser.write(b"status\n")
                    ser.flush()
                    last_poll = now
        except KeyboardInterrupt:
            print("\nStopped.", file=sys.stderr)
            return 0


if __name__ == "__main__":
    raise SystemExit(main())
