#!/usr/bin/env python3
"""Redact bearer and credential material recursively from diagnostic JSON."""

import json
from pathlib import Path
import sys


SECRET_KEY_MARKERS = (
    "token",
    "password",
    "pass",
    "secret",
    "key",
    "authorization",
    "credential",
    "bearer",
)


def redact(value):
    if isinstance(value, dict):
        output = {}
        for key, child in value.items():
            normalized = str(key).casefold()
            output[key] = "<redacted>" if any(marker in normalized for marker in SECRET_KEY_MARKERS) else redact(child)
        return output
    if isinstance(value, list):
        return [redact(child) for child in value]
    return value


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: redact_debug_json.py <source.json> <destination.json>", file=sys.stderr)
        return 2
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    data = json.loads(source.read_text())
    destination.write_text(json.dumps(redact(data), indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
