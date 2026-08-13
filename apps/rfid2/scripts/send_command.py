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


def compact_print(line: str) -> None:
    line = line.strip()
    if not line:
        return
    try:
        event = json.loads(line)
    except json.JSONDecodeError:
        print(line)
        return

    name = event.get("event", "?")
    if name == "card":
        print(f"CARD uid={event.get('uid')} sak={event.get('sak')} type={event.get('type')}")
    elif name in {"status", "store", "write"}:
        print(json.dumps(event, separators=(",", ":")))
    elif name == "dump":
        print(json.dumps(event, separators=(",", ":")))
    else:
        print(f"{name}: {event.get('message', json.dumps(event, separators=(',', ':')))}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Send one command to Cardputer-Adv RFID2 firmware.")
    parser.add_argument("command", help="status, scan, store, dump, write, clear, reset-rfid, version, help")
    parser.add_argument("port", nargs="?", help="Serial port, for example /dev/cu.usbmodem2101")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("-t", "--timeout", type=float, default=6.0)
    args = parser.parse_args()

    port = args.port or autodetect_port()
    with serial.Serial(port, args.baud, timeout=0.2) as ser:
        ser.dtr = True
        ser.rts = False
        ser.reset_input_buffer()
        ser.write((args.command.strip() + "\n").encode("utf-8"))
        ser.flush()

        deadline = time.monotonic() + args.timeout
        saw_output = False
        while time.monotonic() < deadline:
            raw = ser.readline()
            if raw:
                saw_output = True
                compact_print(raw.decode("utf-8", errors="replace"))
                if args.command in {"store", "write", "dump", "scan", "status", "clear", "version", "help", "reset-rfid"}:
                    # Keep reading briefly for related block_error lines.
                    deadline = min(deadline, time.monotonic() + 0.8)

        if not saw_output:
            print("No serial response captured.", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
