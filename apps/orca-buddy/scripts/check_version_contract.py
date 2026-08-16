#!/usr/bin/env python3
"""Reject releases where firmware, plugin, and bridge show different versions."""

import argparse
import json
import re
import sys
from pathlib import Path


VERSION_LINE = re.compile(r'^APP_VERSION="([0-9]+\.[0-9]+\.[0-9]+)"\s*$', re.MULTILINE)
FIRMWARE_VERSION_LINE = re.compile(
    r'^#define ORCA_BUDDY_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\s*$', re.MULTILINE
)


def read_app_version(path: Path) -> str:
    match = VERSION_LINE.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"{path.name}: missing exact APP_VERSION=X.Y.Z")
    return match.group(1)


def read_json_version(path: Path) -> str:
    value = json.loads(path.read_text(encoding="utf-8"))
    version = value.get("version")
    if not isinstance(version, str) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError(f"{path.name}: missing semantic version")
    return version


def read_firmware_version(path: Path) -> str:
    match = FIRMWARE_VERSION_LINE.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"{path.relative_to(path.parents[2])}: missing ORCA_BUDDY_VERSION=X.Y.Z")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    root = arguments.app_root

    try:
        expected = read_app_version(root / "app.env")
        versions = {
            "src/version.h": read_firmware_version(root / "src" / "version.h"),
            "orca-plugin/orca-plugin.json": read_json_version(root / "orca-plugin" / "orca-plugin.json"),
            "desktop-bridge/package.json": read_json_version(root / "desktop-bridge" / "package.json"),
            "desktop-bridge/package-lock.json": read_json_version(root / "desktop-bridge" / "package-lock.json"),
        }
        lock = json.loads((root / "desktop-bridge" / "package-lock.json").read_text(encoding="utf-8"))
        lock_root = lock.get("packages", {}).get("", {}).get("version")
        if lock_root != expected:
            raise ValueError("desktop-bridge/package-lock.json: root package version mismatch")
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"Orca Buddy version contract error: {error}", file=sys.stderr)
        return 1

    mismatched = [path for path, version in versions.items() if version != expected]
    if mismatched:
        print(
            "Orca Buddy version contract error: app.env is "
            f"{expected}; mismatch in {', '.join(mismatched)}",
            file=sys.stderr,
        )
        return 1
    print(expected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
