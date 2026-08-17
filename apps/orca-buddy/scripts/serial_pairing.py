#!/usr/bin/env python3
"""Send one protected Orca Buddy pairing payload without printing its secret."""

import argparse
import os
import stat
import sys
import time
from pathlib import Path

MAX_PAYLOAD_BYTES = 12_288
SUCCESS = "OK secure pairing saved"
USB_CDC_SETTLE_SECONDS = 0.5
USB_CDC_RESYNC_SECONDS = 0.2
USB_CDC_FRAME_BYTES = 16
USB_CDC_FRAME_DELAY_SECONDS = 0.04
USB_CDC_REOPEN_ATTEMPTS = 3
USB_CDC_REOPEN_DELAY_SECONDS = 0.5


def read_provisioning_payload(path: Path) -> bytes:
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or not stat.S_ISREG(info.st_mode):
        raise ValueError("provisioning payload must be one regular non-symlink file")
    if info.st_mode & 0o077:
        raise ValueError("provisioning payload must have mode 0600")
    if info.st_size == 0 or info.st_size > MAX_PAYLOAD_BYTES:
        raise ValueError("provisioning payload is too large or empty")
    payload = path.read_bytes()
    try:
        text = payload.decode("utf-8", "strict")
    except UnicodeDecodeError as error:
        raise ValueError("provisioning payload must be UTF-8") from error
    if not text.startswith("orca-pair "):
        raise ValueError("provisioning payload must start with orca-pair")
    if not text.endswith("\n") or text.count("\n") != 1 or "\r" in text:
        raise ValueError("provisioning payload must contain one complete line")
    return payload


def is_transient_usb_reset(error: Exception) -> bool:
    message = str(error).lower()
    return "device not configured" in message or "no such device" in message


def send_pairing(port: str, payload: bytes, timeout_seconds: float = 8.0) -> None:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError("pyserial is required; run the app's PlatformIO setup first") from error

    for attempt in range(USB_CDC_REOPEN_ATTEMPTS):
        serial_port = None
        try:
            serial_port = serial.Serial(port, 115200, timeout=0.2, write_timeout=1)
            # ESP32-S3 USB CDC may reset/re-enumerate immediately after opening.
            # A newline also clears an incomplete command from a prior attempt.
            time.sleep(USB_CDC_SETTLE_SECONDS)
            serial_port.reset_input_buffer()
            serial_port.write(b"\n")
            serial_port.flush()
            time.sleep(USB_CDC_RESYNC_SECONDS)
            serial_port.reset_input_buffer()
            for offset in range(0, len(payload), USB_CDC_FRAME_BYTES):
                frame = payload[offset:offset + USB_CDC_FRAME_BYTES]
                if serial_port.write(frame) != len(frame):
                    raise RuntimeError("USB serial write was incomplete")
                serial_port.flush()
                time.sleep(USB_CDC_FRAME_DELAY_SECONDS)
            deadline = time.monotonic() + timeout_seconds
            buffered = b""
            while time.monotonic() < deadline:
                buffered += serial_port.read(256)
                while b"\n" in buffered:
                    line, buffered = buffered.split(b"\n", 1)
                    message = line.decode("utf-8", "replace").strip()
                    if message == SUCCESS:
                        return
                    if message.startswith("ERR"):
                        raise RuntimeError(f"device rejected pairing: {message}")
            raise RuntimeError("device did not acknowledge pairing within 8 seconds")
        except serial.SerialException as error:
            if not is_transient_usb_reset(error) or attempt + 1 == USB_CDC_REOPEN_ATTEMPTS:
                raise RuntimeError("USB serial device reset during pairing and did not reconnect") from error
            time.sleep(USB_CDC_REOPEN_DELAY_SECONDS)
        finally:
            if serial_port is not None:
                serial_port.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send one protected Orca Buddy pairing payload over USB serial."
    )
    parser.add_argument("payload", type=Path, help="mode-0600 file from orca-cardputer provision")
    parser.add_argument("port", help="Cardputer ADV USB serial device path")
    args = parser.parse_args()
    try:
        payload = read_provisioning_payload(args.payload)
        print(f"Sending protected pairing payload to {args.port}; secret remains hidden.")
        send_pairing(args.port, payload)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"pairing: {error}", file=sys.stderr)
        return 1
    print("Pairing saved by device.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
