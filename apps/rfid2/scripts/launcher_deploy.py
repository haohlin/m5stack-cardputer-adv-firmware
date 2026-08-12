#!/usr/bin/env python3
"""Stage RFID2 firmware to a mounted Launcher USB MSC volume.

This compatibility helper intentionally has no Wi-Fi, HTTP, session-cookie, or
credential support. Normal project workflows should use ``./cardputer stage
rfid2`` from the repository root.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile


MAX_FIRMWARE_BYTES = 0x4F0000
APP_PREFIX = "RFID2-Clone-Station-"
SAFE_SD_NAME = re.compile(
    rf"^{re.escape(APP_PREFIX)}v\d+\.\d+(?:\.\d+)?"
    r"(?:-[A-Za-z0-9][A-Za-z0-9._-]*)?\.bin$"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_firmware(path: Path) -> None:
    if not path.is_file():
        raise ValueError(f"firmware not found: {path}")
    size = path.stat().st_size
    if size == 0 or size > MAX_FIRMWARE_BYTES:
        raise ValueError(f"firmware size is outside Launcher limits: {size} bytes")
    with path.open("rb") as stream:
        if stream.read(1) != b"\xe9":
            raise ValueError("firmware does not have an ESP32 image header")


def validate_sd_name(name: str) -> None:
    if not SAFE_SD_NAME.fullmatch(name) or Path(name).name != name:
        raise ValueError(
            "sd_name must be an RFID2-Clone-Station-v<version>.bin filename"
        )


def launcher_tools(volume: Path) -> Path:
    if volume.is_symlink() or not volume.is_dir():
        raise ValueError(f"Launcher volume must be one real directory: {volume}")
    resolved_volume = volume.resolve(strict=True)
    tools = resolved_volume / "tools"
    if tools.is_symlink() or not tools.is_dir():
        raise ValueError(f"Launcher tools must be one real directory: {tools}")
    resolved_tools = tools.resolve(strict=True)
    if resolved_tools.parent != resolved_volume:
        raise ValueError("Launcher tools must be a direct child of the mounted volume")
    return resolved_tools


def launcher_volume(explicit: str | None) -> Path:
    if explicit:
        candidates = [Path(explicit).expanduser()]
    else:
        volumes = Path("/Volumes")
        candidates = sorted(volumes.iterdir()) if volumes.is_dir() else []

    matches = []
    for path in candidates:
        try:
            launcher_tools(path)
        except (OSError, ValueError):
            continue
        matches.append(path.resolve(strict=True))
    if len(matches) != 1:
        raise ValueError("expected exactly one mounted Launcher volume containing tools/")
    return matches[0]


def stage(firmware: Path, sd_name: str, volume: Path) -> Path:
    validate_sd_name(sd_name)
    tools = launcher_tools(volume)
    target = tools / sd_name
    if target.is_symlink():
        raise ValueError(f"refusing symlinked Launcher target: {target}")
    fd, partial_name = tempfile.mkstemp(
        prefix=f".{sd_name}.", suffix=".partial", dir=tools
    )
    partial = Path(partial_name)

    try:
        with os.fdopen(fd, "wb") as destination, firmware.open("rb") as source:
            shutil.copyfileobj(source, destination)
            destination.flush()
            os.fsync(destination.fileno())
        if sha256(firmware) != sha256(partial):
            raise OSError("checksum mismatch after staging copy")
        os.replace(partial, target)
    finally:
        if partial.exists():
            partial.unlink()

    for old in tools.iterdir():
        if old == target or old.is_symlink() or not old.is_file():
            continue
        if SAFE_SD_NAME.fullmatch(old.name):
            old.unlink()
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description="Stage firmware to Launcher USB storage")
    parser.add_argument("firmware", help="local ESP32 firmware .bin")
    parser.add_argument("sd_name", help="safe destination filename under tools/")
    parser.add_argument("--volume", help="mounted Launcher volume; auto-detected when omitted")
    args = parser.parse_args()

    try:
        firmware = Path(args.firmware).expanduser().resolve()
        validate_sd_name(args.sd_name)
        validate_firmware(firmware)
        volume = launcher_volume(args.volume)
        target = stage(firmware, args.sd_name, volume)
    except (OSError, ValueError) as error:
        print(f"[DEPLOY] ERROR: {error}", file=sys.stderr)
        return 1

    print(f"[DEPLOY] SUCCESS: {target} ({target.stat().st_size} bytes, sha256={sha256(target)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
