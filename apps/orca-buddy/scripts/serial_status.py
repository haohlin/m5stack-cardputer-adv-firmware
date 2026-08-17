#!/usr/bin/env python3
"""Read a fixed, secret-free Orca Buddy diagnostic line over USB CDC."""

import argparse
import re
import sys
import time

from serial_pairing import (
    USB_CDC_REOPEN_ATTEMPTS,
    USB_CDC_REOPEN_DELAY_SECONDS,
    USB_CDC_SETTLE_SECONDS,
    is_transient_usb_reset,
)


STATUS_PATTERN = re.compile(
    rb"^STATUS version=([0-9]+\.[0-9]+\.[0-9]+) "
    rb"saved_wifi=(yes|no) wifi=(connected|disconnected) "
    rb"saved_pairing=(yes|no) bridge=(connected|disconnected) "
    rb"pairing_store=(nvs|fallback)$"
)
MAX_STATUS_LINE_BYTES = 192


def parse_device_status(line: bytes) -> str:
    match = STATUS_PATTERN.fullmatch(line.rstrip(b"\r\n"))
    if match is None:
        raise ValueError("invalid device status")
    return " ".join(
        f"{name}={value.decode('ascii')}"
        for name, value in zip(
            ("version", "saved_wifi", "wifi", "saved_pairing", "bridge", "pairing_store"),
            match.groups(),
        )
    )


def read_device_status(port: str, timeout_seconds: float = 4.0) -> str:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError("pyserial is required; run the app's PlatformIO setup first") from error

    for attempt in range(USB_CDC_REOPEN_ATTEMPTS):
        serial_port = None
        try:
            serial_port = serial.Serial(port, 115200, timeout=0.2, write_timeout=1)
            time.sleep(USB_CDC_SETTLE_SECONDS)
            serial_port.reset_input_buffer()
            if serial_port.write(b"status\n") != len(b"status\n"):
                raise RuntimeError("USB serial write was incomplete")
            serial_port.flush()
            deadline = time.monotonic() + timeout_seconds
            buffered = b""
            while time.monotonic() < deadline:
                buffered += serial_port.read(128)
                if len(buffered) > MAX_STATUS_LINE_BYTES:
                    raise RuntimeError("device status response was too large")
                while b"\n" in buffered:
                    line, buffered = buffered.split(b"\n", 1)
                    if line.startswith(b"ERR"):
                        raise RuntimeError("device rejected USB status query")
                    if line.startswith(b"STATUS "):
                        return parse_device_status(line)
            raise RuntimeError("device did not return USB status within 4 seconds")
        except serial.SerialException as error:
            if not is_transient_usb_reset(error) or attempt + 1 == USB_CDC_REOPEN_ATTEMPTS:
                raise RuntimeError("USB serial device reset during status query and did not reconnect") from error
            time.sleep(USB_CDC_REOPEN_DELAY_SECONDS)
        finally:
            if serial_port is not None:
                serial_port.close()
    raise RuntimeError("USB status query did not complete")


def main() -> int:
    parser = argparse.ArgumentParser(description="Read safe Orca Buddy USB status.")
    parser.add_argument("port", help="Cardputer ADV USB serial device path")
    args = parser.parse_args()
    try:
        print(read_device_status(args.port))
    except (RuntimeError, ValueError) as error:
        print(f"status: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
