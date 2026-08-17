#!/usr/bin/env python3
"""Find one Cardputer USB CDC port, allowing transient macOS re-enumeration."""

import glob
import os
import platform
import sys
import time


REENUMERATION_TIMEOUT_SECONDS = 6.0
REENUMERATION_POLL_SECONDS = 0.25


class PortSelectionError(RuntimeError):
    pass


def candidate_patterns(platform_name: str) -> tuple[str, ...]:
    if platform_name == "Darwin":
        return ("/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/cu.usb*")
    if platform_name == "Linux":
        return ("/dev/ttyACM*", "/dev/ttyUSB*")
    return ("/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/ttyACM*", "/dev/ttyUSB*")


def discover_ports(platform_name: str, globber=glob.glob) -> list[str]:
    return sorted({path for pattern in candidate_patterns(platform_name) for path in globber(pattern)})


def select_one(ports: list[str]) -> str:
    if not ports:
        raise PortSelectionError("Cardputer USB serial port not detected.")
    if len(ports) != 1:
        raise PortSelectionError("Multiple Cardputer USB serial ports detected.")
    return ports[0]


def wait_for_single_port(timeout_seconds: float = REENUMERATION_TIMEOUT_SECONDS,
                         poll_seconds: float = REENUMERATION_POLL_SECONDS,
                         platform_name: str | None = None, globber=glob.glob,
                         monotonic=time.monotonic, sleep=time.sleep) -> str:
    current_platform = platform_name or platform.system()
    deadline = monotonic() + timeout_seconds
    last_error: PortSelectionError | None = None
    while True:
        try:
            return select_one(discover_ports(current_platform, globber))
        except PortSelectionError as error:
            last_error = error
        if monotonic() >= deadline:
            raise last_error
        sleep(poll_seconds)


def main() -> int:
    if len(sys.argv) > 2:
        print(f"Usage: {sys.argv[0]} [serial-port]", file=sys.stderr)
        return 2
    explicit = sys.argv[1] if len(sys.argv) == 2 else os.environ.get("CARDPUTER_ADV_PORT", "")
    if explicit:
        print(explicit)
        return 0
    try:
        print(wait_for_single_port())
    except PortSelectionError as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
